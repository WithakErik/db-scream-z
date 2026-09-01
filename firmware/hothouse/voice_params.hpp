// voice_params.hpp - milestone 5 EDIT BUFFER / slot block + factory content
// (spec sections 3 and 6). One VoiceParams is one slot is the edit buffer:
// 18 menu parameters + octave + gate level.
#pragma once
#include <cstdint>
#include <cstring>

#include "presets.hpp"

// v2: + ChargeConfig. v3: vibrato removed; menu 3 knobs 4-6 and the sixth
// charge row are the unison stack (voices / detune / aspiration) instead.
// The block is the same SIZE, so the bump is what stops a v2 flash from
// being read back with vib_rate/vib_depth/vib_jitter in those three floats.
inline constexpr uint32_t kVoiceStoreVersion = 3;

// Gate toggle thresholds, indexed low/medium/high. PLACEHOLDERS until the
// milestone 3 on-hardware ear calibration (FIRMWARE.md gotcha 3): medium
// keeps the v12 ear-approved 0.02 (tuned with the lab's digital x4 input
// gain), low/high bracket it by roughly 2.5x each way.
inline constexpr float kGateLevels[3] = {0.008f, 0.02f, 0.05f};

struct VoiceParams {
  float f1, f2, f3;
  float bw1, bw2, bw3;
  float a1, a2, a3;
  float unison;        // stacked voices, 1..8 (engine rounds and clamps)
  float detune_cents;  // unison spread, 0..60 cents
  float aspiration;    // MonkSynth two-sine breath, 0..1, 0 = none
  float mix;           // 0 = 100% dry (bypass sound), 1 = 100% voice
  float glide_ms;      // 0..300
  float master_vol;    // 0..2, unity 1 (both paths)
  float vocal_vol;     // 0..2, unity 1 (voice path)
  float drive;         // 0..1, 0 = clean (exact passthrough)
  float tone;          // -1 dark .. 0 flat (bit-transparent) .. +1 bright
  int8_t octave;       // -1 / 0 / +1 (engine octave_shift)
  uint8_t gate_level;  // index into kGateLevels
  uint8_t pad_[2];     // explicit padding, always 0 (memcmp comparability)
};
static_assert(sizeof(VoiceParams) == 18 * 4 + 4, "no hidden padding");

// Charge mode global config (charge spec section 4): six three-state
// settings. Value mapping is uniform across every row: toggle Up = 2,
// Middle = 1, Down = 0.
struct ChargeConfig {
  uint8_t gain;     // 0 off, 1 on, 2 Above 9000!
  uint8_t time;     // 0 punch, 1 Hamekameka, 2 Birit Spomb
  uint8_t decay;    // 0 off (instant), 1 slow, 2 fast
  uint8_t pitch;    // 0 off, 1 fall, 2 rise
  uint8_t tone;     // 0 off, 1 darker, 2 brighter
  uint8_t aspir;    // 0 off, 1 low, 2 high (added breath on the ramp)
  uint8_t pad_[2];  // always 0 (memcmp comparability)
};
static_assert(sizeof(ChargeConfig) == 8, "no hidden padding");

// Charge/decay durations, indexed by ChargeConfig::time / ::decay.
inline constexpr uint32_t kChargeTimesMs[3] = {750, 2500, 6000};
inline constexpr uint32_t kDecayTimesMs[3] = {0, 1200, 300};

inline ChargeConfig factory_charge_config() {
  ChargeConfig c{};
  c.gain = 1;   // on
  c.time = 1;   // Hamekameka
  c.decay = 1;  // slow
  c.pitch = 1;  // fall
  c.tone = 1;   // darker
  c.aspir = 1;  // low
  return c;
}

// The PersistentStorage block (spec section 6). PersistentStorage<T>
// requires operator!= (see libDaisy util/PersistentStorage.h); memcmp is
// valid because both structs have no hidden padding and pad_ is always 0.
struct VoiceStore {
  uint32_t version;
  VoiceParams slots[4];  // 0 = Set1 R, 1 = Set1 L, 2 = Set2 R, 3 = Set2 L
  ChargeConfig charge;   // global charge mode config (charge spec sec 7)
  bool operator!=(const VoiceStore& o) const {
    return std::memcmp(this, &o, sizeof(VoiceStore)) != 0;
  }
};
static_assert(sizeof(VoiceStore) ==
                  sizeof(uint32_t) + 4 * sizeof(VoiceParams) + sizeof(ChargeConfig),
              "no hidden padding (memcmp operator!= depends on it)");

enum class Side : unsigned char { Left, Right };
enum class Page : unsigned char { Set1, Freeform, Set2 };

inline int slot_index(Page p, Side s) {
  if (p == Page::Set1) return s == Side::Right ? 0 : 1;
  if (p == Page::Set2) return s == Side::Right ? 2 : 3;
  return -1;  // Freeform: no slot
}

// preset_idx indexes kPresets (presets.hpp): Wukong 0, Rice 1, Prince 2,
// Piccolo 3. Non-preset fields are the v12 defaults (FIRMWARE.md section 5)
// plus the milestone 5 post-chain factory values: drive 0, tone center,
// volumes unity, mix full wet. Only Wukong still A/Bs against the
// voice-only milestone renders (spec success criterion 1): it keeps the v12
// reference stack, while the other three are voiced by their own unison
// stack, and no character has vibrato any more.
inline VoiceParams factory_voice(int preset_idx) {
  const CharacterPreset& c = kPresets[preset_idx];
  VoiceParams v{};
  v.f1 = c.formants_hz[0];
  v.f2 = c.formants_hz[1];
  v.f3 = c.formants_hz[2];
  v.bw1 = 32.5f; v.bw2 = 47.5f; v.bw3 = 62.5f;
  v.a1 = v.a2 = v.a3 = 1.0f;
  v.unison = c.unison;
  v.detune_cents = c.detune_cents;
  v.aspiration = c.aspiration;
  v.mix = 1.0f;
  v.glide_ms = 0.0f;
  v.master_vol = 1.0f;
  v.vocal_vol = 1.0f;
  v.drive = 0.0f;
  v.tone = 0.0f;
  v.octave = 0;
  v.gate_level = 1;  // medium
  v.pad_[0] = v.pad_[1] = 0;
  return v;
}

// Set 1 = Wukong (R) / Prince (L); Set 2 = Rice (R) / Piccolo (L).
inline VoiceStore factory_store() {
  VoiceStore s{};
  s.version = kVoiceStoreVersion;
  s.slots[0] = factory_voice(0);  // Wukong
  s.slots[1] = factory_voice(2);  // Prince
  s.slots[2] = factory_voice(1);  // Rice
  s.slots[3] = factory_voice(3);  // Piccolo
  s.charge = factory_charge_config();
  return s;
}
