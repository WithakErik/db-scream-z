// param_map.hpp - knob position (0..1) to parameter mapping for the three
// menu layers (spec section 2). Ranges are the lab UI's CONTROLS
// (dbscreamz_lab/static/app.js lines 515-547, authoritative per spec); the
// post-chain knob ranges are recorded in the milestone 5 plan.
#pragma once
#include <cmath>

#include "voice_params.hpp"

enum class MenuLayer : unsigned char { Menu1 = 0, Menu2 = 1, Menu3 = 2 };

inline float map_lin(float t, float lo, float hi) { return lo + (hi - lo) * t; }

// Cubic taper. Glide (spec: "0-100 ms takes most of the travel") over
// 0..300 ms puts 100 ms at ~69% of the knob's travel. Vibrato rate reuses
// it over 0..50 Hz, which puts 6.25 Hz at 12 o'clock and keeps the fast
// warble end from swallowing the musical range.
inline float map_cube(float t, float lo, float hi) {
  return lo + (hi - lo) * t * t * t;
}

// Tone: -1..+1 with a center detent band that maps to exactly 0. The spec
// requires the center to be bit-transparent and a real pot never reads
// exactly 0.5, so +/-10% around center pins tone to 0; the remaining travel
// rescales to the full -1..+1.
inline float map_tone(float t) {
  float x = (t - 0.5f) * 2.0f;
  const float dz = 0.1f;
  if (std::fabs(x) < dz) return 0.0f;
  const float s = x > 0.0f ? 1.0f : -1.0f;
  return s * (std::fabs(x) - dz) / (1.0f - dz);
}

// Vocal size: 0..1, quantised to 32 steps. formant_scale is grain-affecting
// (grain.hpp bakes it in, fof_engine.hpp dirty-checks it), so an unquantised
// knob would rebuild the grain tables on every audio block during a sweep
// and starve the main loop. 32 steps over the 0.5 scale range is a step of
// about 11 Hz at Piccolo's F1: inaudible. Both stops land exactly on the
// grid, so 0 and 1 stay exactly reachable.
inline float map_vocal_size(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  return std::floor(t * 32.0f + 0.5f) / 32.0f;
}

// Writes knob k (0..5, physical left-to-right, top row then bottom row) at
// position t (0..1) of the given layer into vp.
inline void apply_knob(MenuLayer layer, int k, float t, VoiceParams& vp) {
  switch (layer) {
    case MenuLayer::Menu1:
      switch (k) {
        case 0: vp.vocal_vol  = map_lin(t, 0.0f, 2.0f); break;
        case 1: vp.mix        = map_lin(t, 0.0f, 1.0f); break;
        case 2: vp.master_vol = map_lin(t, 0.0f, 2.0f); break;
        case 3: vp.tone       = map_tone(t); break;
        case 4: vp.glide_ms   = map_cube(t, 0.0f, 300.0f); break;
        case 5: vp.vocal_size = map_vocal_size(t); break;
      }
      break;
    case MenuLayer::Menu2:
      switch (k) {
        case 0: vp.f1  = map_lin(t, 200.0f, 1400.0f); break;
        case 1: vp.bw1 = map_lin(t, 5.0f, 300.0f); break;
        case 2: vp.a1  = map_lin(t, 0.0f, 2.0f); break;
        case 3: vp.f2  = map_lin(t, 500.0f, 2600.0f); break;
        case 4: vp.bw2 = map_lin(t, 5.0f, 400.0f); break;
        case 5: vp.a2  = map_lin(t, 0.0f, 2.0f); break;
      }
      break;
    case MenuLayer::Menu3:
      switch (k) {
        case 0: vp.f3  = map_lin(t, 1500.0f, 4500.0f); break;
        case 1: vp.bw3 = map_lin(t, 5.0f, 500.0f); break;
        case 2: vp.a3  = map_lin(t, 0.0f, 2.0f); break;
        // Vibrato returned to the pedal 2026-09-02, filling the slot the
        // retired voices control left. Rate is cubic so 6.25 Hz lands at
        // 12 o'clock; past ~15 Hz it stops reading as vibrato and becomes
        // an FM warble, which is wanted and is why the range runs to 50.
        case 3: vp.vib_rate_hz     = map_cube(t, 0.0f, 50.0f); break;
        case 4: vp.vib_depth_cents = map_lin(t, 0.0f, 100.0f); break;
        // Detune moved down from knob 5 when grain length was retired.
        case 5: vp.detune_cents    = map_lin(t, 0.0f, 60.0f); break;
      }
      break;
  }
}
