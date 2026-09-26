// voice-params.js - port of firmware/hothouse/voice_params.hpp.
// One VoiceParams is one slot is the edit buffer: 18 menu parameters plus
// octave and gate level. Kept structurally identical to the C++ so the Node
// tests in ../tests can assert the same values the host tests do.

// v3: vibrato removed; menu 3 knobs 4-6 and the sixth charge row are the
// unison stack (voices / detune / aspiration) instead. v4: aspiration
// removed in turn; knob 6 is grain length and the sixth charge row drives
// the stack (voices + detune) together. v5: drive retired and replaced by
// vocal_size in the same slot, and the sixth charge row is vocal size
// rather than the unison stack.
// v6: the voices (unison) control retired on 2026-09-01, and grain length
// followed it on 2026-09-02. Both are pinned in audio.js's toFofParams()
// (3 voices, 20 ms), which moved no voice because every character already
// stored exactly those values. The two knobs grain's retirement freed are
// vibrato rate and depth, which returned to the engine that same day;
// detune moved to knob 6. Unlike v5
// this DOES change the block size, so a v5 block read as v6 would misread
// every field after a1..a3; the bump is what stops that. Saved characters
// factory-restore on first boot.
// v7 (2026-09-03): same layout as v6; the factory charge config changed
// (Birit Spomb / rise / brighter / full) and the bump is what makes a
// returning browser (app.js loads the store only on a version match) pick
// the new defaults up, exactly as the pedal does with its QSPI block.
// v8 (2026-09-24): chord mode (chord mode design spec). One ChordParams
// object (`chord`) added alongside `slots` and `charge`. migrateStore()
// below is the browser's mirror of main.cpp's migrate_store(): a v7 store
// (no `chord` key at all, since JSON has no QSPI tail to inherit stray
// bytes from) migrates in place, keeping slots and charge and adding
// chord = factoryChord(); anything else resets to factoryStore().
// v9 (2026-09-25): three banks (three-banks spec). Freeform retired and
// slots grew from 4 to 6 (Set 3 = Master / Ki-Ki). migrateStore() keeps a
// v8 or v7 store's four slots and charge (and v8's chord) and appends the
// factory Set 3, mirroring main.cpp's migrate_store().
export const kVoiceStoreVersion = 9;

// Gate toggle thresholds, indexed low/medium/high. Placeholders until the
// on-hardware ear calibration; medium is the v12 ear-approved 0.02.
export const kGateLevels = [0.008, 0.02, 0.05];

// Charge/decay durations, indexed by ChargeConfig.time / .decay.
export const kChargeTimesMs = [750, 2500, 6000];
export const kDecayTimesMs = [0, 1200, 300];

// vocal_size 0..1 maps onto the engine's formant_scale multiplier 1.0..
// kMinFormantScale. Mirrors firmware/hothouse/voice_params.hpp exactly.
export const kMinFormantScale = 0.5;

// vocal_size (0..1, what the knob and the charge row store) to the engine's
// formantScale multiplier. 0 leaves the character exactly as written.
export function formantScaleFrom(vocalSize) {
  return 1.0 - vocalSize * (1.0 - kMinFormantScale);
}

export const Side = { Left: 'Left', Right: 'Right' };
export const Page = { Set1: 'Set1', Set2: 'Set2', Set3: 'Set3' };
export const TogglePos = { Up: 'Up', Middle: 'Middle', Down: 'Down' };
export const EngagedSource = { None: 'None', SlotR: 'SlotR', SlotL: 'SlotL' };
export const MenuLayer = { Menu1: 0, Menu2: 1, Menu3: 2 };

// Wukong 0, Rice 1, Prince 2, Piccolo 3, Master 4, Ki-Ki 5 - the order in
// firmware/engine/presets.hpp, which slot_index and factory_store depend on.
// Master and Ki-Ki are pedal-side (tools/gen_presets.py PEDAL_PRESETS).
export const kPresets = [
  { name: 'Wukong',  formants_hz: [858.4, 1234.0, 3111.7], detune_cents: 11.0 },
  { name: 'Rice',    formants_hz: [1000.0, 1437.5, 3625.0], detune_cents: 8.0 },
  { name: 'Prince',  formants_hz: [741.6, 1066.0, 2688.3], detune_cents: 18.0 },
  { name: 'Piccolo', formants_hz: [697.6, 1002.8, 2528.8], detune_cents: 26.0 },
  { name: 'Master',  formants_hz: [640.0, 1080.0, 2400.0], detune_cents: 30.0 },
  { name: 'Ki-Ki',   formants_hz: [950.0, 1800.0, 3350.0], detune_cents: 8.0 },
];

export function cloneVoice(v) { return { ...v }; }

export function voicesEqual(a, b) {
  for (const k of Object.keys(a)) if (a[k] !== b[k]) return false;
  return true;
}

