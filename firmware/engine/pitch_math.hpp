#pragma once
#include "fof_params.hpp"
#include <cmath>

// fof-processor.js lines 869-881. 32 steps/semitone on the TRUE octave
// (FIRMWARE.md section 4: no MonkSynth -12 offset).
//
// Split into bin index and bin frequency so fof_engine.hpp can memoise on
// the BIN: the output depends only on idx, so a memo keyed on the bin (with
// the Hz interval that maps to it) survives the tiny per-window f0 motion
// of a real note, where a memo keyed on the exact hz misses every sample.
// quantize_hz is exactly the composition of the two, so a cached
// quantize_bin_hz(idx) is bit-identical to calling quantize_hz.
inline double quantize_bin(double hz) {
  double note = 12.0 * std::log2(hz / 440.0) + 69.0;
  return jsRound(note * 32.0);
}
inline double quantize_bin_hz(double idx) {
  return 440.0 * std::pow(2.0, (idx / 32.0 - 69.0) / 12.0);
}
inline double quantize_hz(double hz, bool enabled) {
  if (!enabled) return hz;
  return quantize_bin_hz(quantize_bin(hz));
}

// fof-processor.js lines 883-908, registerMode 0 only (snap mode is
// deprecated dead code). Characters NEVER change pitch (FIRMWARE.md sec 4).
//
// Split so the engine can hoist the pow out of its per-sample loop:
// octave_shift is fixed for a whole block, so the multiplier is computed
// once per block and passed in. pow(2, n) is exact for integer n, so
// f0 * octave_multiplier(p) is bit-identical to the JS f0 * 2 ** octave.
inline double octave_multiplier(const FofParams& p) {
  return std::pow(2.0, (double)p.octave_shift);
}
inline double register_map(double f0, const FofParams& p, double oct_mul) {
  if (!p.follow_pitch || f0 < 40.0) return p.target_f0;
  return f0 * oct_mul;
}
inline double register_map(double f0, const FofParams& p) {
  return register_map(f0, p, octave_multiplier(p));
}

// monk_voice_amplitude comp, fof-processor.js lines 1023-1027.
inline double amp_comp_gain(double f) {
  double note = 12.0 * std::log2(f / 440.0) + 69.0;
  return std::min(3.0, std::max(0.1, (note - 12.0) * (-1.0 / 72.0) + 2.0));
}
