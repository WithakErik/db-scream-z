#pragma once
#include "fof_params.hpp"
#include <cmath>

// fof-processor.js lines 869-881. 32 steps/semitone on the TRUE octave
// (FIRMWARE.md section 4: no MonkSynth -12 offset).
inline double quantize_hz(double hz, bool enabled) {
  if (!enabled) return hz;
  double note = 12.0 * std::log2(hz / 440.0) + 69.0;
  double idx = jsRound(note * 32.0);
  return 440.0 * std::pow(2.0, (idx / 32.0 - 69.0) / 12.0);
}

// fof-processor.js lines 883-908, registerMode 0 only (snap mode is
// deprecated dead code). Characters NEVER change pitch (FIRMWARE.md sec 4).
inline double register_map(double f0, const FofParams& p) {
  if (!p.follow_pitch || f0 < 40.0) return p.target_f0;
  return f0 * std::pow(2.0, (double)p.octave_shift);
}

// monk_voice_amplitude comp, fof-processor.js lines 1023-1027.
inline double amp_comp_gain(double f) {
  double note = 12.0 * std::log2(f / 440.0) + 69.0;
  return std::min(3.0, std::max(0.1, (note - 12.0) * (-1.0 / 72.0) + 2.0));
}
