// voice_params.hpp - milestone 5 EDIT BUFFER / slot block + factory content
// (spec sections 3 and 6). One VoiceParams is one slot is the edit buffer:
// 18 menu parameters + octave + gate level.
#pragma once
#include <cstdint>
#include <cstring>

#include "presets.hpp"

// v2: + ChargeConfig. v3: vibrato removed; menu 3 knobs 4-6 and the sixth
// charge row are the unison stack (voices / detune / aspiration) instead.
// v4: aspiration removed in turn; knob 6 is grain length and the sixth
// charge row drives the stack (voices + detune) together.
// v5: drive retired and replaced by vocal_size in the same slot, and the
// sixth charge row is vocal size rather than the unison stack. The block is
// the same SIZE, which is exactly why the bump is needed: a v4 block read as
// v5 would silently reinterpret each character's saved drive as its
// vocal_size.
//
// The block is the same SIZE at every bump, so the version IS the only
// guard: without it a v3 flash would read a saved aspiration of 0.3 back
// as a 0.3 ms grain length, and the voice would collapse.
// v6: the voices (unison) control retired on 2026-09-01, and grain length
// followed it on 2026-09-02. Both are pinned in main.cpp's to_fof_params()
// (3 voices, 20 ms), which moved no voice because every character
// already stored exactly those values. The two knobs grain's retirement
// freed are vibrato rate and depth, which returned to the engine that same
// day; detune moved to knob 6. Unlike v5
// this DOES change the block size, so a v5 block read as v6 would misread
// every field after a1..a3; the bump is what stops that. Saved characters
// factory-restore on first boot.
// v7 (2026-09-03): same layout as v6; the factory charge config changed
// (Birit Spomb / rise / brighter / full) and the bump is the only way the
// new defaults reach a pedal that already has a v6 block in QSPI, because
// main.cpp only restores defaults on a version mismatch. Saved characters
// factory-restore on first boot.
// v8 (2026-09-24): chord mode (chord mode design spec). One ChordParams
// block APPENDED after `charge`, so every v7 field keeps its offset and a
// v7 image read as v8 is a valid v7 prefix plus whatever QSPI held past
// the old end. migrate_store() below keeps the prefix and replaces the
// tail, so saved characters survive this bump, unlike v5..v7.
// v9 (2026-09-25): three banks (three-banks spec). Freeform retired and
// slots grew from 4 to 6 (Set 3 = Master / Ki-Ki). The new slots are
// appended to the ARRAY, so charge and chord move to new offsets and a v8
// image is NOT a valid v9 prefix past slot 3. migrate_store() reads v8 and
// v7 images through the frozen VoiceStoreV8 below, so saved characters,
// the charge config and (from v8) the chord setting all survive.
inline constexpr uint32_t kVoiceStoreVersion = 9;

// Gate toggle thresholds, indexed low/medium/high. PLACEHOLDERS until the
// milestone 3 on-hardware ear calibration (FIRMWARE.md gotcha 3): medium
// keeps the v12 ear-approved 0.02 (tuned with the lab's digital x4 input
// gain), low/high bracket it by roughly 2.5x each way.
inline constexpr float kGateLevels[3] = {0.008f, 0.02f, 0.05f};

// vocal_size 0..1 maps onto FofParams::formant_scale 1.0..kMinFormantScale.
// At 0.5 Flute's formants read 349/501/1264 Hz against 698/1003/2529, and
// the rendered spectral centroid drops from 1439 Hz to 827 Hz. Ear-tuned.
inline constexpr float kMinFormantScale = 0.5f;

// vocal_size (0..1, what the knob and the charge row store) to the engine's
// formant_scale multiplier. 0 leaves the character exactly as written.
inline float formant_scale_from(float vocal_size) {
  return 1.0f - vocal_size * (1.0f - kMinFormantScale);
}

struct VoiceParams {
  float f1, f2, f3;
  float bw1, bw2, bw3;
  float a1, a2, a3;
  float detune_cents;  // spread across the engine's 3 voices, 0..60 cents
  float vib_rate_hz;      // vibrato rate, 0..50 Hz, 0 = off
  float vib_depth_cents;  // vibrato depth, 0..100 cents
  float mix;           // 0 = 100% dry (bypass sound), 1 = 100% voice
  float glide_ms;      // 0..300
  float master_vol;    // 0..2, unity 1 (both paths)
  float vocal_vol;     // 0..2, unity 1 (voice path)
  float vocal_size;    // 0 = the character's own voice, 1 = deepest
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
  uint8_t size;     // 0 off, 1 half, 2 full (vocal size on the ramp)
  uint8_t pad_[2];  // always 0 (memcmp comparability)
};
static_assert(sizeof(ChargeConfig) == 8, "no hidden padding");

