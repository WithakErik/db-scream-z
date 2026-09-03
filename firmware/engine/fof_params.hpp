// fof_params.hpp
#pragma once
#include <cmath>

// Overridable at compile time exactly like DBSCREAMZ_MAX_GRAIN_LEN below
// (firmware/hothouse/Makefile passes -DDBSCREAMZ_MAX_UNISON=3, which trims
// the 2 x kMaxUnison x kMaxGrainLen grain tables to fit the target's RAM
// budget).
//
// The HOST default stays 8 on purpose. 8 is the frozen JS engine's
// MAX_UNISON, and fof_engine.hpp:170 clamps the requested stack to this
// constant, so lowering it here would silently make host/render.cpp
// disagree with tools/ref_render.js for any --set unison above the new
// cap and invalidate the golden reference renders. The firmware carries
// the smaller table because its own pin (main.cpp to_fof_params) can
// never ask for more; the static_assert there is what ties the two
// together.
#ifndef DBSCREAMZ_MAX_UNISON
#define DBSCREAMZ_MAX_UNISON 8     // host default, matches JS MAX_UNISON
#endif
inline constexpr int kMaxUnison = DBSCREAMZ_MAX_UNISON;

// Overridable at compile time (e.g. -DDBSCREAMZ_MAX_GRAIN_LEN=960 on firmware,
// which trims the 8 x kMaxGrainLen grain tables to fit the target's RAM
// budget). Host builds keep the 4800 (100 ms @ 48k) headroom default.
#ifndef DBSCREAMZ_MAX_GRAIN_LEN
#define DBSCREAMZ_MAX_GRAIN_LEN 4800   // host default, 100 ms @ 48k headroom
#endif
inline constexpr int kMaxGrainLen = DBSCREAMZ_MAX_GRAIN_LEN;

// obuf_ capacity: JS obufLen = ceil(sr * 0.2); 0.2 s at 48 kHz headroom.
// The constructor path asserts ceil(sr*0.2) <= kObufMax.
inline constexpr int kObufMax = 9600;

// JS Math.round is round-half-up (toward +infinity), not std::lround's
// round-half-away-from-zero / banker's rounding. Use this for every
// transcribed Math.round call.
inline double jsRound(double x) { return std::floor(x + 0.5); }

struct FofParams {           // live-path mirror of fof-processor.js this.p
  double f1 = 858, f2 = 1234, f3 = 3112;
  double bw1 = 32.5, bw2 = 47.5, bw3 = 62.5;
  double a1 = 1.0, a2 = 1.0, a3 = 1.0;
  double formant_scale = 1.0;
  double grain_ms = 20;
  int    unison = 3;
  double detune_cents = 11;
  // Vibrato. Restored 2026-09-02 after ec97ed6 deleted it as dead code;
  // it now has knobs (menu 3, 4 and 5). Both default to 0 so no voice
  // changes until one is turned. Depth is in SEMITONES, matching the JS
  // this file mirrors; the knob and the store work in cents and
  // to_fof_params() does the one conversion.
  double vib_rate = 0.0;    // Hz, 0 = off (the phase parks)
  double vib_depth = 0.0;   // semitones
  double aspiration = 0.0;
  bool   quantize = true;
  bool   amp_comp = true;
  double glide_ms = 0;
  double target_f0 = 452;
  int    octave_shift = 0;
  bool   follow_pitch = true;
  bool   taper_end = true;
  bool   leveler = true;
  double input_gain = 4.0;
  double gate = 0.02;
  double gain = 1.0;
};