// Non-preset fields are the v12 defaults plus the milestone 5 post-chain
// factory values: vocal_size 0, tone center, volumes unity, mix full wet.
// Only Wukong still A/Bs against the voice-only milestone renders: it keeps
// the v12 reference stack, and every character now runs the engine's own 3
// voices and 20 ms grain, so detune is the only part of the stack that
// still tells them apart. No character has vibrato at factory either.
export function factoryVoice(presetIdx) {
  const c = kPresets[presetIdx];
  return {
    f1: c.formants_hz[0], f2: c.formants_hz[1], f3: c.formants_hz[2],
    bw1: 32.5, bw2: 47.5, bw3: 62.5,
    a1: 1.0, a2: 1.0, a3: 1.0,
    detune_cents: c.detune_cents,
    vib_rate_hz: 0.0, vib_depth_cents: 0.0,
    mix: 1.0,
    glide_ms: 0.0,
    master_vol: 1.0,
    vocal_vol: 1.0,
    vocal_size: 0.0,
    tone: 0.0,
    octave: 0,
    gate_level: 1,   // medium
  };
}

// Mirrors firmware/hothouse/voice_params.hpp factory_charge_config():
// gain on, Birit Spomb, slow decay, pitch rise, brighter, full size.
export function factoryChargeConfig() {
  return { gain: 1, time: 2, decay: 1, pitch: 2, tone: 2, size: 2 };
}

export function chargeConfigsEqual(a, b) {
  return a.gain === b.gain && a.time === b.time && a.decay === b.decay &&
         a.pitch === b.pitch && a.tone === b.tone && a.size === b.size;
}

// Chord mode's one saved setting (chord mode spec sections 3-4). There are
// no slots: this object IS chord mode's edit buffer, auto-saved. Ranges are
// applied by chord-map.js; the fields hold mapped values. Mirrors
// firmware/hothouse/voice_params.hpp ChordParams field for field.
//
// First-pass ear targets (spec section 4). The shared post-chain fields and
// the gate come from factoryVoice() so chord mode starts at the same levels
// as a character.
export function factoryChord() {
  const v = factoryVoice(0);
  return {
    vocal_vol: v.vocal_vol,
    mix: v.mix,
    master_vol: v.master_vol,
    tone: v.tone,
    sensitivity: 3.0,
    drive: 10.0,
    closed_vowel: 0.0,  // oo
    open_vowel: 2.0,    // ah
    vocal_size: v.vocal_size,
    resonance: 0.5,
    attack_ms: 10.0,
    release_ms: 150.0,
    gate_level: v.gate_level,
  };
}

export function chordsEqual(a, b) {
  for (const k of Object.keys(a)) if (a[k] !== b[k]) return false;
  return true;
}

// Set 1 = Wukong (R) / Prince (L); Set 2 = Rice (R) / Piccolo (L);
// Set 3 = Master (R) / Ki-Ki (L).
export function factoryStore() {
  return {
    version: kVoiceStoreVersion,
    slots: [factoryVoice(0), factoryVoice(2), factoryVoice(1), factoryVoice(3),
            factoryVoice(4), factoryVoice(5)],
    charge: factoryChargeConfig(),
    chord: factoryChord(),
  };
}

// The browser's mirror of main.cpp's pure migrate_store() (chord mode spec
// section 4): what a store loaded from localStorage should be run through
// before use. Mutates `s` in place and returns which of the three happened,
// so the caller knows whether to persist the result.
export function migrateStore(s) {
  if (s.version === kVoiceStoreVersion) return 'current';
  if (s.version === 8 || s.version === 7) {
    // Four slots before v9: keep them and append the factory Set 3. A v7
    // store has no `chord` key at all: it gets the factory one.
    const f = factoryStore();
    s.slots = [...s.slots.slice(0, 4), f.slots[4], f.slots[5]];
    if (s.version === 7) s.chord = f.chord;
    s.version = kVoiceStoreVersion;
    return 'migrated';
  }
  Object.assign(s, factoryStore());
  return 'reset';
}

// slots: 0/1 = Set1 R/L, 2/3 = Set2 R/L, 4/5 = Set3 R/L.
export function slotIndex(page, side) {
  const base = page === Page.Set1 ? 0 : page === Page.Set2 ? 2 : 4;
  return base + (side === Side.Right ? 0 : 1);
}

// The name of whatever is in a slot, for the UI. Matches on the formant
// triple, so an edited slot correctly stops claiming to be a factory voice.
export function voiceName(v) {
  for (const p of kPresets) {
    if (Math.abs(v.f1 - p.formants_hz[0]) < 0.05 &&
        Math.abs(v.f2 - p.formants_hz[1]) < 0.05 &&
        Math.abs(v.f3 - p.formants_hz[2]) < 0.05) return p.name;
  }
  return 'Custom';
}
