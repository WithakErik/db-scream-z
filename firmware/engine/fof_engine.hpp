// fof_engine.hpp
//
// Full transcription of the LIVE path of dbscreamz_lab/static/fof-processor.js:
//   - constructor state          (lines 447-571)
//   - liveSample() ordering      (lines 823-847, via LiveFrontEnd + PitchTracker)
//   - process() synth loop       (lines 910-1076)
//
// Every numeric constant below is traceable to a cited JS line. The offline
// (contour playback) path is deliberately NOT ported: firmware is live-only.
//
// Allocation: none, anywhere, ever (the only remaining init-time heap use is
// inside cycfi/q's bitset, a PitchTracker member; that is a documented,
// accepted exception). Grain tables are double-buffered static arrays so a
// grain rebuild (triggered by set_params on a character/formant change) can
// run on a firmware main loop while process_block runs in the audio ISR:
// rebuild_grains_if_dirty() builds into the INACTIVE set and only then flips
// active_set_, so process_block never observes a half-built table. On host,
// render.cpp calls rebuild_grains_if_dirty() from the same thread right
// before each process_block, which is the single-threaded degenerate case of
// the same protocol and reduces to exactly the old behavior.
#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>

#include "fof_params.hpp"
#include "front_end.hpp"
#include "grain.hpp"
#include "pitch_math.hpp"
#include "pitch_tracker.hpp"

class FofEngine {
 public:
  explicit FofEngine(double sr)
      : sr_(sr),
        fe_(sr),
        pt_(static_cast<float>(sr)),
        // JS line 503: this.obufLen = Math.ceil(this.sr * 0.2)
        obuf_len_(static_cast<int>(std::ceil(sr * 0.2))),
        // JS lines 916-918: leveler coefficients (8 ms level tracker,
        // 10 ms gain-reduction slew, 60 ms gain-increase slew)
        c_fast_(1 - std::exp(-1 / (0.008 * sr))),
        c_glv_dn_(1 - std::exp(-1 / (0.010 * sr))),
        c_glv_up_(1 - std::exp(-1 / (0.060 * sr))) {
    // obuf_ is a fixed-size static array (kObufMax); this asserts the
    // runtime-computed length actually fits it instead of silently
    // overrunning on an unexpectedly high sample rate.
    assert(obuf_len_ <= kObufMax);

    // JS lines 509-516: vphase/vibPhase/jitPhase zeroed. Only index 0 of
    // vibPhase/jitPhase is ever read (one common LFO, lines 986-989), and
    // vibPhase[0] = 2*PI*0/8 = 0, jitPhase[0] = (1.7*0) % 2PI = 0, so the
    // faithful port of the used state is a pair of scalars starting at 0.
    for (int u = 0; u < kMaxUnison; u++) vphase_[u] = 0.0;
    for (int s = 0; s < 2; s++)
      for (int u = 0; u < kMaxUnison; u++)
        for (int k = 0; k < kMaxGrainLen; k++) grain_sets_[s][u][k] = 0.0f;
    for (int k = 0; k < kObufMax; k++) obuf_[k] = 0.0f;
  }

  // JS onMsg 'params' (lines 588-600): a grain-affecting key whose VALUE
  // actually changes marks the grain table dirty.
  //
  // Concurrency contract: firmware calls this from the audio ISR while
  // rebuild_grains_if_dirty() runs on the main loop. param_gen_ is bumped
  // AFTER p_ is copied so a generation-retry read (see
  // rebuild_grains_if_dirty) can detect a torn read and retry; the 32-bit
  // store/load pair is atomic on both x86-64 and Cortex-M7 without needing
  // any intrinsics.
  void set_params(const FofParams& p) {
    if (p.f1 != p_.f1 || p.f2 != p_.f2 || p.f3 != p_.f3 ||
        p.bw1 != p_.bw1 || p.bw2 != p_.bw2 || p.bw3 != p_.bw3 ||
        p.a1 != p_.a1 || p.a2 != p_.a2 || p.a3 != p_.a3 ||
        p.formant_scale != p_.formant_scale || p.grain_ms != p_.grain_ms ||
        p.aspiration != p_.aspiration || p.taper_end != p_.taper_end) {
      grain_dirty_ = true;
    }
    p_ = p;
    // Compiler barrier: forbids reordering the p_ copy above past the
    // param_gen_ publish below, so a concurrent reader that observes the
    // bumped generation is guaranteed to also observe the new p_ (see the
    // matching fences in rebuild_grains_if_dirty's retry loop).
    std::atomic_signal_fence(std::memory_order_seq_cst);
    param_gen_ = param_gen_ + 1;  // plain (not compound) assignment: safe on
                                   // a volatile operand under C++20.
  }

  const FofParams& params() const { return p_; }

  // NEW: lets the firmware main loop decide whether it needs to call
  // rebuild_grains_if_dirty() this pass instead of doing so unconditionally.
  bool grains_dirty() const { return grain_dirty_; }

