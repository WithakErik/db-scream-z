// test_pitch_shifter.cpp - chord mode's pitch shift (chord octave design
// spec, 2026-10-06): the two-head delay line on its own, then ShiftStage's
// glide, clamp, bypass and engage reset.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "pitch_shifter.hpp"

static constexpr double kSr = 48000.0;

// Magnitude of frequency f in x (Goertzel), normalized to sine amplitude.
static double tone_mag(const std::vector<double>& x, size_t from, double f) {
  const double w = 2 * M_PI * f / kSr, c = 2 * std::cos(w);
  double s1 = 0, s2 = 0;
  for (size_t i = from; i < x.size(); i++) {
    const double s0 = x[i] + c * s1 - s2;
    s2 = s1; s1 = s0;
  }
  const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
  return 2 * std::sqrt(re * re + im * im) / (x.size() - from);
}

// Energy in a band of +/- 25% around fc. A single Goertzel bin is the
// wrong probe here: the two-head crossfade spreads a shifted sine into
// lines spaced by the head cycle rate, and the exact centre line can null.
static double band(const std::vector<double>& y, size_t from, double fc) {
  double e = 0;
  for (double f = fc / 1.25; f <= fc * 1.25; f += fc / 200) {
    const double m = tone_mag(y, from, f);
    e += m * m;
  }
  return e;
}

// White noise through a one-pole lowpass (guitar-like spectrum). Linear
// interpolation loses up to 3 dB on raw white noise at half-sample reads,
// which would be testing the interpolator, not the crossfade.
struct Noise {
  uint32_t r = 1;
  double lp = 0;
  double next() {
    r = r * 1664525u + 1013904223u;
    lp += ((r >> 8) / 8388608.0 - 1.0 - lp) * 0.25;
    return lp;
  }
};

static PitchShifter ps;  // 16 KB: static, not on the stack

