// param-map.js - port of firmware/hothouse/param_map.hpp.
// Knob position (0..1) to parameter, for the three menu layers. Ranges are
// the lab UI's controls; the post-chain ranges come from the milestone 5 plan.

import { MenuLayer } from './voice-params.js';

export function mapLin(t, lo, hi) { return lo + (hi - lo) * t; }

// Glide taper: cubic over 0..300 ms puts 100 ms at ~69% of the travel.
export function mapCube(t, lo, hi) { return lo + (hi - lo) * t * t * t; }

// Grain length: 4..40 ms, the lab's own slider range, with a center detent
// band pinning exactly 20 ms - the value the engine baked in before this
// knob existed, so the detent is what makes a factory voice reachable again
// after a sweep. The two halves scale independently because the range is
// asymmetric about its detent (16 ms below, 20 ms above).
export function mapGrain(t) {
  const dz = 0.1;
  const x = (t - 0.5) * 2.0;
  if (Math.abs(x) < dz) return 20.0;
  const u = (Math.abs(x) - dz) / (1.0 - dz);
  return x > 0.0 ? 20.0 + u * 20.0 : 20.0 - u * 16.0;
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

// Vocal size: 0..1, quantised to 32 steps. formant_scale is grain-affecting,
// so an unquantised knob would rebuild the grain tables on every audio block
// during a sweep. Both stops land exactly on the grid, so 0 and 1 stay
// exactly reachable.
export function mapVocalSize(t) {
  if (t <= 0.0) return 0.0;
  if (t >= 1.0) return 1.0;
  return Math.floor(t * 32.0 + 0.5) / 32.0;
}

// Knob k (0..5, left to right, top row then bottom row) at position t.
export function applyKnob(layer, k, t, vp) {
  if (layer === MenuLayer.Menu1) {
    switch (k) {
      case 0: vp.vocal_vol  = mapLin(t, 0.0, 2.0); break;
      case 1: vp.mix        = mapLin(t, 0.0, 1.0); break;
      case 2: vp.master_vol = mapLin(t, 0.0, 2.0); break;
      case 3: vp.tone       = mapTone(t); break;
      case 4: vp.glide_ms   = mapCube(t, 0.0, 300.0); break;
      case 5: vp.vocal_size = mapVocalSize(t); break;
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
      case 5: vp.grain_ms     = mapGrain(t); break;
    }
  }
}

// The inverse, so the on-screen knobs can be drawn at the position that
// produces the loaded voice. Hardware has no equivalent: a real pot stays
// where it was left, which is exactly what knob pickup exists to handle.
export function knobPositions(layer, vp) {
  const inv = (v, lo, hi) => Math.min(1, Math.max(0, (v - lo) / (hi - lo)));
  const invCube = (v, lo, hi) => Math.cbrt(inv(v, lo, hi));
  const invGrain = (v) => {
    const dz = 0.1;
    if (v === 20) return 0.5;
    const s = v > 20 ? 1 : -1;
    const u = s > 0 ? (v - 20) / 20 : (20 - v) / 16;
    return Math.min(1, Math.max(0, 0.5 + s * (u * (1 - dz) + dz) / 2));
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
    return [inv(vp.vocal_vol, 0, 2), inv(vp.mix, 0, 1),
            inv(vp.master_vol, 0, 2), invTone(vp.tone),
            invCube(vp.glide_ms, 0, 300), inv(vp.vocal_size, 0, 1)];
  }
  if (layer === MenuLayer.Menu2) {
    return [inv(vp.f1, 200, 1400), inv(vp.bw1, 5, 300), inv(vp.a1, 0, 2),
            inv(vp.f2, 500, 2600), inv(vp.bw2, 5, 400), inv(vp.a2, 0, 2)];
  }
  return [inv(vp.f3, 1500, 4500), inv(vp.bw3, 5, 500), inv(vp.a3, 0, 2),
          invVoices(vp.unison), inv(vp.detune_cents, 0, 60),
          invGrain(vp.grain_ms)];
}

// Knob captions per layer, matching docs/BOOKLET.md.
export const kKnobLabels = [
  ['Vocal', 'Mix', 'Master', 'Tone', 'Glide', 'Size'],
  ['F1', 'F1 bw', 'F1 amt', 'F2', 'F2 bw', 'F2 amt'],
  ['F3', 'F3 bw', 'F3 amt', 'Voices', 'Detune', 'Grain'],
];

// Readouts for the value display under each knob.
export function knobValueText(layer, k, vp) {
  const f = (x, d = 0) => x.toFixed(d);
  if (layer === MenuLayer.Menu1) {
    return [f(vp.vocal_vol, 2), `${f(vp.mix * 100)}%`,
            f(vp.master_vol, 2), f(vp.tone, 2),
            `${f(vp.glide_ms)} ms`, `${f(vp.vocal_size * 100)}%`][k];
  }
  if (layer === MenuLayer.Menu2) {
    return [`${f(vp.f1)} Hz`, f(vp.bw1, 1), f(vp.a1, 2),
            `${f(vp.f2)} Hz`, f(vp.bw2, 1), f(vp.a2, 2)][k];
  }
  return [`${f(vp.f3)} Hz`, f(vp.bw3, 1), f(vp.a3, 2),
          f(vp.unison), `${f(vp.detune_cents, 1)} ct`,
          `${f(vp.grain_ms, 1)} ms`][k];
}
