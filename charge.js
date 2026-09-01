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
  // Gain (post chain) drives both halves of the loudness: drive toward 1 and
  // vocal volume toward its 2.0 ceiling.
  if (c.gain !== 0) {
    const reach = c.gain === 2 ? 1.0 : 0.5;
    v.drive += (1.0 - v.drive) * reach * level;
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

  // Aspiration: breath ramped toward a full 1.0, same reach rule as gain.
  // A voice sitting at 0 breath still gets it, which is the point: the
  // power-up ramp is where the scream tears.
  //
  // Aspiration is the only thing charge moves that is GRAIN-AFFECTING: any
  // change to it re-dirties the grain tables, and a rebuild is 8 voices x
  // grainLen x 3 formants. A continuous ramp would rebuild every block for
  // the whole charge, so the ramp is stepped: about 33 rebuilds across a
  // full charge instead of hundreds, and a step that small is inaudible in
  // a breath texture. level 0 is still a bit-exact identity.
  if (c.aspir !== 0) {
    const reach = c.aspir === 2 ? 1.0 : 0.5;
    // Quantise the LEVEL, not the result: level 1 stays exactly 1 so a full
    // charge still lands exactly on the top of the range.
    const q = Math.round(level * 32.0) / 32.0;
    v.aspiration += (1.0 - v.aspiration) * reach * q;
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
  aspir: ['off', 'low', 'high'],
};
