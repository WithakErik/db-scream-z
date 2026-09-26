// test_chord_map.cpp - chord mode knob ranges/tapers, the ChordParams ->
// ChordEngineParams bridge, and the chord charge overlay (chord mode spec
// section 3).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "chord_map.hpp"

static bool near(double a, double b, double eps = 1e-4) {
  return std::fabs(a - b) <= eps;
}

int main() {
  // ---- menu 1: the four shared knobs match normal mode exactly
  {
    ChordParams c = factory_chord();
    VoiceParams v = factory_voice(0);
    for (float t : {0.0f, 0.3f, 0.5f, 1.0f}) {
      apply_chord_knob(ChordLayer::Main, 0, t, c);
      apply_knob(MenuLayer::Menu1, 0, t, v);
      assert(c.vocal_vol == v.vocal_vol);
      apply_chord_knob(ChordLayer::Main, 1, t, c);
      apply_knob(MenuLayer::Menu1, 1, t, v);
      assert(c.mix == v.mix);
      apply_chord_knob(ChordLayer::Main, 2, t, c);
      apply_knob(MenuLayer::Menu1, 2, t, v);
      assert(c.master_vol == v.master_vol);
      apply_chord_knob(ChordLayer::Main, 3, t, c);
      apply_knob(MenuLayer::Menu1, 3, t, v);
      assert(c.tone == v.tone);
    }
  }

  // ---- menu 1 knobs 5-6: sensitivity 0..8 cubic, drive 1..40 log
  {
    ChordParams c = factory_chord();
    apply_chord_knob(ChordLayer::Main, 4, 0.0f, c); assert(c.sensitivity == 0.0f);
    apply_chord_knob(ChordLayer::Main, 4, 1.0f, c); assert(near(c.sensitivity, 8.0));
    apply_chord_knob(ChordLayer::Main, 4, 0.5f, c); assert(near(c.sensitivity, 1.0));
    apply_chord_knob(ChordLayer::Main, 5, 0.0f, c); assert(near(c.drive, 1.0));
    apply_chord_knob(ChordLayer::Main, 5, 1.0f, c); assert(near(c.drive, 40.0));
    apply_chord_knob(ChordLayer::Main, 5, 0.5f, c); assert(near(c.drive, std::sqrt(40.0)));
  }

  // ---- chord menu: vowels 0..4 linear, size as normal mode, resonance
  // 0..1, attack 1..50 cubic, release 20..500 cubic
  {
    ChordParams c = factory_chord();
    apply_chord_knob(ChordLayer::Menu, 0, 0.0f, c); assert(c.closed_vowel == 0.0f);
    apply_chord_knob(ChordLayer::Menu, 0, 1.0f, c); assert(c.closed_vowel == 4.0f);
    apply_chord_knob(ChordLayer::Menu, 1, 0.5f, c); assert(c.open_vowel == 2.0f);
    apply_chord_knob(ChordLayer::Menu, 2, 0.51f, c); assert(c.vocal_size == map_vocal_size(0.51f));
    apply_chord_knob(ChordLayer::Menu, 3, 0.25f, c); assert(c.resonance == 0.25f);
    apply_chord_knob(ChordLayer::Menu, 4, 0.0f, c); assert(c.attack_ms == 1.0f);
    apply_chord_knob(ChordLayer::Menu, 4, 1.0f, c); assert(near(c.attack_ms, 50.0));
    apply_chord_knob(ChordLayer::Menu, 5, 0.0f, c); assert(c.release_ms == 20.0f);
    apply_chord_knob(ChordLayer::Menu, 5, 1.0f, c); assert(near(c.release_ms, 500.0));
    // out-of-range knob indices are ignored
    const ChordParams before = c;
    apply_chord_knob(ChordLayer::Menu, 6, 0.9f, c);
    assert(std::memcmp(&c, &before, sizeof c) == 0);
  }

  // ---- resonance -> bandwidth scale: 2.0 soft .. 0.35 sharp, log
  {
    assert(near(map_bw_scale(0.0f), 2.0));
    assert(near(map_bw_scale(1.0f), 0.35));
    assert(near(map_bw_scale(0.5f), std::sqrt(2.0 * 0.35)));
  }

  // ---- bridge
  {
    ChordParams c = factory_chord();
    c.vocal_size = 1.0f;
    c.gate_level = 2;
    ChordEngineParams p = to_chord_engine_params(c, true, 4.0);
    assert(p.mouth_open && p.input_gain == 4.0);
    assert(near(p.formant_scale, formant_scale_from(1.0f)));
    assert(near(p.gate, kGateLevels[2]));
    assert(near(p.drive, c.drive) && near(p.sensitivity, c.sensitivity));
    assert(near(p.bw_scale, map_bw_scale(c.resonance)));
    assert(p.gain == 1.0);  // levels live in the post chain
  }

  // ---- charge overlay: gain/tone/size rows as apply_charge, pitch ignored
  {
    const ChordParams base = factory_chord();
    ChargeConfig cfg = factory_charge_config();
    ChordParams z = apply_charge_chord(base, cfg, 0.0f);
    assert(std::memcmp(&z, &base, sizeof z) == 0);  // level 0 = identity

    VoiceParams vb = factory_voice(0);
    vb.vocal_vol = base.vocal_vol; vb.tone = base.tone; vb.vocal_size = base.vocal_size;
    for (float lv : {0.25f, 1.0f}) {
      ChordParams c = apply_charge_chord(base, cfg, lv);
      VoiceParams v = apply_charge(vb, cfg, lv, true);
      assert(c.vocal_vol == v.vocal_vol);
      assert(c.tone == v.tone);
      assert(c.vocal_size == v.vocal_size);
      assert(c.drive == base.drive && c.open_vowel == base.open_vowel);
    }
  }

  std::printf("test_chord_map OK\n");
  return 0;
}
