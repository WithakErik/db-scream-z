// charge.hpp - charge mode (power-up boost) modulation overlay.
// Spec: docs/superpowers/specs/2026-08-27-charge-mode-design.md
// sections 3 and 5. Pure function of (edit buffer, config, level,
// charging): the edit buffer is NEVER written; main.cpp feeds the
// returned copy to the engine and post chain each block. All the
// numeric targets are placeholders to ear-tune on hardware.
#pragma once
#include <cmath>

#include "voice_params.hpp"

inline VoiceParams apply_charge(const VoiceParams& base,
                                const ChargeConfig& c, float level,
                                bool charging) {
  VoiceParams v = base;
  if (level <= 0.0f) return v;

  // Gain (post chain): on = x1.5 vocal + 0.3 drive at full charge;
  // Above 9000! = drive pinned toward 1 and vocal x2.
  if (c.gain == 1) {
    v.vocal_vol *= 1.0f + 0.5f * level;
    v.drive += 0.3f * level;
  } else if (c.gain == 2) {
    v.drive += (1.0f - v.drive) * level;
    v.vocal_vol *= 1.0f + 1.0f * level;
  }
  if (v.vocal_vol > 2.0f) v.vocal_vol = 2.0f;
  if (v.drive > 1.0f) v.drive = 1.0f;

  // Pitch: an octave step swept continuously by the engine's portamento
  // smoother (fof_engine.hpp cur_f0_) via a long glide override.
  // Saturates at +/-1: a +/-1 voice charging further adds no pitch.
  if (c.pitch != 0) {
    const int target = base.octave + (c.pitch == 2 ? 1 : -1);
    if (target >= -1 && target <= 1) {
      if (charging) {
        v.octave = static_cast<int8_t>(target);
        v.glide_ms = 2000.0f;  // the sweep: rise/fall, never a jump
      } else {
        v.octave = base.octave;  // decay: glide back home
        v.glide_ms = static_cast<float>(kDecayTimesMs[c.decay]);
      }
    }
  }

  // Tone: ramp toward full bright (+1) or full dark (-1).
  if (c.tone != 0) {
    const float target = c.tone == 2 ? 1.0f : -1.0f;
    v.tone += (target - v.tone) * level;
  }

  // Aspiration: added breath, clamped to the menu 3 knob range. A voice
  // sitting at 0 breath still gets it, which is the point: the power-up
  // ramp is where the scream tears.
  //
  // Aspiration is the only thing charge moves that is GRAIN-AFFECTING: any
  // change to it re-dirties the grain tables (fof_engine.hpp set_params),
  // and a rebuild is 8 voices x grain_len x 3 formants on the main loop.
  // A continuous ramp would therefore rebuild on every 5 ms main-loop pass
  // for the whole charge, so the ADDED amount is quantised to 1/32 of the
  // range: about 20 rebuilds across a full charge instead of hundreds, and
  // a step that small is inaudible in a breath texture. The base value is
  // left exact so level 0 stays a bit-exact identity. Both amounts are
  // exact multiples of 1/32, so full charge lands on its nominal value.
  if (c.aspir != 0) {
    const float add = (c.aspir == 2 ? 0.625f : 0.25f) * level;
    v.aspiration += std::floor(add * 32.0f + 0.5f) / 32.0f;
    if (v.aspiration > 1.0f) v.aspiration = 1.0f;
  }
  return v;
}
