// chord-map.js - port of firmware/hothouse/chord_map.hpp.
// Chord mode's knob layout, the ChordParams -> ChordEngineParams bridge,
// and its charge overlay (chord mode design spec section 3).

import { mapLin, mapCube, mapTone, mapVocalSize } from './param-map.js';
import { applyCharge } from './charge.js';
import { kGateLevels, formantScaleFrom } from './voice-params.js';

// Chord mode has two layers: menu 1 (normal play) and the chord menu (left
// hold). There is no menu 3.
export const ChordLayer = { Main: 0, Menu: 1 };

// Drive pre-gain, 1x..40x on a log taper, so the middle of the knob is
// about 6.3x rather than 20x.
export function mapDrive(t) { return Math.pow(40.0, t); }

// Resonance 0..1 to the bandwidth multiplier, 2.0 (soft) .. 0.35 (sharp)
// on a log taper.
export function mapBwScale(resonance) {
  return 2.0 * Math.pow(0.35 / 2.0, resonance);
}

// Knob k (0..5, same physical order as applyKnob) at position t (0..1).
// Menu 1 knobs 1-4 deliberately reuse normal mode's maps so the level
// knobs feel identical in both modes.
export function applyChordKnob(layer, k, t, c) {
  if (layer === ChordLayer.Main) {
    switch (k) {
      case 0: c.vocal_vol   = mapLin(t, 0.0, 2.0); break;
      case 1: c.mix         = mapLin(t, 0.0, 1.0); break;
      case 2: c.master_vol  = mapLin(t, 0.0, 2.0); break;
      case 3: c.tone        = mapTone(t); break;
      case 4: c.sensitivity = mapCube(t, 0.0, 8.0); break;
      case 5: c.drive       = mapDrive(t); break;
    }
    return;
  }
  switch (k) {
    case 0: c.closed_vowel = mapLin(t, 0.0, 4.0); break;
    case 1: c.open_vowel   = mapLin(t, 0.0, 4.0); break;
    case 2: c.vocal_size   = mapVocalSize(t); break;
    case 3: c.resonance    = t; break;
    case 4: c.attack_ms    = mapCube(t, 1.0, 50.0); break;
    case 5: c.release_ms   = mapCube(t, 20.0, 500.0); break;
  }
}

// The inverse, so the on-screen knobs can be drawn at the position that
// produces the loaded chord setting, exactly as knobPositions() does for
// normal mode.
export function chordKnobPositions(layer, c) {
  const inv = (v, lo, hi) => Math.min(1, Math.max(0, (v - lo) / (hi - lo)));
  const invCube = (v, lo, hi) => Math.cbrt(inv(v, lo, hi));
  const invTone = (v) => {
    const dz = 0.1;
    if (v === 0) return 0.5;
    const s = v > 0 ? 1 : -1;
    return 0.5 + s * (Math.abs(v) * (1 - dz) + dz) / 2;
  };
  const invDrive = (v) => Math.min(1, Math.max(0, Math.log(v) / Math.log(40.0)));
  if (layer === ChordLayer.Main) {
    return [inv(c.vocal_vol, 0, 2), inv(c.mix, 0, 1),
            inv(c.master_vol, 0, 2), invTone(c.tone),
            invCube(c.sensitivity, 0, 8), invDrive(c.drive)];
  }
  return [inv(c.closed_vowel, 0, 4), inv(c.open_vowel, 0, 4),
          inv(c.vocal_size, 0, 1), c.resonance,
          invCube(c.attack_ms, 1, 50), invCube(c.release_ms, 20, 500)];
}

// Knob captions per layer, matching docs/BOOKLET.md.
export const kChordKnobLabels = [
  ['Vocal vol', 'Mix', 'Master', 'Tone', 'Sensitivity', 'Drive'],
  ['Closed vowel', 'Open vowel', 'Vocal size', 'Resonance', 'Attack', 'Release'],
];

const kVowelNames = ['oo', 'oh', 'ah', 'eh', 'ee'];
const vowelText = (v) => {
  const i = Math.min(4, Math.max(0, Math.round(v)));
  return `${kVowelNames[i]} ${v.toFixed(1)}`;
};

// Readouts for the value display under each knob.
export function chordKnobValueText(layer, k, c) {
  const f = (x, d = 0) => x.toFixed(d);
  if (layer === ChordLayer.Main) {
    return [f(c.vocal_vol, 2), `${f(c.mix * 100)}%`, f(c.master_vol, 2),
            f(c.tone, 2), f(c.sensitivity, 2), `${f(c.drive, 1)}x`][k];
  }
  return [vowelText(c.closed_vowel), vowelText(c.open_vowel),
          `${f(c.vocal_size * 100)}%`, f(c.resonance, 2),
          `${f(c.attack_ms)} ms`, `${f(c.release_ms)} ms`][k];
}

// inputGain: 1.0 on the pedal (hardware analog gain), 4.0 in the host
// render and the emulator (the lab's digital stand-in the gate thresholds
// were tuned against).
export function toChordEngineParams(c, mouthOpen, inputGain) {
  return {
    inputGain,
    gate: kGateLevels[c.gate_level],
    drive: c.drive,
    sensitivity: c.sensitivity,
    closedVowel: c.closed_vowel,
    openVowel: c.open_vowel,
    formantScale: formantScaleFrom(c.vocal_size),
    bwScale: mapBwScale(c.resonance),
    attackMs: c.attack_ms,
    releaseMs: c.release_ms,
    mouthOpen,
    gain: 1.0,
  };
}

// Charge in chord mode (spec section 3): the gain, tone and size rows use
// applyCharge()'s formulas exactly, by running them on a voice-shaped
// object carrying the three fields, so the two modes can never drift. The
// pitch row has nothing to act on and is ignored. A pure copy: the chord
// setting itself is never written, so a charge can never be auto-saved.
export function applyChargeChord(base, c, level) {
  if (level <= 0.0) return base;
  const v = { vocal_vol: base.vocal_vol, tone: base.tone, vocal_size: base.vocal_size };
  const rows = { ...c, pitch: 0 };
  const r = applyCharge(v, rows, level, true);
  return { ...base, vocal_vol: r.vocal_vol, tone: r.tone, vocal_size: r.vocal_size };
}
