// test_store.cpp - store v9 (three-banks spec section 4): six slots, the
// factory Set 3 (Master / Ki-Ki), and migrate_store(), the pure function
// main.cpp runs on whatever PersistentStorage::Init() read. v8 and v7
// images are rebuilt byte for byte through the frozen VoiceStoreV8 layout.
// (The chord block and its factory values date from store v8, chord mode
// spec section 4.)
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "voice_params.hpp"

int main() {
  // ---- layout: six slots, then charge, then chord
  {
    static_assert(sizeof(ChordParams) == 12 * 4 + 4, "no hidden padding");
    // The octave took pad_[0]'s byte (chord octave spec, 2026-10-06). Every
    // v9 image saved before then holds 0 there, so it loads as octave 0
    // with no store version bump (plan Review Focus 3).
    static_assert(offsetof(ChordParams, octave) == 12 * 4 + 1);
    assert(offsetof(VoiceStore, chord) ==
           sizeof(uint32_t) + 6 * sizeof(VoiceParams) + sizeof(ChargeConfig));
    assert(kVoiceStoreVersion == 9);
    // A v8 image must sit entirely inside the bytes Init() reads for v9.
    static_assert(sizeof(VoiceStoreV8) < sizeof(VoiceStore));
    assert(offsetof(VoiceStoreV8, chord) ==
           sizeof(uint32_t) + 4 * sizeof(VoiceParams) + sizeof(ChargeConfig));
  }

  // ---- factory chord: hardware-tuned values (2026-09-28), pad zeroed
  {
    const ChordParams c = factory_chord();
    const VoiceParams v = factory_voice(0);
    assert(c.vocal_vol == 2.0f && c.mix == v.mix);
    assert(c.master_vol == 1.2f && c.tone == v.tone);
    assert(c.vocal_size == v.vocal_size && c.gate_level == v.gate_level);
    assert(c.sensitivity == 1.6f && c.drive == 40.0f);
    assert(c.closed_vowel == 0.0f && c.open_vowel == 4.0f);  // oo -> ee
    assert(c.resonance == 0.53f);
    assert(c.attack_ms == 17.0f && c.release_ms == 20.0f);
    assert(c.octave == 0);
    assert(c.pad_[0] == 0 && c.pad_[1] == 0);
    const VoiceStore s = factory_store();
    assert(s.version == 9);
    assert(std::memcmp(&s.chord, &c, sizeof c) == 0);
  }

  // ---- factory Set 3: Master (R, slot 4) and Ki-Ki (L, slot 5)
  {
    const VoiceStore s = factory_store();
    assert(std::strcmp(kPresets[4].name, "Master") == 0);
    assert(std::strcmp(kPresets[5].name, "Ki-Ki") == 0);
    assert(s.slots[4].f1 == 640.0f && s.slots[4].f2 == 1080.0f &&
           s.slots[4].f3 == 2400.0f && s.slots[4].detune_cents == 30.0f);
    assert(s.slots[5].f1 == 950.0f && s.slots[5].f2 == 1800.0f &&
           s.slots[5].f3 == 3350.0f && s.slots[5].detune_cents == 8.0f);
    const VoiceParams m = factory_voice(4), k = factory_voice(5);
    assert(std::memcmp(&s.slots[4], &m, sizeof m) == 0);
    assert(std::memcmp(&s.slots[5], &k, sizeof k) == 0);
  }

  // ---- v9 is left alone
  {
    VoiceStore s = factory_store();
    s.chord.drive = 33.0f;
    s.slots[5].f1 = 1111.0f;
    const VoiceStore before = s;
    assert(migrate_store(s) == StoreLoad::Current);
    assert(std::memcmp(&s, &before, sizeof s) == 0);
  }

  // ---- v8 image, as Init() copies it out of QSPI after a v8 write: the
  // v8 bytes, then 0xFF past the old end. charge and chord sat at v8
  // offsets, so a prefix patch would read slot bytes as charge; the
  // migration must go through VoiceStoreV8.
  {
    const VoiceStore f = factory_store();
    VoiceStoreV8 v8{};
    v8.version = 8;
    for (int i = 0; i < 4; i++) v8.slots[i] = f.slots[i];
    v8.slots[1].f1 = 777.0f;           // user edits that must survive
    v8.slots[3].detune_cents = 42.0f;
    v8.charge = f.charge;
    v8.charge.gain = 2;
    v8.chord = f.chord;
    v8.chord.drive = 33.0f;
    VoiceStore s;
    std::memset(&s, 0xFF, sizeof s);
    std::memcpy(&s, &v8, sizeof v8);
    assert(migrate_store(s) == StoreLoad::Migrated);
    assert(s.version == 9);
    assert(std::memcmp(&s.slots[0], &v8.slots[0], 4 * sizeof(VoiceParams)) == 0);
    assert(std::memcmp(&s.slots[4], &f.slots[4], 2 * sizeof(VoiceParams)) == 0);
    assert(std::memcmp(&s.charge, &v8.charge, sizeof s.charge) == 0);
    assert(std::memcmp(&s.chord, &v8.chord, sizeof s.chord) == 0);
  }

  // ---- v7 image: v8's layout without the chord tail. Slots and charge
  // survive; slots 4..5 and chord become factory.
  {
    const VoiceStore f = factory_store();
    VoiceStoreV8 v7{};
    v7.version = 7;
    for (int i = 0; i < 4; i++) v7.slots[i] = f.slots[i];
    v7.slots[1].f1 = 777.0f;
    v7.charge = f.charge;
    v7.charge.gain = 2;
    VoiceStore s;
    std::memset(&s, 0xFF, sizeof s);
    std::memcpy(&s, &v7, offsetof(VoiceStoreV8, chord));
    assert(migrate_store(s) == StoreLoad::Migrated);
    assert(s.version == 9);
    assert(std::memcmp(&s.slots[0], &v7.slots[0], 4 * sizeof(VoiceParams)) == 0);
    assert(std::memcmp(&s.slots[4], &f.slots[4], 2 * sizeof(VoiceParams)) == 0);
    assert(std::memcmp(&s.charge, &v7.charge, sizeof s.charge) == 0);
    const ChordParams c = factory_chord();
    assert(std::memcmp(&s.chord, &c, sizeof c) == 0);
  }

  // ---- anything else resets to factory
  for (uint32_t bad : {0u, 6u, 10u, 0xFFFFFFFFu}) {
    VoiceStore s = factory_store();
    s.slots[0].f1 = 1.0f;
    s.version = bad;
    assert(migrate_store(s) == StoreLoad::Reset);
    const VoiceStore f = factory_store();
    assert(std::memcmp(&s, &f, sizeof s) == 0);
  }

  std::printf("test_store OK\n");
  return 0;
}