  // JS line 922: `if (this.grainDirty) this.buildGrain();` at the top of
  // process(). On firmware this runs on the main loop instead of in the
  // audio callback; the host harness calls it once per block from the same
  // thread that calls process_block.
  //
  // Builds into the INACTIVE grain set, then swaps active_set_ as the LAST
  // step, so process_block (reading active_set_ once per block) never sees
  // a half-built table and a swap can never happen mid-block. grain_dirty_
  // is cleared FIRST: if set_params re-dirties the params while this build
  // is in flight, the next main-loop pass simply rebuilds again
  // (self-healing). The generation-retry snapshot below guarantees the
  // build itself never reads a torn FofParams even though set_params can
  // run concurrently with it.
  void rebuild_grains_if_dirty() {
    if (!grain_dirty_) return;
    grain_dirty_ = false;

    // Hazard: p_ is plain (non-volatile) data, so nothing in the visible
    // control flow stops the optimizer from hoisting `snap = p_` out of
    // this loop entirely (no write to p_ is visible here) - which would
    // take one torn snapshot before the loop and still pass the generation
    // check below. The std::atomic_signal_fence calls are full compiler
    // barriers (single-core/ISR fences: no instructions emitted, just a
    // reordering boundary) that pin the copy inside each iteration and
    // pair with the fence in set_params. Do not remove them as "dead code".
    FofParams snap;
    uint32_t g;
    do {
      g = param_gen_;
      std::atomic_signal_fence(std::memory_order_seq_cst);
      snap = p_;
      std::atomic_signal_fence(std::memory_order_seq_cst);
    } while (g != param_gen_);

    const int inactive = 1 - active_set_;
    grain_lens_[inactive] = build_grains(snap, sr_, grain_sets_[inactive]);
    active_set_ = inactive;  // last step: publish the new table set
  }

  // JS onMsg 'live' (lines 604-613): new BACF detector, front-end reset,
  // overlap buffer cleared. curF0 / vphase / vibrato / leveler state are
  // deliberately NOT reset here, matching the JS.
  void reset_live() {
    fe_.reset();
    pt_ = PitchTracker(static_cast<float>(sr_));
    std::fill(obuf_, obuf_ + obuf_len_, 0.0f);
    owrite_ = 0;
    oread_ = 0;
  }

  // Firmware engage path (milestone 5): clear the envelope/gate front end
  // and the overlap buffer WITHOUT reconstructing PitchTracker, so the held
  // f0 survives a bypass/engage toggle. v12 keeps liveF0 across the JS
  // 'live' handler (MILESTONE0.md section 7); reset_live() above remains
  // the full transcription of that handler and host render never calls
  // either method on the live path.
  void clear_output_state() {
    fe_.reset();
    std::fill(obuf_, obuf_ + obuf_len_, 0.0f);
    owrite_ = 0;
    oread_ = 0;
  }

