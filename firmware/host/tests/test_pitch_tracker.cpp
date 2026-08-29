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

int main() {
  // 1) synthetic guitar-ish tone: 110 Hz + harmonics, 1 s
  {
    PitchTracker pt(48000.0f);
    for (int i = 0; i < 48000; i++) {
      double t = i / 48000.0;
      float x = 0.4f * (std::sin(2 * M_PI * 110 * t) + 0.5 * std::sin(2 * M_PI * 220 * t)
                        + 0.3 * std::sin(2 * M_PI * 330 * t));
      pt.process(x);
    }
    double err_cents = 1200.0 * std::fabs(std::log2(pt.f0() / 110.0));
    std::printf("sine lock: %.2f Hz (%.1f cents)\n", pt.f0(), err_cents);
    assert(err_cents < 20.0);
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
