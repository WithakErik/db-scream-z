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

  // Vibrato: added depth/rate, clamped to the menu 3 knob ranges.
  if (c.vib !== 0) {
    v.vib_depth += (c.vib === 2 ? 1.5 : 0.5) * level;
    v.vib_rate += (c.vib === 2 ? 3.0 : 1.0) * level;
    if (v.vib_depth > 4.0) v.vib_depth = 4.0;
    if (v.vib_rate > 14.0) v.vib_rate = 14.0;
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
  vib:   ['off', 'low', 'high'],
};
