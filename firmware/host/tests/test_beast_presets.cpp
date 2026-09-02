// test_beast_presets.cpp - the hidden "beast" bank: four sustained animal
// calls loaded in place of the four DBZ characters when both stomps are
// held at power-up (main.cpp). The bank is data, so this is a validity
// gate: every field inside the range voice_params.hpp documents for it,
// the store schema intact, and every voice distinct from every character
// so the emulator's voiceName() can tell them apart.
//
// The numbers themselves are first-pass ear targets, in the same spirit as
// the kGateLevels placeholders. This test deliberately asserts RANGES and
// RELATIONSHIPS, not exact values, so re-tuning by ear does not mean
// rewriting the test.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "beast_presets.hpp"

int main() {
  // ---- four voices, in the documented slot order
  assert(kBeastCount == 4);

  for (int i = 0; i < kBeastCount; ++i) {
    const VoiceParams v = beast_voice(i);

    // Formants ascend and are audible. The engine assumes f1 < f2 < f3.
    assert(v.f1 > 0.0f && v.f2 > v.f1 && v.f3 > v.f2);
    assert(v.f3 < 20000.0f);

    // Bandwidths ascend too, matching the character convention
    // (32.5 / 47.5 / 62.5 in factory_voice).
    assert(v.bw1 > 0.0f && v.bw2 >= v.bw1 && v.bw3 >= v.bw2);

    // Formant amplitudes: the bank does not touch them.
    assert(v.a1 == 1.0f && v.a2 == 1.0f && v.a3 == 1.0f);

    // Voice stack, per voice_params.hpp: 1..8 voices, 0..60 cents,
    // 4..40 ms grain. The bank leaves grain at the engine default, but it
    // must still be SET: beast_voice zero-initialises the block, and a
    // grain of 0 ms would floor to a 16-sample grain in build_grains.
    assert(v.unison >= 1.0f && v.unison <= 8.0f);
    assert(v.unison == std::floor(v.unison));  // whole voices only
    assert(v.detune_cents >= 0.0f && v.detune_cents <= 60.0f);
    assert(v.grain_ms >= 4.0f && v.grain_ms <= 40.0f);

    // Post chain and routing.
    assert(v.mix >= 0.0f && v.mix <= 1.0f);
    assert(v.glide_ms >= 0.0f && v.glide_ms <= 300.0f);
    assert(v.master_vol >= 0.0f && v.master_vol <= 2.0f);
    assert(v.vocal_vol >= 0.0f && v.vocal_vol <= 2.0f);
    assert(v.tone >= -1.0f && v.tone <= 1.0f);
    assert(v.vocal_size >= 0.0f && v.vocal_size <= 1.0f);
    // Every beast's size sits on knob 6's own 32-step grid, so nudging the
    // knob back to a beast's factory setting lands on it exactly rather
    // than one step either side of it.
    assert(v.vocal_size * 32.0f == std::floor(v.vocal_size * 32.0f));

    // Octave is the toggle's own range; charge may push past it later.
    assert(v.octave >= -1 && v.octave <= 1);
    assert(v.gate_level <= 2);

    // memcmp comparability (VoiceStore::operator!=) needs zero padding.
    assert(v.pad_[0] == 0 && v.pad_[1] == 0);
  }

  // ---- the four are distinct from each other
  for (int i = 0; i < kBeastCount; ++i) {
    for (int j = i + 1; j < kBeastCount; ++j) {
      const VoiceParams a = beast_voice(i), b = beast_voice(j);
      assert(std::memcmp(&a, &b, sizeof a) != 0);
    }
  }

  // ---- and distinct from every DBZ character, on the formant triple.
  // voiceName() in the emulator identifies a voice by that triple within
  // 0.05 Hz, so a collision here would label a beast as a character.
  for (int i = 0; i < kBeastCount; ++i) {
    const VoiceParams b = beast_voice(i);
    for (int p = 0; p < 4; ++p) {
      const VoiceParams c = factory_voice(p);
      const bool same = std::fabs(b.f1 - c.f1) < 0.05f &&
                        std::fabs(b.f2 - c.f2) < 0.05f &&
                        std::fabs(b.f3 - c.f3) < 0.05f;
      assert(!same);
    }
  }

  // ---- the store: same schema, same charge config, beast slots
  {
    const VoiceStore s = beast_store();
    assert(s.version == kVoiceStoreVersion);

    // Charge mode is untouched by the bank: the animals still power up.
    const ChargeConfig f = factory_charge_config();
    assert(std::memcmp(&s.charge, &f, sizeof f) == 0);

    // Slot order is the documented one: 0 = Set1 R, 1 = Set1 L,
    // 2 = Set2 R, 3 = Set2 L (slot_index in voice_params.hpp).
    for (int i = 0; i < kBeastCount; ++i) {
      const VoiceParams v = beast_voice(i);
      assert(std::memcmp(&s.slots[i], &v, sizeof v) == 0);
    }

    // A beast store must never compare equal to the factory one, or a
    // save in beast mode would look like a no-op to PersistentStorage.
    const VoiceStore fs = factory_store();
    assert(s != fs);
  }

  // ---- the character identities the four are meant to have. These are
  // the only value assertions, and each is the defining trait of its
  // animal rather than a transcription of the table.
  {
    const VoiceParams cow = beast_voice(kBeastCow);
    assert(cow.octave == -1);   // a cow is a big animal
    assert(cow.tone < 0.0f);    // and a dark one

    const VoiceParams wolf = beast_voice(kBeastWolf);
    assert(wolf.unison >= 5.0f);    // the pack shimmer
    assert(wolf.glide_ms >= 150.0f);  // a howl swoops into its note

    const VoiceParams whale = beast_voice(kBeastWhale);
    assert(whale.octave == -1);
    assert(whale.glide_ms >= 250.0f);  // the longest slide in the bank
    assert(whale.f1 < cow.f1);         // and the lowest voice in it

    const VoiceParams eleph = beast_voice(kBeastElephant);
    assert(eleph.tone > 0.0f);   // brassy, not dark
    assert(eleph.f2 > wolf.f2);  // and sit far brighter than a howl

    // Size is what tells the animals apart from a person imitating them,
    // and it is the bank's replacement for the per-beast drive that went
    // when the pedal lost its distortion. The ordering is the point: a
    // whale is the largest thing in the bank and a wolf is dog sized, so
    // the wolf's lowness has to come from its formants and its howl
    // rather than from pretending it has a whale's throat.
    assert(whale.vocal_size > cow.vocal_size);
    assert(cow.vocal_size > eleph.vocal_size);
    assert(eleph.vocal_size > wolf.vocal_size);
    assert(wolf.vocal_size > 0.0f);   // every beast is bigger than a person
  }

  std::printf("test_beast_presets OK\n");
  return 0;
}
