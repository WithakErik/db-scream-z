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

  // Every row here ramps its parameter toward the TOP of its own range,
  // measured from wherever the voice already sits: charge is a power-up,
  // so a full charge means full value, not a fixed increment on top of
  // whatever you dialled. `reach` is how far up that gap the row travels
  // at full charge: Up goes the whole way, Middle stops halfway.
  //
  // Gain (post chain) drives both halves of the loudness: drive toward 1
  // and vocal volume toward its 2.0 ceiling.
  if (c.gain != 0) {
    const float reach = c.gain == 2 ? 1.0f : 0.5f;
    v.drive += (1.0f - v.drive) * reach * level;
    v.vocal_vol += (2.0f - v.vocal_vol) * reach * level;
  }

  // Pitch: a TWO octave sweep, run continuously by the engine's portamento
  // smoother (fof_engine.hpp cur_f0_) via a long glide override. The result
  // is capped at +/-2 rather than the sweep being cancelled, so pitch always
  // does something: a voice whose own octave toggle is already at +1 simply
  // has one octave of travel left instead of two.
  if (c.pitch != 0) {
    int target = base.octave + (c.pitch == 2 ? 2 : -2);
    if (target > 2) target = 2;
    if (target < -2) target = -2;
    if (charging) {
      v.octave = static_cast<int8_t>(target);
      v.glide_ms = 2000.0f;  // the sweep: rise/fall, never a jump
    } else {
      v.octave = base.octave;  // decay: glide back home
      v.glide_ms = static_cast<float>(kDecayTimesMs[c.decay]);
    }
  }

  // Tone: ramp toward full bright (+1) or full dark (-1).
  if (c.tone != 0) {
    const float target = c.tone == 2 ? 1.0f : -1.0f;
    v.tone += (target - v.tone) * level;
  }

  // Aspiration: breath ramped toward a full 1.0, same reach rule as gain.
  // A voice sitting at 0 breath still gets it, which is the point: the
  // power-up ramp is where the scream tears.
  //
  // Aspiration is the only thing charge moves that is GRAIN-AFFECTING: any
  // change to it re-dirties the grain tables (fof_engine.hpp set_params),
  // and a rebuild is 8 voices x grain_len x 3 formants on the main loop. A
  // continuous ramp would therefore rebuild on every 5 ms main-loop pass for
  // the whole charge, so the ramp is stepped: about 33 rebuilds across a
  // full charge instead of hundreds, and a step that small is inaudible in
  // a breath texture. level 0 is still a bit-exact identity.
  if (c.aspir != 0) {
    const float reach = c.aspir == 2 ? 1.0f : 0.5f;
    // Quantise the LEVEL, not the result: 33 distinct steps up the ramp
    // keeps the rebuild count down, and level 1 stays exactly 1 so a full
    // charge still lands exactly on the top of the range.
    const float q = std::floor(level * 32.0f + 0.5f) / 32.0f;
    v.aspiration += (1.0f - v.aspiration) * reach * q;
  }
  return v;
}
