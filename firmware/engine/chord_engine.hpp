// chord_engine.hpp - chord mode (chord mode design spec, 2026-09-24).
//
// The guitar itself, driven, through three parallel formant bandpasses
// whose vowel moves with pick dynamics. No pitch tracking anywhere, so it
// plays chords, adds no latency and cannot make an octave error. The grain
// engine (fof_engine.hpp) does not run in chord mode; main.cpp picks one
// engine at boot.
//
//   in -> LiveFrontEnd (gate, peak-normalised amp)
//   in -> soft clip -> 3 x TPT bandpass, summed -> leveler -> x amp x 0.5
//   mouth follower (amp x sensitivity, or forced open) sweeps the formants
//   from the closed vowel to the open vowel, in log frequency.
//
// Every tuning number here is a first-pass ear target (spec section 2);
// test_chord_engine.cpp asserts ranges and relationships, not values.
//
// No new libm: tan and tanh are NOT linked in the firmware today, and
// flash is the binding budget, so the prewarp is sin/cos (both already
// linked) and the drive is a rational soft clip. Allocation-free.
#pragma once
#include <algorithm>
#include <cmath>

#include "front_end.hpp"

struct ChordEngineParams {
  double input_gain = 1.0;     // firmware 1.0; host render + emulator 4.0
  double gate = 0.02;          // kGateLevels[gate_level]
  double drive = 10.0;         // pre-gain into the soft clip, 1..40
  double sensitivity = 3.0;    // mouth = amp x this, 0 = fixed closed vowel
  double closed_vowel = 0.0;   // 0..4: oo oh ah eh ee
  double open_vowel = 2.0;
  double formant_scale = 1.0;  // formant_scale_from(vocal_size)
  double bw_scale = 1.0;       // resonance: 2.0 soft .. 0.35 sharp
  double attack_ms = 10.0;     // mouth follower
  double release_ms = 150.0;
  bool mouth_open = false;     // right stomp held
  double gain = 1.0;           // output levels live in the post chain
};

// Peterson and Barney (1952), adult male averages (spec section 2).
inline constexpr int kVowelCount = 5;
inline constexpr double kVowelHz[kVowelCount][3] = {
    {300, 870, 2240},   // oo (boot)
    {570, 840, 2410},   // oh (bought)
    {730, 1090, 2440},  // ah (father)
    {530, 1840, 2480},  // eh (bet)
    {270, 2290, 3010},  // ee (beet)
};
inline constexpr double kChordBaseBw[3] = {90.0, 110.0, 150.0};  // first-pass
inline constexpr double kChordAmp[3] = {1.0, 0.63, 0.40};        // 0/-4/-8 dB
inline constexpr int kChordCoefEvery = 16;  // samples per coefficient update
// Leveler target. First pass was the grain engine's 0.09; Task 5
// (2026-09-24) calibrated it against a factory Wukong render on
// guitar_long.wav: 0.09 measured chord RMS 0.0294 vs Wukong RMS 0.0293,
// ratio +0.03 dB (already inside the 1 dB target, well under the 3 dB
// gate). Scaled anyway by the measured ratio (new = old * 10^(-dB/20)) for
// an exact match: 0.0897 re-measured at chord RMS 0.0293 vs Wukong 0.0293,
// ratio 0.00 dB, peak 0.239 (no clipping).
inline constexpr double kChordLevelTarget = 0.0897;

// Pade tanh: exact 0 and +/-1 at +/-3, odd, monotone, no libm.
inline double soft_clip(double x) {
  if (x >= 3.0) return 1.0;
  if (x <= -3.0) return -1.0;
  const double x2 = x * x;
  return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}

// tan(pi fc / sr) for the TPT prewarp, from the already-linked sin/cos.
inline double prewarp_g(double fc, double sr) {
  const double w = M_PI * fc / sr;
  return std::sin(w) / std::cos(w);
}

// Vowel knob position (0..4, clamped) to a formant triple in Hz,
// interpolated between neighbouring rows in log frequency.
inline void vowel_formants(double v, double out[3]) {
  v = std::min(std::max(v, 0.0), static_cast<double>(kVowelCount - 1));
  const int i = std::min(static_cast<int>(v), kVowelCount - 2);
  const double t = v - i;
  for (int k = 0; k < 3; k++)
    out[k] = kVowelHz[i][k] * std::pow(kVowelHz[i + 1][k] / kVowelHz[i][k], t);
}

