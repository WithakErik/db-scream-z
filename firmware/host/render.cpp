// firmware/host/render.cpp
//
// Host WAV render CLI for the ported FOF engine. Mirrors tools/ref_render.js
// exactly: same character baking (the v12 param set = app.js P defaults +
// applyPreset()), same 128-sample block loop with a zero-padded final block,
// same `block,f0,amp` trace CSV.
//
//   ./render <in.wav> <out.wav> --character Wukong [--trace f.csv] [--set k=v ...]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "fof_engine.hpp"
#include "presets.hpp"
#include "wav_io.hpp"

namespace {

// presets.json f0_hz per character. Not in presets.hpp because firmware does
// not need it (it only matters when followPitch is off or f0 < 40 Hz), but
// milestone parity with ref_render.js wants it right.
double preset_f0_hz(const std::string& name) {
  if (name == "Wukong")  return 451.9;
  if (name == "Rice")    return 534.8;
  if (name == "Prince")  return 379.8;
  if (name == "Piccolo") return 354.1;
  return 452.0;
}

// --set keys are the JS param names (ref_render.js line 81 writes straight
// into the worklet's `p`), mapped here onto the snake_cased FofParams fields.
bool apply_set(FofParams& p, const std::string& k, double v) {
  if (k == "f1") { p.f1 = v; return true; }
  if (k == "f2") { p.f2 = v; return true; }
  if (k == "f3") { p.f3 = v; return true; }
  if (k == "bw1") { p.bw1 = v; return true; }
  if (k == "bw2") { p.bw2 = v; return true; }
  if (k == "bw3") { p.bw3 = v; return true; }
  if (k == "a1") { p.a1 = v; return true; }
  if (k == "a2") { p.a2 = v; return true; }
  if (k == "a3") { p.a3 = v; return true; }
  if (k == "formantScale") { p.formant_scale = v; return true; }
  if (k == "grainMs") { p.grain_ms = v; return true; }
  if (k == "unison") { p.unison = static_cast<int>(jsRound(v)); return true; }
  if (k == "detuneCents") { p.detune_cents = v; return true; }
  if (k == "vibRate") { p.vib_rate = v; return true; }
  if (k == "vibDepth") { p.vib_depth = v; return true; }
  if (k == "vibJitter") { p.vib_jitter = v; return true; }
  if (k == "aspiration") { p.aspiration = v; return true; }
  if (k == "quantize") { p.quantize = (v != 0); return true; }
  if (k == "ampComp") { p.amp_comp = (v != 0); return true; }
  if (k == "glideMs") { p.glide_ms = v; return true; }
  if (k == "targetF0") { p.target_f0 = v; return true; }
  if (k == "octaveShift") { p.octave_shift = static_cast<int>(jsRound(v)); return true; }
  if (k == "followPitch") { p.follow_pitch = (v != 0); return true; }
  if (k == "taperEnd") { p.taper_end = (v != 0); return true; }
  if (k == "leveler") { p.leveler = (v != 0); return true; }
  if (k == "inputGain") { p.input_gain = v; return true; }
  if (k == "gate") { p.gate = v; return true; }
  if (k == "gain") { p.gain = v; return true; }
  return false;
}

// JS params the live firmware path does not model. ref_render.js accepts
// them; we accept and warn rather than silently changing nothing.
bool is_unmodelled_key(const std::string& k) {
  return k == "loF0" || k == "hiF0" || k == "registerMode" ||
         k == "fastTrack" || k == "useBacf" || k == "breathiness";
}

const char* opt(int argc, char** argv, const char* name) {
  for (int i = 1; i < argc - 1; i++)
    if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: render <in.wav> <out.wav|-> --character <Name> "
                 "[--trace f.csv] [--set key=value ...]\n");
    return 1;
  }
  const std::string in_path = argv[1];
  const std::string out_path = argv[2];
  const char* c_opt = opt(argc, argv, "--character");
  const std::string character = c_opt ? c_opt : "Wukong";
  const char* trace_path = opt(argc, argv, "--trace");

  const CharacterPreset* pr = nullptr;
  for (const auto& cp : kPresets)
    if (character == cp.name) pr = &cp;
  if (!pr) {
    std::fprintf(stderr, "unknown --character '%s'; available:", character.c_str());
    for (const auto& cp : kPresets) std::fprintf(stderr, " %s", cp.name);
    std::fprintf(stderr, "\n");
    return 1;
  }

  // ---- v12 param set: app.js P defaults + applyPreset() baking ----
  // (ref_render.js lines 69-78; the character supplies formants + vibrato
  // only, formantScale is already baked into formants_hz)
  FofParams p;
  p.f1 = pr->formants_hz[0];
  p.f2 = pr->formants_hz[1];
  p.f3 = pr->formants_hz[2];
  p.bw1 = 32.5; p.bw2 = 47.5; p.bw3 = 62.5;
  p.a1 = 1.0; p.a2 = 1.0; p.a3 = 1.0;
  p.formant_scale = 1.0;
  p.grain_ms = 20;
  p.unison = 3;
  p.detune_cents = 11;
  p.aspiration = 0.0;
  p.target_f0 = preset_f0_hz(character);
  p.octave_shift = 0;
  p.follow_pitch = true;
  p.quantize = true;
  p.amp_comp = true;
  p.glide_ms = 0;
  p.taper_end = true;
  p.leveler = true;
  p.input_gain = 4.0;
  p.gate = 0.02;
  p.vib_rate = pr->vib_rate_hz;
  p.vib_depth = pr->vib_depth_semi;
  p.vib_jitter = 0.10;
  p.gain = 1.0;

  for (int i = 1; i < argc - 1; i++) {
    if (std::strcmp(argv[i], "--set") != 0) continue;
    const std::string kv = argv[i + 1];
    const size_t eq = kv.find('=');
    if (eq == std::string::npos) {
      std::fprintf(stderr, "bad --set '%s' (expected key=value)\n", kv.c_str());
      return 1;
    }
    const std::string k = kv.substr(0, eq);
    const double v = std::atof(kv.substr(eq + 1).c_str());
    if (!apply_set(p, k, v)) {
      if (is_unmodelled_key(k)) {
        std::fprintf(stderr,
                     "warning: --set %s is not modelled by the live firmware "
                     "path; ignored\n", k.c_str());
      } else {
        std::fprintf(stderr, "unknown --set key '%s'\n", k.c_str());
        return 1;
      }
    }
  }

  WavData input;
  try {
    input = wav_read_mono(in_path);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
  const int sr = input.sample_rate;
  const int N = static_cast<int>(input.samples.size());

  // grains_[8][4800] is too big for the stack; heap-allocate the engine.
  auto eng = std::make_unique<FofEngine>(static_cast<double>(sr));
  eng->set_params(p);
  eng->reset_live();   // ref_render.js line 87: onMsg({type:'live', on:1})

  std::vector<float> out(N, 0.0f);
  std::string trace;
  if (trace_path) trace = "block,f0,amp\n";

  float blk[128], oblk[128];
  for (int b = 0; b * 128 < N; b++) {
    const int s0 = b * 128;
    const int n = std::min(128, N - s0);
    // ref_render.js lines 108-110 zero-fill the block and always hand the
    // worklet a full 128 frames, so the final partial block is zero-padded
    // and the engine's state advances by 128 samples for it too.
    for (int k = 0; k < 128; k++) blk[k] = 0.0f;
    for (int k = 0; k < 128; k++) oblk[k] = 0.0f;
    for (int k = 0; k < n; k++) blk[k] = input.samples[s0 + k];

    eng->rebuild_grains_if_dirty();
    eng->process_block(blk, oblk, 128);

    for (int k = 0; k < n; k++) out[s0 + k] = oblk[k];

    if (trace_path) {
      char row[96];
      std::snprintf(row, sizeof(row), "%d,%.17g,%.17g\n", b, eng->live_f0(),
                    eng->live_amp());
      trace += row;
    }
  }

  if (out_path != "-") wav_write_mono(out_path, sr, out);
  if (trace_path) {
    FILE* f = std::fopen(trace_path, "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", trace_path); return 1; }
    std::fwrite(trace.data(), 1, trace.size(), f);
    std::fclose(f);
  }

  double peak = 0;
  for (int i = 0; i < N; i++) peak = std::max(peak, (double)std::fabs(out[i]));
  if (!std::isfinite(peak)) { std::fprintf(stderr, "NON-FINITE OUTPUT\n"); return 1; }
  std::printf("rendered %s: %d samples @ %d Hz, peak %.3f\n",
              character.c_str(), N, sr, peak);
  return 0;
}
