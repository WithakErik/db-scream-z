// charge.js - port of firmware/hothouse/charge.hpp.
// Charge mode (power-up boost) modulation overlay. A pure function of
// (edit buffer, config, level, charging): the edit buffer is NEVER written,
// the caller feeds the returned copy to the engine and post chain.

import { kDecayTimesMs, cloneVoice } from './voice-params.js';

export function applyCharge(base, c, level, charging) {
  const v = cloneVoice(base);
  if (level <= 0.0) return v;

  // Every row here ramps its parameter toward the TOP of its own range,
  // measured from wherever the voice already sits: charge is a power-up, so
  // a full charge means full value, not a fixed increment on top of whatever
  // you dialled. `reach` is how far up that gap the row travels at full
  // charge: up goes the whole way, middle stops halfway.
  //
  // Gain: the ramp drives vocal volume toward its 2.0 ceiling. It used to
  // push drive as well, but the pedal has no drive any more (vocal size
  // design spec section 6), so loudness is the whole of this row now.
  if (c.gain !== 0) {
    const reach = c.gain === 2 ? 1.0 : 0.5;
    v.vocal_vol += (2.0 - v.vocal_vol) * reach * level;
  }

  // Pitch: a TWO octave sweep, run continuously by the engine's portamento
  // smoother via a long glide override. The result is capped at +/-2 rather
  // than the sweep being cancelled, so pitch always does something: a voice
  // whose own octave toggle is already at +1 simply has one octave of travel
  // left instead of two.
  if (c.pitch !== 0) {
    let target = base.octave + (c.pitch === 2 ? 2 : -2);
    if (target > 2) target = 2;
    if (target < -2) target = -2;
    if (charging) {
      v.octave = target;
      v.glide_ms = 2000.0;   // the sweep: rise/fall, never a jump
    } else {
      v.octave = base.octave;   // decay: glide back home
      v.glide_ms = kDecayTimesMs[c.decay];
    }
  }

  // Tone: ramp toward full bright (+1) or full dark (-1).
  if (c.tone !== 0) {
    const target = c.tone === 2 ? 1.0 : -1.0;
    v.tone += (target - v.tone) * level;
  }

  // Size: the ramp grows the voice itself, scaling the whole vocal tract
  // toward its deepest. Same reach rule as gain: Up travels the whole gap,
  // Middle half of it. The scream physically grows as it charges.
  //
  // UNLIKE every other row, this one IS grain-affecting: audio.js turns
  // vocal_size into formantScale, which fof-processor.js bakes into the
  // formants and dirty-checks. An unquantised ramp would therefore rebuild
  // the grain tables far more often than needed. Quantising the LEVEL to 32
  // steps holds that down, exactly as the aspiration row used to.
  if (c.size !== 0) {
    const reach = c.size === 2 ? 1.0 : 0.5;
    const q = Math.floor(level * 32.0 + 0.5) / 32.0;
    v.vocal_size += (1.0 - v.vocal_size) * reach * q;
  }
  return v;
}

// Captions for the charge config, matching docs/BOOKLET.md. Index is the
// uniform toggle mapping: Down = 0, Middle = 1, Up = 2.
export const kChargeLabels = {
  gain:  ['off', 'on', 'max'],
  time:  ['~0.75 s', '~2.5 s', '~6 s'],
  decay: ['instant', 'slow', 'fast'],
  pitch: ['off', 'fall', 'rise'],
  tone:  ['off', 'darker', 'brighter'],
  size:  ['off', 'half', 'full'],
};