// Charge/decay durations, indexed by ChargeConfig::time / ::decay.
inline constexpr uint32_t kChargeTimesMs[3] = {750, 2500, 6000};
inline constexpr uint32_t kDecayTimesMs[3] = {0, 1200, 300};

inline ChargeConfig factory_charge_config() {
  ChargeConfig c{};
  c.gain = 1;   // on
  c.time = 2;   // Birit Spomb
  c.decay = 1;  // slow
  c.pitch = 2;  // rise
  c.tone = 2;   // brighter
  c.size = 2;   // full
  return c;
}

enum class Side : unsigned char { Left, Right };

// Toggle 2: Up = Set 1, Middle = Set 2, Down = Set 3 (three-banks spec;
// Freeform, the old middle position, retired 2026-09-25).
enum class Page : unsigned char { Set1, Set2, Set3 };

// Every page has two slots: R = 2 * page, L = 2 * page + 1.
inline int slot_index(Page p, Side s) {
  const int base = p == Page::Set1 ? 0 : p == Page::Set2 ? 2 : 4;
  return base + (s == Side::Right ? 0 : 1);
}

// preset_idx indexes kPresets (presets.hpp): Wukong 0, Rice 1, Prince 2,
// Flute 3, Master 4, Ki-Ki 5 (the last two are pedal-side, appended by
// tools/gen_presets.py PEDAL_PRESETS).
// Non-preset fields are the v12 defaults (FIRMWARE.md section 5)
// plus the milestone 5 post-chain factory values: vocal_size 0, tone center,
// volumes unity, mix full wet. Only Wukong still A/Bs against the
// voice-only milestone renders (spec success criterion 1): it keeps the v12
// reference stack, and every character now runs the engine's own 3 voices
// and 20 ms grain, so detune is the only part of the stack that still tells
// them apart. No character has vibrato at factory either.
inline VoiceParams factory_voice(int preset_idx) {
  const CharacterPreset& c = kPresets[preset_idx];
  VoiceParams v{};
  v.f1 = c.formants_hz[0];
  v.f2 = c.formants_hz[1];
  v.f3 = c.formants_hz[2];
  v.bw1 = 32.5f; v.bw2 = 47.5f; v.bw3 = 62.5f;
  v.a1 = v.a2 = v.a3 = 1.0f;
  v.detune_cents = c.detune_cents;
  v.vib_rate_hz = 0.0f;
  v.vib_depth_cents = 0.0f;
  v.mix = 1.0f;
  v.glide_ms = 0.0f;
  v.master_vol = 1.0f;
  v.vocal_vol = 1.0f;
  v.vocal_size = 0.0f;
  v.tone = 0.0f;
  v.octave = 0;
  v.gate_level = 1;  // medium
  v.pad_[0] = v.pad_[1] = 0;
  return v;
}

// Chord mode's one saved setting (chord mode spec sections 3-4). There are
// no slots: this block IS chord mode's edit buffer, auto-saved.
// Ranges are applied by chord_map.hpp; the fields hold mapped values.
struct ChordParams {
  float vocal_vol;     // 0..2 (menu 1 knob 1)
  float mix;           // 0..1 (menu 1 knob 2)
  float master_vol;    // 0..2 (menu 1 knob 3)
  float tone;          // -1..+1 (menu 1 knob 4)
  float sensitivity;   // 0..8, 0 = fixed closed vowel (menu 1 knob 5)
  float drive;         // 1..40 pre-gain (menu 1 knob 6)
  float closed_vowel;  // 0..4: oo oh ah eh ee (chord menu knob 1)
  float open_vowel;    // 0..4 (chord menu knob 2)
  float vocal_size;    // 0..1 (chord menu knob 3)
  float resonance;     // 0 soft .. 1 sharp (chord menu knob 4)
  float attack_ms;     // 1..50 mouth attack (chord menu knob 5)
  float release_ms;    // 20..500 mouth release (chord menu knob 6)
  uint8_t gate_level;  // index into kGateLevels (toggle 3)
  uint8_t pad_[3];     // always 0 (memcmp comparability)
};
static_assert(sizeof(ChordParams) == 12 * 4 + 4, "no hidden padding");

