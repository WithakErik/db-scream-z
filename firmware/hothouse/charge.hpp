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
  // Gain: the ramp drives vocal volume toward its 2.0 ceiling. It used to
  // push drive as well, but the pedal has no drive any more (vocal size
  // design spec section 6), so loudness is the whole of this row now.
  if (c.gain != 0) {
    const float reach = c.gain == 2 ? 1.0f : 0.5f;
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

  // Size: the ramp grows the voice itself, scaling the whole vocal tract
  // toward its deepest. Same reach rule as gain: Up travels the whole gap,
  // Middle half of it. The scream physically grows as it charges.
  //
  // UNLIKE every other row, this one IS grain-affecting: main.cpp turns
  // vocal_size into FofParams::formant_scale, which grain.hpp bakes into
  // the formants and fof_engine.hpp dirty-checks. An unquantised ramp would
  // therefore rebuild the grain tables on every 5 ms main loop pass for the
  // whole charge. Quantising the LEVEL to 32 steps holds that to about 33
  // rebuilds per charge, exactly as the aspiration row used to. Audio is
  // never at risk either way (rebuilds are double buffered and published by
  // an active_set_ flip), but an unbounded rebuild rate starves the main
  // loop and the LEDs visibly stutter.
  if (c.size != 0) {
    const float reach = c.size == 2 ? 1.0f : 0.5f;
    const float q = std::floor(level * 32.0f + 0.5f) / 32.0f;
    v.vocal_size += (1.0f - v.vocal_size) * reach * q;
  }
  return v;
}
