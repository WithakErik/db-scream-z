// test_chord_engine.cpp - chord mode DSP (chord mode spec section 2).
// Asserts ranges and relationships, not tuning values: the first-pass
// numbers in chord_engine.hpp are due an ear pass.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "chord_engine.hpp"

static constexpr double kSr = 48000.0;

// Magnitude of frequency f in x (Goertzel), normalized to sine amplitude.
static double tone_mag(const std::vector<float>& x, size_t from, double f) {
  const double w = 2 * M_PI * f / kSr, c = 2 * std::cos(w);
  double s1 = 0, s2 = 0;
  for (size_t i = from; i < x.size(); i++) {
    const double s0 = x[i] + c * s1 - s2;
    s2 = s1; s1 = s0;
  }
  const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
  return 2 * std::sqrt(re * re + im * im) / (x.size() - from);
}

static std::vector<float> run(ChordEngine& e, const std::vector<float>& in) {
  std::vector<float> out(in.size());
  for (size_t i = 0; i < in.size(); i += 128) {
    const int n = static_cast<int>(std::min<size_t>(128, in.size() - i));
    e.process_block(in.data() + i, out.data() + i, n);
  }
  return out;
}

static std::vector<float> sines(std::initializer_list<double> fs, double amp,
                                double secs) {
  std::vector<float> x(static_cast<size_t>(secs * kSr));
  for (size_t i = 0; i < x.size(); i++) {
    double s = 0;
    for (double f : fs) s += amp * std::sin(2 * M_PI * f * i / kSr);
    x[i] = static_cast<float>(s);
  }
  return x;
}

static double rms(const std::vector<float>& x, size_t from) {
  double a = 0;
  for (size_t i = from; i < x.size(); i++) a += double(x[i]) * x[i];
  return std::sqrt(a / (x.size() - from));
}