// Ear-tuned on the hardware and captured from its QSPI with
// firmware/qspi_dump (2026-09-28). End-stop readings (e.g. 1.9999 vocal
// vol, 39.8x drive) are snapped to the stop they were set at; the rest are
// rounded to two significant figures. The first-pass sensitivity of 3.0 held
// the mouth fully open for most of every note, so the vowel never audibly
// moved; 1.6 lets it close as the note decays. Mix, tone, vocal size and the
// gate still come from factory_voice(), as a character's do.
inline ChordParams factory_chord() {
  const VoiceParams v = factory_voice(0);
  ChordParams c{};
  c.vocal_vol = 2.0f;     // knob full up
  c.mix = v.mix;
  c.master_vol = 1.2f;
  c.tone = v.tone;
  c.sensitivity = 1.6f;
  c.drive = 40.0f;        // knob full up
  c.closed_vowel = 0.0f;  // oo
  c.open_vowel = 4.0f;    // ee
  c.vocal_size = v.vocal_size;
  c.resonance = 0.53f;
  c.attack_ms = 17.0f;
  c.release_ms = 20.0f;   // knob full down
  c.gate_level = v.gate_level;
  c.pad_[0] = c.pad_[1] = c.pad_[2] = 0;
  return c;
}

// The PersistentStorage block (spec section 6). PersistentStorage<T>
// requires operator!= (see libDaisy util/PersistentStorage.h); memcmp is
// valid because both structs have no hidden padding and pad_ is always 0.
struct VoiceStore {
  uint32_t version;
  VoiceParams slots[6];  // 0/1 = Set1 R/L, 2/3 = Set2 R/L, 4/5 = Set3 R/L
  ChargeConfig charge;   // global charge mode config (charge spec sec 7)
  ChordParams chord;     // chord mode's one setting (see migrate_store)
  bool operator!=(const VoiceStore& o) const {
    return std::memcmp(this, &o, sizeof(VoiceStore)) != 0;
  }
};
static_assert(sizeof(VoiceStore) ==
                  sizeof(uint32_t) + 6 * sizeof(VoiceParams) + sizeof(ChargeConfig) +
                      sizeof(ChordParams),
              "no hidden padding (memcmp operator!= depends on it)");

// The v8 block exactly as it was laid out in QSPI. FROZEN: never edit it,
// it describes bytes already written to shipped pedals. v7 is the same
// layout without the chord tail. migrate_store() reads old images through
// it because v9's extra slots moved charge and chord.
struct VoiceStoreV8 {
  uint32_t version;
  VoiceParams slots[4];
  ChargeConfig charge;
  ChordParams chord;
};
static_assert(sizeof(VoiceStoreV8) ==
                  sizeof(uint32_t) + 4 * sizeof(VoiceParams) + sizeof(ChargeConfig) +
                      sizeof(ChordParams),
              "v8 layout has no hidden padding");
static_assert(sizeof(VoiceStoreV8) < sizeof(VoiceStore),
              "a v8 image must fit inside the bytes a v9 Init() reads");

// Set 1 = Wukong (R) / Prince (L); Set 2 = Rice (R) / Flute (L);
// Set 3 = Master (R) / Ki-Ki (L).
inline VoiceStore factory_store() {
  VoiceStore s{};
  s.version = kVoiceStoreVersion;
  s.slots[0] = factory_voice(0);  // Wukong
  s.slots[1] = factory_voice(2);  // Prince
  s.slots[2] = factory_voice(1);  // Rice
  s.slots[3] = factory_voice(3);  // Flute
  s.slots[4] = factory_voice(4);  // Master
  s.slots[5] = factory_voice(5);  // Ki-Ki
  s.charge = factory_charge_config();
  s.chord = factory_chord();
  return s;
}

// What main.cpp does with the block PersistentStorage::Init() read
// (chord mode spec section 4). Pure, so host-testable; main.cpp only
// saves on Migrated and calls RestoreDefaults() on Reset.
enum class StoreLoad : unsigned char { Current, Migrated, Reset };

inline StoreLoad migrate_store(VoiceStore& s) {
  if (s.version == kVoiceStoreVersion) return StoreLoad::Current;
  if (s.version == 8 || s.version == 7) {
    // Reinterpret the old layout, then rebuild on a factory v9 so Set 3
    // gets Master / Ki-Ki. v7 had no chord block: its bytes are QSPI
    // garbage, so chord stays factory.
    VoiceStoreV8 old;
    std::memcpy(&old, &s, sizeof old);
    s = factory_store();
    for (int i = 0; i < 4; i++) s.slots[i] = old.slots[i];
    s.charge = old.charge;
    if (old.version == 8) s.chord = old.chord;
    return StoreLoad::Migrated;
  }
  s = factory_store();
  return StoreLoad::Reset;
}
