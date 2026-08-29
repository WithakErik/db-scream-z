// charge.hpp - charge mode (power-up boost) modulation overlay.
// Spec: docs/superpowers/specs/2026-08-27-charge-mode-design.md
// sections 3 and 5. Pure function of (edit buffer, config, level,
// charging): the edit buffer is NEVER written; main.cpp feeds the
// returned copy to the engine and post chain each block. All the
// numeric targets are placeholders to ear-tune on hardware.
#pragma once
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

  // Vibrato: added depth/rate, clamped to the menu 3 knob ranges.
  if (c.vib != 0) {
    v.vib_depth += (c.vib == 2 ? 1.5f : 0.5f) * level;
    v.vib_rate += (c.vib == 2 ? 3.0f : 1.0f) * level;
    if (v.vib_depth > 4.0f) v.vib_depth = 4.0f;
    if (v.vib_rate > 14.0f) v.vib_rate = 14.0f;
  }
  return v;
}
