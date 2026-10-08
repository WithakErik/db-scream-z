// pitch_shifter.hpp - chord mode's pitch shift (chord octave design spec,
// 2026-10-06). No pitch detection anywhere: a two-head delay line, so it
// shifts whole chords. ShiftStage wraps it with the octave glide and a dry
// bypass that keeps octave 0 a bit-exact wire (zero added latency).
//
// Two read heads half a window apart sweep through the last kShiftWindowMs
// of input at `ratio` times the write speed. Each head fades out as it
// reaches the window edge and wraps while silent; the fades are equal
// power, so uncorrelated material (any real chord) keeps its level.
//
// No new libm: exp and pow (double) are already linked, the fade is a
// polynomial rather than sin, and the phase wraps by compare, not floor.
// Allocation-free. Every number here is a first-pass ear target.
#pragma once
#include <algorithm>
#include <cmath>

inline constexpr double kShiftWindowMs = 40.0;   // head sweep window
inline constexpr double kShiftBypassMs = 10.0;   // dry <-> shifted fade
inline constexpr double kShiftMaxOctaves = 3.0;  // toggle +/-1, charge +/-2
inline constexpr int kShiftRatioEvery = 16;      // samples per ratio update
inline constexpr double kShiftDryEps = 1e-3;     // octaves: glide has landed

// sin(pi p) for p in [0, 1], Bhaskara I's approximation: exact at 0, 0.5
// and 1, within 0.2% elsewhere.
inline double shift_fade(double p) {
  const double q = p * (1.0 - p);
  return 16.0 * q / (5.0 - 4.0 * q);
}

struct PitchShifter {
  static constexpr int kBufSize = 4096;  // power of two
  float buf[kBufSize] = {};
  int w = 0;            // next write index
  double win = 1920.0;  // window, samples
  double phase = 0.0;   // head A's delay as a fraction of win, [0, 1)
  double ratio = 1.0;   // read speed / write speed

  void init(double sr) {
    win = std::min(kShiftWindowMs * 0.001 * sr,
                   static_cast<double>(kBufSize - 4));
    reset();
  }
  void reset() {
    std::fill(buf, buf + kBufSize, 0.0f);
    w = 0;
    phase = 0.0;
  }
  // Linear interpolation d samples back from the newest sample (d >= 0).
  double read(double d) const {
    const int i = static_cast<int>(d);
    const double f = d - i;
    const double a = buf[(w - i) & (kBufSize - 1)];
    const double b = buf[(w - i - 1) & (kBufSize - 1)];
    return a + (b - a) * f;
  }
  double process(double x) {
    buf[w] = static_cast<float>(x);
    const double pa = phase;
    double pb = phase + 0.5;
    if (pb >= 1.0) pb -= 1.0;
    const double y = read(pa * win) * shift_fade(pa) +
                     read(pb * win) * shift_fade(pb);
    // The delay grows (ratio < 1) or shrinks (ratio > 1) by |1 - ratio|
    // samples per sample; at most 7/win per step, so one wrap suffices.
    phase += (1.0 - ratio) / win;
    if (phase >= 1.0) phase -= 1.0;
    else if (phase < 0.0) phase += 1.0;
    w = (w + 1) & (kBufSize - 1);
    return y;
  }
};

class ShiftStage {
 public:
  explicit ShiftStage(double sr)
      : sr_(sr), c_wet_(1.0 / (kShiftBypassMs * 0.001 * sr)) {
    ps_.init(sr);
    set_target(0.0, 0.0);
  }

  // Once per block. The glide is the grain engine's one-pole portamento
  // (fof_engine.hpp glide_coef), floored at 1 ms the same way.
  void set_target(double octaves, double glide_ms) {
    target_ = std::min(kShiftMaxOctaves, std::max(-kShiftMaxOctaves, octaves));
    c_glide_ = std::exp(-1 / (std::max(1.0, glide_ms) * 0.001 * sr_));
  }

  // Engage path: clear the history and arrive at the target at once.
  void reset() {
    ps_.reset();
    oct_ = target_;
    wet_ = target_ == 0.0 ? 0.0 : 1.0;
    count_ = 0;
  }

  double process(double x) {
    oct_ = target_ + (oct_ - target_) * c_glide_;
    if (std::fabs(oct_ - target_) < 1e-9) oct_ = target_;  // no denormal tail
    if (count_ == 0) ps_.ratio = std::pow(2.0, oct_);
    if (++count_ == kShiftRatioEvery) count_ = 0;
    const double s = ps_.process(x);  // always written: re-entry has history
    const bool dry = target_ == 0.0 && std::fabs(oct_) < kShiftDryEps;
    wet_ = dry ? std::max(0.0, wet_ - c_wet_) : std::min(1.0, wet_ + c_wet_);
    if (wet_ == 0.0) return x;
    if (wet_ == 1.0) return s;
    return x + (s - x) * wet_;
  }

  double octaves() const { return oct_; }
  double wet() const { return wet_; }

 private:
  double sr_;
  double c_wet_;
  PitchShifter ps_;
  double target_ = 0.0, oct_ = 0.0, c_glide_ = 0.0, wet_ = 0.0;
  int count_ = 0;
};