// Zavalishin/Simper topology-preserving SVF, bandpass output normalised to
// unity peak gain (k * v1). Chosen because it stays stable and quiet under
// the fast coefficient modulation the mouth sweep puts on it.
struct TptBandpass {
  double a1 = 1, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0;
  void set(double fc, double bw, double sr) {
    const double g = prewarp_g(fc, sr);
    k = bw / fc;
    a1 = 1.0 / (1.0 + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }
  double process(double v0) {
    const double v3 = v0 - ic2;
    const double v1 = a1 * ic1 + a2 * v3;
    const double v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2 * v1 - ic1;
    ic2 = 2 * v2 - ic2;
    return k * v1;
  }
  void reset() { ic1 = ic2 = 0; }
};

class ChordEngine {
 public:
  explicit ChordEngine(double sr)
      : sr_(sr),
        fe_(sr),
        // the grain engine's leveler constants (fof_engine.hpp): 8 ms
        // tracker, 10 ms reduction slew, 60 ms increase slew
        c_fast_(1 - std::exp(-1 / (0.008 * sr))),
        c_glv_dn_(1 - std::exp(-1 / (0.010 * sr))),
        c_glv_up_(1 - std::exp(-1 / (0.060 * sr))) {
    set_params(ChordEngineParams{});
  }

  // Called once per block from the audio callback. A handful of pow/exp
  // per block, nothing per sample.
  void set_params(const ChordEngineParams& p) {
    p_ = p;
    vowel_formants(p.closed_vowel, closed_);
    vowel_formants(p.open_vowel, open_);
    c_att_ = 1 - std::exp(-1 / (std::max(0.1, p.attack_ms) * 0.001 * sr_));
    c_rel_ = 1 - std::exp(-1 / (std::max(0.1, p.release_ms) * 0.001 * sr_));
  }

  // Engage path: same contract as FofEngine::clear_output_state().
  void clear_output_state() {
    fe_.reset();
    for (auto& b : bp_) b.reset();
    mouth_ = 0.0;
    coef_count_ = 0;
  }

  void process_block(const float* in, float* out, int n) {
    for (int i = 0; i < n; i++) {
      const double x = static_cast<double>(in[i]) * p_.input_gain;
      fe_.process(x, p_.gate);
      const double amp = fe_.live_amp();

      const double target =
          p_.mouth_open ? 1.0 : std::min(1.0, amp * p_.sensitivity);
      mouth_ += (target - mouth_) * (target > mouth_ ? c_att_ : c_rel_);

      if (coef_count_ == 0) update_coefs();
      if (++coef_count_ == kChordCoefEvery) coef_count_ = 0;

      const double d = soft_clip(p_.drive * x);
      double s = 0;
      for (int k = 0; k < 3; k++) s += kChordAmp[k] * bp_[k].process(d);

      // Target leveler BEFORE the envelope multiply, as the grain engine
      // does (FIRMWARE.md section 4), so vowel and resonance changes do
      // not jump in loudness.
      lv_fast_ += (std::fabs(s) - lv_fast_) * c_fast_;
      double g = kChordLevelTarget / (lv_fast_ + 1e-3);
      g = std::min(4.0, std::max(0.25, g));
      lv_g_ += (g - lv_g_) * (g < lv_g_ ? c_glv_dn_ : c_glv_up_);

      out[i] = static_cast<float>(s * lv_g_ * amp * 0.5 * p_.gain);
    }
  }

  double mouth() const { return mouth_; }
  double live_amp() const { return fe_.live_amp(); }

 private:
  void update_coefs() {
    for (int k = 0; k < 3; k++) {
      double f = closed_[k] * std::pow(open_[k] / closed_[k], mouth_) *
                 p_.formant_scale;
      f = std::min(f, 0.45 * sr_);
      const double bw = std::max(1.0, kChordBaseBw[k] * p_.bw_scale);
      bp_[k].set(f, bw, sr_);
    }
  }

  double sr_;
  LiveFrontEnd fe_;
  ChordEngineParams p_{};
  double closed_[3] = {}, open_[3] = {};
  double c_att_ = 0, c_rel_ = 0;
  double mouth_ = 0.0;
  int coef_count_ = 0;
  TptBandpass bp_[3];
  double lv_fast_ = 0.0, lv_g_ = 1.0;
  double c_fast_, c_glv_dn_, c_glv_up_;
};