int main() {
  // ---- soft clip: odd, bounded, ~identity when small, monotone
  {
    assert(soft_clip(0.0) == 0.0);
    assert(std::fabs(soft_clip(0.01) - 0.01) < 1e-5);
    double prev = -2;
    for (double x = -10; x <= 10; x += 0.01) {
      const double y = soft_clip(x);
      assert(y >= -1.0 && y <= 1.0);
      assert(std::fabs(y + soft_clip(-x)) < 1e-12);
      assert(y >= prev);
      prev = y;
    }
  }

  // ---- prewarp within 0.1% of tan from 20 Hz to 0.45 x sr
  for (double f = 20; f <= 0.45 * kSr; f *= 1.05) {
    const double t = std::tan(M_PI * f / kSr);
    assert(std::fabs(prewarp_g(f, kSr) - t) <= 1e-3 * t);
  }

  // ---- vowel table: integer knob positions are exact, ends clamp,
  // interpolation is in log frequency (geometric mean at the midpoint)
  {
    double f[3];
    for (int v = 0; v < 5; v++) {
      vowel_formants(v, f);
      for (int k = 0; k < 3; k++) assert(std::fabs(f[k] - kVowelHz[v][k]) < 1e-9);
    }
    vowel_formants(-1.0, f);
    assert(f[0] == kVowelHz[0][0]);
    vowel_formants(9.0, f);
    assert(f[2] == kVowelHz[4][2]);
    vowel_formants(0.5, f);
    assert(std::fabs(f[0] - std::sqrt(kVowelHz[0][0] * kVowelHz[1][0])) < 1e-9);
    // spec table, Peterson and Barney adult male
    assert(kVowelHz[0][0] == 300 && kVowelHz[2][1] == 1090 && kVowelHz[4][2] == 3010);
  }

  // ---- bandpass: unity at center, >= 12 dB down two octaves away
  {
    for (double fc : {300.0, 1090.0, 2440.0}) {
      TptBandpass bp;
      bp.set(fc, fc / 10, kSr);
      auto x = sines({fc}, 1.0, 1.0);
      std::vector<float> y(x.size());
      for (size_t i = 0; i < x.size(); i++) y[i] = static_cast<float>(bp.process(x[i]));
      const double at = tone_mag(y, y.size() / 2, fc);
      assert(at > 0.9 && at < 1.1);
      bp.reset();
      auto x2 = sines({fc / 4}, 1.0, 1.0);
      for (size_t i = 0; i < x2.size(); i++) y[i] = static_cast<float>(bp.process(x2[i]));
      assert(tone_mag(y, y.size() / 2, fc / 4) < 0.25);
    }
  }

  // ---- mouth: sensitivity 0 holds it shut; forced open follows the
  // attack time; release follows the release time
  {
    ChordEngine e(kSr);
    ChordEngineParams p;
    p.sensitivity = 0.0;
    e.set_params(p);
    run(e, sines({220}, 0.5, 0.5));
    assert(e.mouth() == 0.0);

    p.mouth_open = true;
    p.attack_ms = 10.0;
    e.set_params(p);
    run(e, std::vector<float>(480, 0.0f));   // exactly 10 ms
    assert(std::fabs(e.mouth() - (1 - std::exp(-1.0))) < 0.05);
    run(e, std::vector<float>(4800, 0.0f));
    assert(e.mouth() > 0.99);

    p.mouth_open = false;
    p.release_ms = 150.0;
    e.set_params(p);
    run(e, std::vector<float>(7200, 0.0f));  // exactly 150 ms
    assert(std::fabs(e.mouth() - std::exp(-1.0)) < 0.05);
  }

  // ---- the chord check: two simultaneous notes both come out
  {
    ChordEngine e(kSr);
    ChordEngineParams p;
    p.drive = 1.0;   // near-linear, so the test sees the filters
    e.set_params(p);
    auto y = run(e, sines({220, 330}, 0.3, 1.5));
    const size_t from = y.size() / 3;
    const double a = tone_mag(y, from, 220), b = tone_mag(y, from, 330);
    assert(a > 0 && b > 0);
    const double big = std::max(a, b), small = std::min(a, b);
    assert(20 * std::log10(small / big) > -20.0);
  }

  // ---- leveler: steady input, every vowel within 6 dB of every other
  {
    double lo = 1e9, hi = 0;
    for (int v = 0; v < 5; v++) {
      ChordEngine e(kSr);
      ChordEngineParams p;
      p.closed_vowel = p.open_vowel = v;
      e.set_params(p);
      auto y = run(e, sines({110, 220, 330, 440, 550}, 0.1, 1.5));
      const double r = rms(y, y.size() / 2);
      lo = std::min(lo, r);
      hi = std::max(hi, r);
    }
    assert(lo > 0 && 20 * std::log10(hi / lo) < 6.0);
  }

  // ---- silence in is silence out, even at maximum drive
  {
    ChordEngine e(kSr);
    ChordEngineParams p;
    p.drive = 40.0;
    e.set_params(p);
    auto y = run(e, std::vector<float>(48000, 0.0f));
    for (float s : y) assert(s == 0.0f);
  }

  // ---- after playing, the gate brings it back to (near) silence
  {
    ChordEngine e(kSr);
    ChordEngineParams p;
    p.drive = 40.0;
    e.set_params(p);
    auto x = sines({196, 247, 294}, 0.2, 0.5);
    x.resize(x.size() + 48000, 0.0f);
    auto y = run(e, x);
    for (size_t i = y.size() - 4800; i < y.size(); i++) assert(std::fabs(y[i]) < 1e-4);
  }

  // ---- extreme settings stay finite and bounded (Review Focus 1)
  {
    for (double fs : {0.5, 1.0}) {
      ChordEngine e(kSr);
      ChordEngineParams p;
      p.input_gain = 4.0;
      p.drive = 40.0;
      p.bw_scale = 0.35;
      p.formant_scale = fs;
      p.sensitivity = 8.0;
      p.closed_vowel = 4;
      p.open_vowel = 0;
      e.set_params(p);
      std::vector<float> x(96000);
      uint32_t r = 1;
      for (auto& s : x) { r = r * 1664525u + 1013904223u; s = (r >> 8) / 8388608.0f - 1.0f; }
      auto y = run(e, x);
      for (float s : y) assert(std::isfinite(s) && std::fabs(s) < 10.0f);
    }
  }

  std::printf("test_chord_engine OK\n");
  return 0;
}
