// param-map.js - port of firmware/hothouse/param_map.hpp.
// Knob position (0..1) to parameter, for the three menu layers. Ranges are
// the lab UI's controls; the post-chain ranges come from the milestone 5 plan.

import { MenuLayer } from './voice-params.js';

export function mapLin(t, lo, hi) { return lo + (hi - lo) * t; }

// Glide taper: cubic over 0..300 ms puts 100 ms at ~69% of the travel.
export function mapCube(t, lo, hi) { return lo + (hi - lo) * t * t * t; }

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
      case 3: vp.vib_rate   = mapLin(t, 0.0, 14.0); break;
      case 4: vp.vib_depth  = mapLin(t, 0.0, 4.0); break;
      case 5: vp.vib_jitter = mapLin(t, 0.0, 0.6); break;
    }
  }
}

// The inverse, so the on-screen knobs can be drawn at the position that
// produces the loaded voice. Hardware has no equivalent: a real pot stays
// where it was left, which is exactly what knob pickup exists to handle.
export function knobPositions(layer, vp) {
  const inv = (v, lo, hi) => Math.min(1, Math.max(0, (v - lo) / (hi - lo)));
  const invCube = (v, lo, hi) => Math.cbrt(inv(v, lo, hi));
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
          inv(vp.vib_rate, 0, 14), inv(vp.vib_depth, 0, 4),
          inv(vp.vib_jitter, 0, 0.6)];
}

// Knob captions per layer, matching docs/BOOKLET.md.
export const kKnobLabels = [
  ['Mix', 'Glide', 'Master', 'Vocal', 'Drive', 'Tone'],
  ['F1', 'F1 bw', 'F1 amt', 'F2', 'F2 bw', 'F2 amt'],
  ['F3', 'F3 bw', 'F3 amt', 'Vib rate', 'Vib depth', 'Vib jitter'],
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
          `${f(vp.vib_rate, 2)} Hz`, `${f(vp.vib_depth, 2)} st`,
          f(vp.vib_jitter, 2)][k];
}