int main() {
  // ---- crossfade shape: exact 0 at the wrap, exact 1 mid-window, and
  // equal power for heads half a window apart (within 1%)
  {
    assert(shift_fade(0.0) == 0.0 && shift_fade(1.0) == 0.0);
    assert(shift_fade(0.5) == 1.0);
    for (double p = 0; p < 0.5; p += 0.01) {
      const double a = shift_fade(p), b = shift_fade(p + 0.5);
      assert(std::fabs(a * a + b * b - 1.0) < 0.01);
    }
  }

  // ---- window scales with the sample rate and always fits the buffer
  // (Review Focus 5: the emulator may run at 44.1 kHz)
  {
    ps.init(48000.0);
    assert(ps.win == 1920.0);
    ps.init(44100.0);
    assert(std::fabs(ps.win - 1764.0) < 1e-9);
    ps.init(192000.0);
    assert(ps.win == PitchShifter::kBufSize - 4);
  }

  // ---- ratio 1 is an exact delay of half a window: head A sits at the
  // wrap (fade 0), head B mid-window (fade 1), integer delay
  {
    ps.init(kSr);
    ps.ratio = 1.0;
    Noise n;
    std::vector<float> x(9600);
    for (auto& s : x) s = static_cast<float>(n.next());
    for (size_t i = 0; i < x.size(); i++) {
      const double y = ps.process(x[i]);
      const double want = i >= 960 ? x[i - 960] : 0.0;
      assert(y == want);
    }
  }

  // ---- pitch: a 220 Hz sine comes out an octave (or three) away, with
  // next to nothing left at 220 Hz
  for (double oct : {1.0, -1.0, 3.0, -3.0}) {
    ps.init(kSr);
    ps.ratio = std::pow(2.0, oct);
    std::vector<double> y(96000);
    for (size_t i = 0; i < y.size(); i++)
      y[i] = ps.process(0.5 * std::sin(2 * M_PI * 220 * i / kSr));
    const double at = band(y, 24000, 220 * std::pow(2.0, oct));
    const double orig = band(y, 24000, 220.0);
    assert(at > 20 * orig);
  }

  // ---- level: guitar-like noise keeps its RMS within 1.5 dB at every
  // shift (the crossfade is equal power)
  for (double oct : {1.0, -1.0, 3.0, -3.0}) {
    ps.init(kSr);
    ps.ratio = std::pow(2.0, oct);
    Noise n;
    double ain = 0, aout = 0;
    for (int i = 0; i < 4 * 48000; i++) {
      const double x = n.next();
      const double y = ps.process(x);
      if (i > 4800) { ain += x * x; aout += y * y; }
    }
    assert(std::fabs(10 * std::log10(aout / ain)) < 1.5);
  }

  // ---- reset clears the history: zeros in, zeros out at once
  {
    ps.init(kSr);
    ps.ratio = 2.0;
    for (int i = 0; i < 4800; i++) ps.process(0.7);
    ps.reset();
    for (int i = 0; i < 4800; i++) assert(ps.process(0.0) == 0.0);
  }

  // ---- ShiftStage at target 0 is a bit-exact wire (zero latency)
  {
    ShiftStage st(kSr);
    Noise n;
    for (int i = 0; i < 48000; i++) {
      const double x = n.next();
      assert(st.process(x) == x);
      assert(st.wet() == 0.0);
    }
  }

  // ---- clamp: a target past +/-3 octaves lands on +/-3 (Review Focus 3:
  // a corrupt stored byte can never shift further)
  {
    ShiftStage st(kSr);
    st.set_target(7.0, 0.0);
    for (int i = 0; i < 4800; i++) st.process(0.0);
    assert(st.octaves() == 3.0);
    st.set_target(-7.0, 0.0);
    for (int i = 0; i < 4800; i++) st.process(0.0);
    assert(st.octaves() == -3.0);
  }

  // ---- glide 0 arrives within 20 ms; glide 2000 ms is the one-pole
  // portamento (63% at 2 s), monotone, and lands exactly
  {
    ShiftStage st(kSr);
    st.set_target(1.0, 0.0);
    for (int i = 0; i < 960; i++) st.process(0.0);
    assert(std::fabs(st.octaves() - 1.0) < 1e-3);

    ShiftStage g(kSr);
    g.set_target(2.0, 2000.0);
    double prev = 0.0;
    for (int k = 0; k < 20; k++) {
      for (int i = 0; i < 4800; i++) g.process(0.0);
      assert(g.octaves() > prev);
      prev = g.octaves();
    }
    assert(std::fabs(prev - 2.0 * (1 - std::exp(-1.0))) < 0.01);
    for (int i = 0; i < 48000 * 60; i++) g.process(0.0);
    assert(g.octaves() == 2.0);  // snapped, never a denormal tail
  }

  // ---- no clicks across toggle moves (Review Focus 4): 0 -> +1 -> 0 ->
  // +1 -> -1 -> 0 at glide 0 on a 220 Hz sine. A legitimate step here is
  // under 0.03; a discontinuity would be an order of magnitude bigger.
  {
    ShiftStage st(kSr);
    double prev = 0, worst = 0;
    size_t i = 0;
    auto seg = [&](double tgt, double secs) {
      st.set_target(tgt, 0.0);
      for (size_t k = 0; k < secs * kSr; k++, i++) {
        const double y = st.process(0.5 * std::sin(2 * M_PI * 220 * i / kSr));
        if (i) worst = std::max(worst, std::fabs(y - prev));
        prev = y;
      }
    };
    seg(0, 0.2); seg(1, 0.5); seg(0, 0.5); seg(1, 0.3); seg(-1, 0.5); seg(0, 0.5);
    assert(worst < 0.1);
    assert(st.wet() == 0.0 && st.octaves() == 0.0);  // back to the dry wire
  }

  // ---- engage reset (Review Focus 2): the shift arrives at once, no
  // glide from 0, no old audio
  {
    ShiftStage st(kSr);
    st.set_target(1.0, 2000.0);
    for (int i = 0; i < 4800; i++) st.process(0.5);
    st.reset();
    assert(st.octaves() == 1.0 && st.wet() == 1.0);
    for (int i = 0; i < 4800; i++) assert(st.process(0.0) == 0.0);
    st.set_target(0.0, 0.0);
    st.reset();
    assert(st.octaves() == 0.0 && st.wet() == 0.0);
  }

  std::printf("test_pitch_shifter OK\n");
  return 0;
}
