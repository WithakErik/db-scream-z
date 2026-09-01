// charge.js - port of firmware/hothouse/charge.hpp.
// Charge mode (power-up boost) modulation overlay. A pure function of
// (edit buffer, config, level, charging): the edit buffer is NEVER written,
// the caller feeds the returned copy to the engine and post chain.

import { kDecayTimesMs, cloneVoice } from './voice-params.js';

export function applyCharge(base, c, level, charging) {
  const v = cloneVoice(base);
  if (level <= 0.0) return v;

  // Gain (post chain): on = x1.5 vocal + 0.3 drive at full charge; the top
  // setting pins drive toward 1 and doubles vocal.
  if (c.gain === 1) {
    v.vocal_vol *= 1.0 + 0.5 * level;
    v.drive += 0.3 * level;
  } else if (c.gain === 2) {
    v.drive += (1.0 - v.drive) * level;
    v.vocal_vol *= 1.0 + 1.0 * level;
  }
  if (v.vocal_vol > 2.0) v.vocal_vol = 2.0;
  if (v.drive > 1.0) v.drive = 1.0;

  // Pitch: an octave step swept continuously by the engine's portamento
  // smoother via a long glide override. Saturates at +/-1.
  if (c.pitch !== 0) {
    const target = base.octave + (c.pitch === 2 ? 1 : -1);
    if (target >= -1 && target <= 1) {
      if (charging) {
        v.octave = target;
        v.glide_ms = 2000.0;   // the sweep: rise/fall, never a jump
      } else {
        v.octave = base.octave;   // decay: glide back home
        v.glide_ms = kDecayTimesMs[c.decay];
      }
    }
  }

  // Tone: ramp toward full bright (+1) or full dark (-1).
  if (c.tone !== 0) {
    const target = c.tone === 2 ? 1.0 : -1.0;
    v.tone += (target - v.tone) * level;
  }

  // Aspiration: added breath, clamped to the menu 3 knob range. A voice
  // sitting at 0 breath still gets it, which is the point: the power-up
  // ramp is where the scream tears.
  //
  // Aspiration is the only thing charge moves that is GRAIN-AFFECTING: any
  // change to it re-dirties the grain tables, and a rebuild is 8 voices x
  // grainLen x 3 formants. A continuous ramp would rebuild every block for
  // the whole charge, so the ADDED amount is quantised to 1/32 of the
  // range: about 20 rebuilds across a full charge instead of hundreds, and
  // a step that small is inaudible in a breath texture. The base value is
  // left exact so level 0 stays a bit-exact identity. Both amounts are
  // exact multiples of 1/32, so full charge lands on its nominal value.
  if (c.aspir !== 0) {
    const add = (c.aspir === 2 ? 0.625 : 0.25) * level;
    v.aspiration += Math.round(add * 32.0) / 32.0;
    if (v.aspiration > 1.0) v.aspiration = 1.0;
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
