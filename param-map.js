// param-map.js - port of firmware/hothouse/param_map.hpp.
// Knob position (0..1) to parameter, for the three menu layers. Ranges are
// the lab UI's controls; the post-chain ranges come from the milestone 5 plan.

import { MenuLayer } from './voice-params.js';

export function mapLin(t, lo, hi) { return lo + (hi - lo) * t; }

// Glide taper: cubic over 0..300 ms puts 100 ms at ~69% of the travel.
export function mapCube(t, lo, hi) { return lo + (hi - lo) * t * t * t; }

// Aspiration: 0..hi with a detent band at the BOTTOM of the travel that maps
// to exactly 0, so the breath can be switched fully off. Same reason as the
// tone center detent: a real pot never reads exactly 0 at full
// counter-clockwise. The first 5% of the travel pins to 0, the rest rescales.
export function mapLinOff(t, hi) {
  const dz = 0.05;
  if (t <= dz) return 0.0;
  return hi * (t - dz) / (1.0 - dz);
}

// Voice count: 1..8 in eight equal bands, so every count gets the same slice
// of the travel and a knob at either stop lands on a legal value.
export function mapVoices(t) {
  return Math.min(8, Math.max(1, 1 + Math.floor(t * 8)));
}

// Tone: -1..+1 with a center detent band that maps to exactly 0. The center
// must be bit-transparent and a real pot never reads exactly 0.5, so +/-10%
// around center pins tone to 0 and the rest rescales to the full range.
export function mapTone(t) {
  const x = (t - 0.5) * 2.0;
  const dz = 0.1;
  if (Math.abs(x) < dz) return 0.0;
  const s = x > 0.0 ? 1.0 : -1.0;
  return s * (Math.abs(x) - dz) / (1.0 - dz);
}

// Knob k (0..5, left to right, top row then bottom row) at position t.
export function applyKnob(layer, k, t, vp) {
  if (layer === MenuLayer.Menu1) {
    switch (k) {
      case 0: vp.mix        = mapLin(t, 0.0, 1.0); break;
      case 1: vp.glide_ms   = mapCube(t, 0.0, 300.0); break;
      case 2: vp.master_vol = mapLin(t, 0.0, 2.0); break;
      case 3: vp.vocal_vol  = mapLin(t, 0.0, 2.0); break;
      case 4: vp.drive      = mapLin(t, 0.0, 1.0); break;
      case 5: vp.tone       = mapTone(t); break;
    }
  } else if (layer === MenuLayer.Menu2) {
    switch (k) {
      case 0: vp.f1  = mapLin(t, 200.0, 1400.0); break;
      case 1: vp.bw1 = mapLin(t, 5.0, 300.0); break;
      case 2: vp.a1  = mapLin(t, 0.0, 2.0); break;
      case 3: vp.f2  = mapLin(t, 500.0, 2600.0); break;
      case 4: vp.bw2 = mapLin(t, 5.0, 400.0); break;
      case 5: vp.a2  = mapLin(t, 0.0, 2.0); break;
    }
  } else {
    switch (k) {
      case 0: vp.f3  = mapLin(t, 1500.0, 4500.0); break;
      case 1: vp.bw3 = mapLin(t, 5.0, 500.0); break;
      case 2: vp.a3  = mapLin(t, 0.0, 2.0); break;
      case 3: vp.unison       = mapVoices(t); break;
      case 4: vp.detune_cents = mapLin(t, 0.0, 60.0); break;
      case 5: vp.aspiration   = mapLinOff(t, 1.0); break;
    }
  }
}

// The inverse, so the on-screen knobs can be drawn at the position that
// produces the loaded voice. Hardware has no equivalent: a real pot stays
// where it was left, which is exactly what knob pickup exists to handle.
export function knobPositions(layer, vp) {
  const inv = (v, lo, hi) => Math.min(1, Math.max(0, (v - lo) / (hi - lo)));
  const invCube = (v, lo, hi) => Math.cbrt(inv(v, lo, hi));
  const invLinOff = (v, hi) => {
    const dz = 0.05;
    if (v <= 0) return 0;
    return Math.min(1, dz + (1 - dz) * (v / hi));
  };
  // A voice count owns a whole band of travel; draw the knob at its centre.
  const invVoices = (v) => (Math.min(8, Math.max(1, Math.round(v))) - 0.5) / 8;
  const invTone = (v) => {
    const dz = 0.1;
    if (v === 0) return 0.5;
    const s = v > 0 ? 1 : -1;
    return 0.5 + s * (Math.abs(v) * (1 - dz) + dz) / 2;
  };
  if (layer === MenuLayer.Menu1) {
    return [inv(vp.mix, 0, 1), invCube(vp.glide_ms, 0, 300),
            inv(vp.master_vol, 0, 2), inv(vp.vocal_vol, 0, 2),
            inv(vp.drive, 0, 1), invTone(vp.tone)];
  }
  if (layer === MenuLayer.Menu2) {
    return [inv(vp.f1, 200, 1400), inv(vp.bw1, 5, 300), inv(vp.a1, 0, 2),
            inv(vp.f2, 500, 2600), inv(vp.bw2, 5, 400), inv(vp.a2, 0, 2)];
  }
  return [inv(vp.f3, 1500, 4500), inv(vp.bw3, 5, 500), inv(vp.a3, 0, 2),
          invVoices(vp.unison), inv(vp.detune_cents, 0, 60),
          invLinOff(vp.aspiration, 1)];
}

// Knob captions per layer, matching docs/BOOKLET.md.
export const kKnobLabels = [
  ['Mix', 'Glide', 'Master', 'Vocal', 'Drive', 'Tone'],
  ['F1', 'F1 bw', 'F1 amt', 'F2', 'F2 bw', 'F2 amt'],
  ['F3', 'F3 bw', 'F3 amt', 'Voices', 'Detune', 'Aspiration'],
];

// Readouts for the value display under each knob.
export function knobValueText(layer, k, vp) {
  const f = (x, d = 0) => x.toFixed(d);
  if (layer === MenuLayer.Menu1) {
    return [`${f(vp.mix * 100)}%`, `${f(vp.glide_ms)} ms`,
            f(vp.master_vol, 2), f(vp.vocal_vol, 2),
            `${f(vp.drive * 100)}%`, f(vp.tone, 2)][k];
  }
  if (layer === MenuLayer.Menu2) {
    return [`${f(vp.f1)} Hz`, f(vp.bw1, 1), f(vp.a1, 2),
            `${f(vp.f2)} Hz`, f(vp.bw2, 1), f(vp.a2, 2)][k];
  }
  return [`${f(vp.f3)} Hz`, f(vp.bw3, 1), f(vp.a3, 2),
          f(vp.unison), `${f(vp.detune_cents, 1)} ct`,
          f(vp.aspiration, 2)][k];
}
