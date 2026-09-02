// beast_presets.hpp - the hidden "beast" bank.
//
// Hold BOTH footswitches while powering the pedal on and the four DBZ
// characters are replaced by four animal calls for that session. Nothing
// is written to QSPI (main.cpp skips the save), so the saved characters
// are untouched and a power cycle brings them back.
//
// Deliberately NOT routed through tools/gen_presets.py like
// ../engine/presets.hpp is. A CharacterPreset carries only formants,
// unison, detune and grain length, and these voices need octave, glide and
// bandwidth to sound like anything: a cow is a cow because it is an
// octave down and dark, not because of its formant triple alone. Keeping
// the bank here means the generated file and the character path never
// have to know it exists.
//
// The engine is a formant resynth driven by the guitar's own pitch, which
// is why the bank is four SUSTAINED calls. It does vowel-like animal
// voices well and percussive ones (a bark, a quack) badly, because there
// is no per-attack amplitude shaper or noise burst anywhere in the chain.
//
// Every number below is a first-pass ear target, in the same spirit as
// the kGateLevels placeholders in voice_params.hpp: plausible on paper,
// due a tuning pass through a real amp. test_beast_presets.cpp asserts
// ranges and relationships rather than exact values so that pass does not
// mean rewriting the test.
#pragma once
#include <cstdint>

#include "voice_params.hpp"

// Slot order, matching slot_index(): 0 = Set1 R, 1 = Set1 L,
// 2 = Set2 R, 3 = Set2 L. So Set 1 is Cow / Wolf, Set 2 is Whale /
// Elephant, and toggle 2 still pages between them.
inline constexpr int kBeastCow = 0;
inline constexpr int kBeastWolf = 1;
inline constexpr int kBeastWhale = 2;
inline constexpr int kBeastElephant = 3;
inline constexpr int kBeastCount = 4;

// Only the fields that differ from factory_voice()'s shared defaults.
// Names are omitted on purpose: nothing on the pedal displays them, and
// internal flash is at 94%. The emulator carries them instead, where the
// panel actually has a label to fill (pedal/static/beast-presets.js).
// vocal_size is what makes an animal an ANIMAL rather than a person doing an
// impression: it scales the whole tract, so the formants move together and
// the creature reads as physically large. Values are on the knob's 32-step
// grid, so nudging knob 6 back to a beast's factory setting lands exactly.
// The beasts used to carry a per-beast drive value for grit; drive was
// retired from the pedal, and size is what replaces it.
struct BeastPreset {
  float f1, f2, f3;
  float bw1, bw2, bw3;
  float unison, detune_cents;
  float glide_ms, tone, vocal_size;
  int8_t octave;
};

inline constexpr BeastPreset kBeasts[kBeastCount] = {
    // Cow: a long tract, so low formants on an /o/, an octave down for
    // the body. Wide bandwidths blur the vowel into an animal, and the
    // glide is the slide a moo makes falling off its own note.
    {420.0f, 800.0f, 2400.0f, 60.0f, 90.0f, 120.0f,
     4.0f, 14.0f, 140.0f, -0.35f, 0.5f, -1},

    // Wolf: a pure sustained /u/, the stack doing the work. Six voices
    // at 22 cents is the beating that makes one howl sound like several
    // a valley away. The long glide swoops in.
    {350.0f, 850.0f, 2600.0f, 40.0f, 55.0f, 80.0f,
     6.0f, 22.0f, 220.0f, 0.10f, 0.25f, 0},

    // Whale: the lowest and most smeared voice in the bank, with the
    // glide pinned at its 300 ms ceiling so every note arrives by
    // sliding. Three voices spread wide beat slowly, like distance.
    {300.0f, 700.0f, 1800.0f, 70.0f, 100.0f, 150.0f,
     3.0f, 30.0f, 300.0f, -0.20f, 0.75f, -1},

    // Elephant: the outlier. Bright, tight and brassy rather than dark
    // and vocal, with barely any detune so the blast stays focused.
    // Short glide: a trumpet is an attack.
    {700.0f, 1900.0f, 3200.0f, 45.0f, 60.0f, 90.0f,
     2.0f, 7.0f, 45.0f, 0.55f, 0.375f, 0},
};

// One beast as a full edit buffer. Everything the table does not name
// keeps factory_voice()'s values: unity gains, full wet, medium gate.
inline VoiceParams beast_voice(int idx) {
  const BeastPreset& b = kBeasts[idx];
  VoiceParams v{};
  v.f1 = b.f1;
  v.f2 = b.f2;
  v.f3 = b.f3;
  v.bw1 = b.bw1;
  v.bw2 = b.bw2;
  v.bw3 = b.bw3;
  v.a1 = v.a2 = v.a3 = 1.0f;
  v.unison = b.unison;
  v.detune_cents = b.detune_cents;
  // Not in the table: every beast wants the engine's long-standing default
  // (FofParams::grain_ms). It has to be assigned explicitly all the same,
  // because VoiceParams v{} above zero-initialises it and a grain length of
  // 0 ms would floor to a 16-sample grain in build_grains().
  v.grain_ms = 20.0f;
  v.mix = 1.0f;
  v.glide_ms = b.glide_ms;
  v.master_vol = 1.0f;
  v.vocal_vol = 1.0f;
  v.vocal_size = b.vocal_size;
  v.tone = b.tone;
  v.octave = b.octave;
  v.gate_level = 1;  // medium
  v.pad_[0] = v.pad_[1] = 0;
  return v;
}

// The session store handed to ui.init() in place of the QSPI one.
//
// Charge mode still works on the animals exactly as it does on the
// characters (a charged wolf howl is the point of the whole thing), but
// the config is the FACTORY one rather than whatever is saved in QSPI.
// Beast mode is a self-contained sandbox: nothing it does survives the
// power cycle, so inheriting a half-remembered charge config would only
// make the same gesture sound different on different days.
inline VoiceStore beast_store() {
  VoiceStore s{};
  s.version = kVoiceStoreVersion;
  for (int i = 0; i < kBeastCount; ++i) s.slots[i] = beast_voice(i);
  s.charge = factory_charge_config();
  return s;
}
