#include "pitch_tracker.hpp"
#include "../wav_io.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

// The BACF word-size guard (FIRMWARE.md section 9 gotcha) now lives as a
// static_assert in firmware/engine/pitch_tracker.hpp, included above - it
// is TU-wide there (covers this test, render.cpp, and any other consumer),
// so it is not duplicated here.

namespace {

double cents(double a, double b) { return 1200.0 * std::log2(b / a); }

// OctaveGate is pure logic over one reading per analysis window, so these
// cases feed it readings directly: (f, periodicity) is what PitchTracker
// hands it each time the detector finishes a window.
void test_gate() {
  constexpr float kSure = 0.95f;  // comfortably above the threshold
  constexpr float kUnsure = 0.5f;

  // a) the first confident reading is taken at once, whatever the
  //    placeholder was, even an octave away from it (200 -> 100)
  {
    OctaveGate g;
    assert(g.f0() == 200.0);
    g.window(100.0f, kSure);
    assert(g.f0() == 100.0);
  }
  // b) an unconfident reading never moves f0, however plausible
  {
    OctaveGate g;
    g.window(110.0f, kSure);
    g.window(147.0f, kUnsure);
    assert(g.f0() == 110.0);
    // nor does a zero (detector not locked yet)
    g.window(0.0f, kSure);
    assert(g.f0() == 110.0);
    // and before any lock, an unconfident reading is not a lock either
    OctaveGate fresh;
    fresh.window(110.0f, kUnsure);
    assert(fresh.f0() == 200.0);
  }
  // c) a non-octave jump is a note change: taken immediately
  {
    OctaveGate g;
    g.window(110.0f, kSure);
    g.window(165.0f, kSure);  // a fifth up
    assert(g.f0() == 165.0);
    g.window(110.0f, kSure);  // a fifth down
    assert(g.f0() == 110.0);
    // an octave plus a semitone is outside the 50-cent octave band
    const double f = 110.0 * std::pow(2.0, 13.0 / 12.0);
    g.window(static_cast<float>(f), kSure);
    assert(std::fabs(cents(f, g.f0())) < 0.01);
  }
  // d) an octave jump must hold for kOctaveHoldWindows consecutive windows
  {
    OctaveGate g;
    g.window(110.0f, kSure);
    for (int i = 1; i < OctaveGate::kOctaveHoldWindows; i++) {
      g.window(220.0f, kSure);
      assert(g.f0() == 110.0);
    }
    g.window(220.0f, kSure);
    assert(g.f0() == 220.0);
    // and downwards
    for (int i = 1; i < OctaveGate::kOctaveHoldWindows; i++) {
      g.window(110.0f, kSure);
      assert(g.f0() == 220.0);
    }
    g.window(110.0f, kSure);
    assert(g.f0() == 110.0);
  }
  // e) a blip that comes back is never taken; the hold count restarts on
  //    any window that does not repeat the candidate, unconfident included
  {
    OctaveGate g;
    g.window(110.0f, kSure);
    g.window(220.0f, kSure);
    g.window(110.0f, kSure);  // back: the blip is gone
    g.window(220.0f, kSure);
    g.window(220.0f, kUnsure);  // breaks the run
    g.window(220.0f, kSure);
    assert(g.f0() == 110.0);
    // a slightly detuned repeat of the candidate still counts (bends)
    OctaveGate h;
    h.window(110.0f, kSure);
    h.window(220.0f, kSure);
    h.window(221.0f, kSure);
    h.window(222.0f, kSure);
    assert(h.f0() == 222.0);
  }
  // f) after unvoiced (silence), the first confident reading is taken at
  //    once even when it is exactly an octave away
  {
    OctaveGate g;
    g.window(110.0f, kSure);
    g.unvoiced();
    assert(g.f0() == 110.0);  // unvoiced holds the last f0
    g.window(220.0f, kSure);
    assert(g.f0() == 220.0);
    // and it is ONE reading: the next octave jump is gated again
    g.window(110.0f, kSure);
    assert(g.f0() == 220.0);
  }
  std::puts("gate OK");
}

// Synthetic guitar-ish tone: fundamental + 2 harmonics.
float tone(double f, double t) {
  return 0.4f * static_cast<float>(std::sin(2 * M_PI * f * t) + 0.5 * std::sin(2 * M_PI * 2 * f * t) +
                                   0.3 * std::sin(2 * M_PI * 3 * f * t));
}

}  // namespace

int main() {
  test_gate();

  // 1) synthetic guitar-ish tone: 110 Hz + harmonics, 1 s
  {
    PitchTracker pt(48000.0f);
    for (int i = 0; i < 48000; i++) pt.process(tone(110, i / 48000.0));
    double err_cents = 1200.0 * std::fabs(std::log2(pt.f0() / 110.0));
    std::printf("sine lock: %.2f Hz (%.1f cents)\n", pt.f0(), err_cents);
    assert(err_cents < 20.0);
  }
  // 1b) A2, silence, then A3: the second note must read as A3. Before the
  //     tracker reset the detector on silence, cycfi::q::pitch_detector
  //     kept its old frequency through the gap and its harmonic snap
  //     (bias(): incoming / round(incoming / current)) folded the new note
  //     back onto it, so this read 110 Hz for as long as A3 rang.
  {
    PitchTracker pt(48000.0f);
    for (int i = 0; i < 24000; i++) pt.process(tone(110, i / 48000.0));
    assert(std::fabs(cents(110.0, pt.f0())) < 20.0);
    for (int i = 0; i < 24000; i++) pt.process(0.0f);
    assert(std::fabs(cents(110.0, pt.f0())) < 20.0);  // held through silence
    for (int i = 0; i < 24000; i++) pt.process(tone(220, i / 48000.0));
    std::printf("A2 / silence / A3: %.2f Hz\n", pt.f0());
    assert(std::fabs(cents(220.0, pt.f0())) < 20.0);
  }
  // 2) real DI: octave-jump rate on guitar_long at inputGain 4, f0 per 128 block
  {
    WavData g = wav_read_mono("../../dbscreamz_lab/static/audio/guitar_long.wav");
    PitchTracker pt(48000.0f);
    std::vector<double> trace;
    for (size_t i = 0; i < g.samples.size(); i++) {
      pt.process((float)(g.samples[i] * 4.0));
      if (i % 128 == 127) trace.push_back(pt.f0());
    }
    int jumps = 0;
    for (size_t i = 1; i < trace.size(); i++)
      if (std::fabs(std::log2(trace[i] / trace[i - 1])) > 0.45) jumps++;
    double rate = jumps / 35.0;
    std::printf("octave jump rate %.2f/s\n", rate);
    // block-pair metric counts melodic leaps too; JS reference = 1.0857/s
    // on this DI, C++ delta 0.20/s, parity gated at 0.4/s in the render
    // comparison.
    assert(rate < 1.5);
  }
  std::puts("test_pitch_tracker OK");
  return 0;
}
