// beast-presets.js - port of firmware/hothouse/beast_presets.hpp.
//
// The hidden bank. On the pedal you hold both footswitches while powering
// on; here you load the page as ?beast, which is the same moment in the
// only sense a browser has one. Four animal calls replace the four DBZ
// characters for the session, and nothing is persisted: app.js skips its
// localStorage write, exactly as the firmware skips the QSPI one.
//
// Kept structurally identical to the C++ so the Node tests can assert the
// same values the host tests do, which is the same contract voice-params.js
// holds for voice_params.hpp. Deliberately NOT in presets.json: that file
// is a frozen verbatim copy of the lab's, guarded by
// tools/check_engine_copy.py, and nothing may be added to it.
//
// Every number is a first-pass ear target due a tuning pass on hardware.
// The tests assert ranges and relationships, not exact values.
//
// This module imports from voice-params.js and never the other way round:
// voiceName() takes the name table as an argument precisely so the two
// files do not form a cycle.

import { kVoiceStoreVersion, factoryChargeConfig } from './voice-params.js';

// Slot order, matching slotIndex(): 0 = Set1 R, 1 = Set1 L, 2 = Set2 R,
// 3 = Set2 L. Set 1 is Cow / Wolf, Set 2 is Whale / Elephant, and
// toggle 2 still pages between them.
export const kBeastCow = 0;
export const kBeastWolf = 1;
export const kBeastWhale = 2;
export const kBeastElephant = 3;

// The firmware table carries no names (nothing on the pedal displays one,
// and internal flash is at 94%). Here the panel has a label to fill, so
// the names live on this side only.
// vocal_size is what makes an animal an ANIMAL rather than a person doing an
// impression: it scales the whole tract, so the formants move together and
// the creature reads as physically large. Values are on the knob's 32-step
// grid, so nudging knob 6 back to a beast's factory setting lands exactly.
// The beasts used to carry a per-beast drive value for grit; drive was
// retired from the pedal, and size is what replaces it.
export const kBeasts = [
  // Cow: a long tract, so low formants on an /o/, an octave down for the
  // body. Wide bandwidths blur the vowel into an animal, and the glide is
  // the slide a moo makes falling off its own note.
  { name: 'Cow',      f1: 420.0, f2: 800.0,  f3: 2400.0, bw1: 60.0, bw2: 90.0,  bw3: 120.0,
    detune_cents: 14.0, glide_ms: 140.0, tone: -0.35, vocal_size: 0.5,   octave: -1 },

  // Wolf: a pure sustained /u/. The howl used to get its "several wolves
  // a valley away" from six stacked voices beating against each other;
  // the voices control was retired 2026-09-01, so the 22 cents of detune
  // across the engine's three is what carries that now. The long glide
  // swoops in.
  { name: 'Wolf',     f1: 350.0, f2: 850.0,  f3: 2600.0, bw1: 40.0, bw2: 55.0,  bw3: 80.0,
    detune_cents: 22.0, glide_ms: 220.0, tone: 0.10,  vocal_size: 0.25,  octave: 0 },

  // Whale: the lowest and most smeared voice in the bank, glide pinned at
  // its 300 ms ceiling so every note arrives by sliding. Three voices
  // spread wide beat slowly, like distance.
  { name: 'Whale',    f1: 300.0, f2: 700.0,  f3: 1800.0, bw1: 70.0, bw2: 100.0, bw3: 150.0,
    detune_cents: 30.0, glide_ms: 300.0, tone: -0.20, vocal_size: 0.75,  octave: -1 },

  // Elephant: the outlier. Bright, tight and brassy rather than dark and
  // vocal, with barely any detune so the blast stays focused. Short
  // glide: a trumpet is an attack.
  { name: 'Elephant', f1: 700.0, f2: 1900.0, f3: 3200.0, bw1: 45.0, bw2: 60.0,  bw3: 90.0,
    detune_cents: 7.0,  glide_ms: 45.0,  tone: 0.55,  vocal_size: 0.375, octave: 0 },
];

// One beast as a full edit buffer. The key set matches factoryVoice()
// exactly: voicesEqual() and knobPositions() iterate these keys, so a
// missing or extra one would quietly break the panel.
export function beastVoice(idx) {
  const b = kBeasts[idx];
  return {
    f1: b.f1, f2: b.f2, f3: b.f3,
    bw1: b.bw1, bw2: b.bw2, bw3: b.bw3,
    a1: 1.0, a2: 1.0, a3: 1.0,
    detune_cents: b.detune_cents,
    // Not in the table: like the C++ bank, every beast wants vibrato off,
    // and the keys must still be present because voicesEqual() and
    // knobPositions() iterate factoryVoice()'s key set.
    vib_rate_hz: 0.0, vib_depth_cents: 0.0,
    mix: 1.0,
    glide_ms: b.glide_ms,
    master_vol: 1.0,
    vocal_vol: 1.0,
    vocal_size: b.vocal_size,
    tone: b.tone,
    octave: b.octave,
    gate_level: 1,   // medium
  };
}

// The session store handed to ui.init() in place of the localStorage one.
//
// Charge mode still works on the animals exactly as it does on the
// characters, but the config is the FACTORY one rather than whatever is
// saved. Beast mode is a self-contained sandbox: nothing it does survives
// the reload, so inheriting a half-remembered charge config would only
// make the same gesture sound different on different days.
export function beastStore() {
  return {
    version: kVoiceStoreVersion,
    slots: [beastVoice(0), beastVoice(1), beastVoice(2), beastVoice(3)],
    charge: factoryChargeConfig(),
  };
}