  // Mono in (RAW wav sample, pre-inputGain), mono out. n <= 128.
  void process_block(const float* in, float* out, int n) {
    // Read active_set_ ONCE per block into a local: the whole block uses
    // this set's tables and length, even if rebuild_grains_if_dirty() swaps
    // active_set_ concurrently partway through (firmware ISR contract).
    const int as = active_set_;
    const FofParams& p = p_;
    const int gl = grain_lens_[as];

    // JS lines 938-943
    const int n_uni =
        std::max(1, std::min(kMaxUnison, static_cast<int>(jsRound(p.unison))));
    double sum_g = 0;
    for (int u = 0; u < n_uni; u++) {
      const double sp = (n_uni == 1) ? 0.0
                                     : (static_cast<double>(u) / (n_uni - 1)) * 2 - 1;
      sum_g += 1 - 0.45 * std::fabs(sp);
    }
    // JS line 944: glideMs is floored at 1 ms, so glideCoef is well-defined
    // even at glideMs = 0 (the default).
    const double glide_coef =
        std::exp(-1 / (std::max(1.0, p.glide_ms) * 0.001 * sr_));

    for (int i = 0; i < n; i++) {
      // ---- JS lines 948-962: live front end, then tracker, then reads ----
      // The JS multi-channel average (lines 950-956) collapses to a
      // pass-through for a mono input.
      const double x = static_cast<double>(in[i]) * p.input_gain;
      fe_.process(x, p.gate);                        // liveSample() 824-838
      pt_.process(static_cast<float>(x));            // liveSample() 842-846
      const double raw_f0 = pt_.f0();                // line 958
      const double amp = fe_.live_amp() * 0.5;       // line 962, 0.5 headroom

      // JS lines 974-976: register map + portamento
      const double target = register_map(raw_f0, p);
      cur_f0_ = target + (cur_f0_ - target) * glide_coef;

      // JS lines 986-989: ONE common vibrato LFO for the whole unison
      // stack; the jitter modulates the RATE, never the audio.
      jit_phase_ += (2 * M_PI * 0.31) / sr_;
      const double vib_rate = p.vib_rate * (1 + p.vib_jitter * std::sin(jit_phase_));
      vib_phase_ += (2 * M_PI * vib_rate) / sr_;
      const double vib = p.vib_depth * std::sin(vib_phase_);

      // ---- JS lines 992-1036: trigger grains per unison voice ----
      for (int u = 0; u < n_uni; u++) {
        const double spread =
            (n_uni == 1) ? 0.0 : (static_cast<double>(u) / (n_uni - 1)) * 2 - 1;
        const double v_gain = 1 - 0.45 * std::fabs(spread);   // side taper

        const double semis = (spread * p.detune_cents) / 100 + vib;
        double f = cur_f0_ * std::pow(2.0, semis / 12.0);
        f = std::min(std::max(f, 16.0), 2000.0);   // clamp BEFORE quantize
        f = quantize_hz(f, p.quantize);

        vphase_[u] += f / sr_;
        if (vphase_[u] >= 1) {
          vphase_[u] -= std::floor(vphase_[u]);
          // Overlap normalisation PER GRAIN, at TRIGGER time (lines 1009-1022).
          // gl is the grain length of the ACTIVE set as of the top of this
          // block: a grain-length change from a rebuild only takes effect
          // for triggers in the block after the swap, matching the JS
          // engine (a rebuild swaps this.grains/this.grainLen between
          // process() calls, never mid-call).
          const double overlap_at_trig = std::max(1.0, (gl * f) / sr_);
          double gain = (0.55 * v_gain) / std::sqrt(overlap_at_trig);
          if (p.amp_comp) gain *= amp_comp_gain(f);   // lines 1023-1027

          const float* grain = grain_sets_[as][u];
          int w = owrite_;
          for (int k = 0; k < gl; k++) {
            // obuf is a Float32Array in JS: the accumulation rounds to
            // float on every store. Keep that exactly.
            obuf_[w] += grain[k] * gain;
            w++;
            if (w >= obuf_len_) w = 0;
          }
        }
      }

      // ---- JS lines 1039-1047: read one sample, clear it, advance both ----
      double s = obuf_[oread_];
      obuf_[oread_] = 0.0f;
      oread_++; if (oread_ >= obuf_len_) oread_ = 0;
      owrite_++; if (owrite_ >= obuf_len_) owrite_ = 0;

      double raw = s / sum_g;

      // ---- JS lines 1058-1068: TARGET LEVELER (before the amp multiply) ----
      if (p.leveler) {
        const double a = std::fabs(raw);
        lv_fast_ += (a - lv_fast_) * c_fast_;
        double g = 0.09 / (lv_fast_ + 1e-3);
        if (g < 0.25) g = 0.25; else if (g > 4.0) g = 4.0;
        // asymmetric slew: reduction fast (10 ms), increase slow (60 ms)
        lv_g_ += (g - lv_g_) * (g < lv_g_ ? c_glv_dn_ : c_glv_up_);
        raw *= lv_g_;
      }

      out[i] = static_cast<float>(raw * amp * p.gain);   // JS line 1070
    }
  }

  double live_f0()  const { return pt_.f0(); }        // JS this.liveF0
  double live_amp() const { return fe_.live_amp(); }  // JS this.liveAmp (pre-0.5)
  int    grain_len() const { return grain_lens_[active_set_]; }

 private:
  FofParams p_{};
  double sr_;
  LiveFrontEnd fe_;
  PitchTracker pt_;

  // Double-buffered grain tables: rebuild_grains_if_dirty() builds into
  // grain_sets_[1 - active_set_] and then publishes it by flipping
  // active_set_, so process_block (which snapshots active_set_ once per
  // block) never observes a partially-built table.
  float grain_sets_[2][kMaxUnison][kMaxGrainLen];
  int   grain_lens_[2] = {1, 1};   // JS line 499 (this.grainLen = 1)
  volatile int active_set_ = 0;    // index read once by process_block
  // JS line 500. Volatile like its siblings above: set_params writes this from
  // the audio ISR while the firmware main loop polls it via grains_dirty() and
  // clears it in rebuild_grains_if_dirty(). Without volatile nothing in the
  // visible control flow stops the optimizer from hoisting the main-loop poll
  // out of the while(true) loop (it sees no write to the flag there), which
  // would wedge grain rebuilds permanently under LTO or a more aggressive
  // inliner. A single aligned bool load/store is atomic on Cortex-M7.
  volatile bool grain_dirty_ = true;

  // set_params/rebuild_grains_if_dirty concurrency: bumped after p_ is
  // copied in set_params; rebuild_grains_if_dirty snapshots p_ under a
  // generation-retry loop so it never reads a torn struct.
  volatile uint32_t param_gen_ = 0;

  int   obuf_len_;
  float obuf_[kObufMax];         // was std::vector<float>, sized obuf_len_
  int   owrite_ = 0, oread_ = 0; // JS lines 505-506

  double vphase_[kMaxUnison];    // JS line 509
  double vib_phase_ = 0.0;       // JS vibPhase[0], line 513 with i = 0
  double jit_phase_ = 0.0;       // JS jitPhase[0], line 514 with i = 0
  double cur_f0_ = 200.0;        // JS line 516

  double lv_fast_ = 0.0;         // JS line 519
  double lv_g_ = 1.0;            // JS line 919
  double c_fast_, c_glv_dn_, c_glv_up_;
};
