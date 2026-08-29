// voice-params.js - port of firmware/hothouse/voice_params.hpp.
// One VoiceParams is one slot is the edit buffer: 18 menu parameters plus
// octave and gate level. Kept structurally identical to the C++ so the Node
// tests in ../tests can assert the same values the host tests do.

export const kVoiceStoreVersion = 2;

// Gate toggle thresholds, indexed low/medium/high. Placeholders until the
// on-hardware ear calibration; medium is the v12 ear-approved 0.02.
export const kGateLevels = [0.008, 0.02, 0.05];

// Charge/decay durations, indexed by ChargeConfig.time / .decay.
export const kChargeTimesMs = [750, 2500, 6000];
export const kDecayTimesMs = [0, 1200, 300];

export const Side = { Left: 'Left', Right: 'Right' };
export const Page = { Set1: 'Set1', Freeform: 'Freeform', Set2: 'Set2' };
export const TogglePos = { Up: 'Up', Middle: 'Middle', Down: 'Down' };
export const EngagedSource = {
  None: 'None', SlotR: 'SlotR', SlotL: 'SlotL', Freeform: 'Freeform',
};
export const MenuLayer = { Menu1: 0, Menu2: 1, Menu3: 2 };

// Wukong 0, Rice 1, Prince 2, Piccolo 3 - the order in
// firmware/engine/presets.hpp, which slot_index and factory_store depend on.
export const kPresets = [
  { name: 'Wukong',  formants_hz: [858.4, 1234.0, 3111.7], vib_rate_hz: 6.72, vib_depth_semi: 0.423 },
  { name: 'Rice',    formants_hz: [1000.0, 1437.5, 3625.0], vib_rate_hz: 7.25, vib_depth_semi: 0.209 },
  { name: 'Prince',  formants_hz: [741.6, 1066.0, 2688.3], vib_rate_hz: 6.98, vib_depth_semi: 0.7 },
  { name: 'Piccolo', formants_hz: [697.6, 1002.8, 2528.8], vib_rate_hz: 7.1,  vib_depth_semi: 0.7 },
];

export function cloneVoice(v) { return { ...v }; }

export function voicesEqual(a, b) {
  for (const k of Object.keys(a)) if (a[k] !== b[k]) return false;
  return true;
}

// Non-preset fields are the v12 defaults plus the milestone 5 post-chain
// factory values: drive 0, tone center, volumes unity, mix full wet.
export function factoryVoice(presetIdx) {
  const c = kPresets[presetIdx];
  return {
    f1: c.formants_hz[0], f2: c.formants_hz[1], f3: c.formants_hz[2],
    bw1: 32.5, bw2: 47.5, bw3: 62.5,
    a1: 1.0, a2: 1.0, a3: 1.0,
    vib_rate: c.vib_rate_hz, vib_depth: c.vib_depth_semi, vib_jitter: 0.10,
    mix: 1.0,
    glide_ms: 0.0,
    master_vol: 1.0,
    vocal_vol: 1.0,
    drive: 0.0,
    tone: 0.0,
    octave: 0,
    gate_level: 1,   // medium
  };
}

export function factoryChargeConfig() {
  return { gain: 1, time: 1, decay: 1, pitch: 1, tone: 1, vib: 1 };
}

export function chargeConfigsEqual(a, b) {
  return a.gain === b.gain && a.time === b.time && a.decay === b.decay &&
         a.pitch === b.pitch && a.tone === b.tone && a.vib === b.vib;
}

// Set 1 = Wukong (R) / Prince (L); Set 2 = Rice (R) / Piccolo (L).
export function factoryStore() {
  return {
    version: kVoiceStoreVersion,
    slots: [factoryVoice(0), factoryVoice(2), factoryVoice(1), factoryVoice(3)],
    charge: factoryChargeConfig(),
  };
}

// slots: 0 = Set1 R, 1 = Set1 L, 2 = Set2 R, 3 = Set2 L. Freeform has none.
export function slotIndex(page, side) {
  if (page === Page.Set1) return side === Side.Right ? 0 : 1;
  if (page === Page.Set2) return side === Side.Right ? 2 : 3;
  return -1;
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
