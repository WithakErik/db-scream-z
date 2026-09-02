// test_param_map.cpp - voice_params.hpp + param_map.hpp
#include <cassert>
#include <cmath>
#include <cstdio>
#include "knob_pickup.hpp"
#include "param_map.hpp"
#include "voice_params.hpp"

static bool near(float a, float b, float eps = 1e-4f) {
  return std::fabs(a - b) <= eps;
}

int main() {
  // ---- factory store (spec sec 6): Set1 = Wukong R / Prince L, Set2 = Rice R / Piccolo L
  VoiceStore s = factory_store();
  assert(s.version == kVoiceStoreVersion);
  assert(near(s.slots[0].f1, 858.4f));    // Wukong
  assert(near(s.slots[1].f1, 741.6f));    // Prince
  assert(near(s.slots[2].f1, 1000.0f));   // Rice
  assert(near(s.slots[3].f1, 697.6f));    // Piccolo
  assert(near(s.slots[0].bw1, 32.5f) && near(s.slots[0].bw2, 47.5f) &&
         near(s.slots[0].bw3, 62.5f));    // v12 defaults
  assert(near(s.slots[0].a1, 1.f));
  // Per-character voice stack (gen_presets.py STACK): Wukong keeps the v12
  // reference 3 / 11, Piccolo is the thick end of the range. Every
  // character carries the engine's long-standing 20 ms grain length, so
  // exposing it on knob 6 moved nobody's voice.
  assert(near(s.slots[0].unison, 3.f) && near(s.slots[0].detune_cents, 11.f) &&
         near(s.slots[0].grain_ms, 20.0f));
  assert(near(s.slots[3].unison, 5.f) && near(s.slots[3].detune_cents, 26.f) &&
         near(s.slots[3].grain_ms, 20.0f));
  assert(near(s.slots[0].mix, 1.0f));     // factory full wet (plan: resolved items)
  assert(near(s.slots[0].master_vol, 1.0f) && near(s.slots[0].vocal_vol, 1.0f));
  assert(near(s.slots[0].vocal_size, 0.f) && near(s.slots[0].tone, 0.f));
  assert(s.slots[0].octave == 0 && s.slots[0].gate_level == 1);
  VoiceStore t = s;
  assert(!(s != t));
  t.slots[3].tone = 0.5f;
  assert(s != t);

  // ---- slot_index
  assert(slot_index(Page::Set1, Side::Right) == 0);
  assert(slot_index(Page::Set1, Side::Left) == 1);
  assert(slot_index(Page::Set2, Side::Right) == 2);
  assert(slot_index(Page::Set2, Side::Left) == 3);
  assert(slot_index(Page::Freeform, Side::Right) == -1);

  // ---- gate levels: placeholders bracketing v12's 0.02
  assert(kGateLevels[0] < kGateLevels[1] && kGateLevels[1] < kGateLevels[2]);
  assert(near(kGateLevels[1], 0.02f));

  // ---- map_cube glide taper: 100 ms lands at ~69% of travel
  assert(near(map_cube(0.f, 0.f, 300.f), 0.f));
  assert(near(map_cube(1.f, 0.f, 300.f), 300.f));
  assert(std::fabs(map_cube(0.693f, 0.f, 300.f) - 100.f) < 2.f);

  // ---- map_tone: center detent is exactly 0, ends reach -1/+1
  assert(map_tone(0.5f) == 0.0f);
  assert(map_tone(0.52f) == 0.0f);   // inside detent band
  assert(map_tone(0.48f) == 0.0f);
  assert(map_tone(1.0f) > 0.999f);
  assert(map_tone(0.0f) < -0.999f);
  assert(map_tone(0.56f) > 0.0f && map_tone(0.44f) < 0.0f);

  // ---- menu 1 is: vocal vol, mix, master, tone, glide, vocal size
  {
    VoiceParams v{};
    apply_knob(MenuLayer::Menu1, 0, 0.5f, v);
    assert(near(v.vocal_vol, 1.0f));
    apply_knob(MenuLayer::Menu1, 1, 0.25f, v);
    assert(near(v.mix, 0.25f));
    apply_knob(MenuLayer::Menu1, 2, 1.0f, v);
    assert(near(v.master_vol, 2.0f));
    apply_knob(MenuLayer::Menu1, 3, 0.5f, v);
    assert(v.tone == 0.0f);              // centre detent, exact
    apply_knob(MenuLayer::Menu1, 4, 1.0f, v);
    assert(near(v.glide_ms, 300.0f));
    apply_knob(MenuLayer::Menu1, 5, 1.0f, v);
    assert(v.vocal_size == 1.0f);        // exact at the stop
  }

  // ---- vocal size endpoints are exactly reachable
  {
    VoiceParams v{};
    apply_knob(MenuLayer::Menu1, 5, 0.0f, v);
    assert(v.vocal_size == 0.0f);
    apply_knob(MenuLayer::Menu1, 5, 1.0f, v);
    assert(v.vocal_size == 1.0f);
  }

  // ---- vocal size is quantised to 32 steps, so a knob sweep cannot
  // trigger a grain rebuild per audio block (design spec section 8)
  {
    float seen[64];
    int n = 0;
    for (int i = 0; i <= 1000; i++) {
      VoiceParams v{};
      apply_knob(MenuLayer::Menu1, 5, (float)i / 1000.0f, v);
      bool dup = false;
      for (int k = 0; k < n; k++) if (seen[k] == v.vocal_size) dup = true;
      if (!dup) { assert(n < 64); seen[n++] = v.vocal_size; }
    }
    assert(n <= 33);   // 32 steps plus both endpoints landing on grid
  }

  // ---- vocal_size to formant_scale: endpoints exact, and monotonic down
  {
    assert(formant_scale_from(0.0f) == 1.0f);
    assert(formant_scale_from(1.0f) == kMinFormantScale);
    float prev = 2.0f;
    for (int i = 0; i <= 100; i++) {
      const float s = formant_scale_from((float)i / 100.0f);
      assert(s < prev);          // strictly deeper as the knob comes up
      assert(s <= 1.0f && s >= kMinFormantScale);
      prev = s;
    }
  }

  // ---- apply_knob ranges (lab-authoritative, plan: resolved items)
  VoiceParams v = factory_voice(0);
  apply_knob(MenuLayer::Menu2, 0, 0.0f, v);  assert(near(v.f1, 200.f));
  apply_knob(MenuLayer::Menu2, 1, 1.0f, v);  assert(near(v.bw1, 300.f));
  apply_knob(MenuLayer::Menu2, 2, 0.5f, v);  assert(near(v.a1, 1.0f));
  apply_knob(MenuLayer::Menu2, 3, 1.0f, v);  assert(near(v.f2, 2600.f));
  apply_knob(MenuLayer::Menu2, 4, 1.0f, v);  assert(near(v.bw2, 400.f));
  apply_knob(MenuLayer::Menu2, 5, 1.0f, v);  assert(near(v.a2, 2.0f));
  apply_knob(MenuLayer::Menu3, 0, 1.0f, v);  assert(near(v.f3, 4500.f));
  apply_knob(MenuLayer::Menu3, 1, 1.0f, v);  assert(near(v.bw3, 500.f));
  apply_knob(MenuLayer::Menu3, 2, 0.0f, v);  assert(near(v.a3, 0.0f));
  apply_knob(MenuLayer::Menu3, 4, 0.5f, v);  assert(near(v.detune_cents, 30.0f));
  // Grain: center detent, so half travel is exactly the 20 ms default.
  apply_knob(MenuLayer::Menu3, 5, 0.5f, v);  assert(v.grain_ms == 20.0f);

  // ---- voices: eight equal bands over 1..8, both stops legal, monotonic
  apply_knob(MenuLayer::Menu3, 3, 0.0f, v);    assert(v.unison == 1.0f);
  apply_knob(MenuLayer::Menu3, 3, 1.0f, v);    assert(v.unison == 8.0f);
  apply_knob(MenuLayer::Menu3, 3, 0.124f, v);  assert(v.unison == 1.0f);
  apply_knob(MenuLayer::Menu3, 3, 0.126f, v);  assert(v.unison == 2.0f);
  apply_knob(MenuLayer::Menu3, 3, 0.5f, v);    assert(v.unison == 5.0f);
  {
    float prev = 0.0f;
    for (int i = 0; i <= 100; ++i) {
      apply_knob(MenuLayer::Menu3, 3, i / 100.0f, v);
      assert(v.unison >= prev && v.unison >= 1.0f && v.unison <= 8.0f);
      prev = v.unison;
    }
  }

  // ---- grain length: 20 ms must be exactly reachable, because it is the
  // value every factory character stores and the one the engine baked in
  // before this knob existed. A real pot never reads exactly 0.5, so the
  // whole detent band has to give 20, and the band must clear the
  // knob-pickup threshold (0.02) so a centred knob cannot drift off it.
  assert(map_grain(0.5f) == 20.0f);
  assert(map_grain(0.55f) == 20.0f);   // inside the detent band
  assert(map_grain(0.45f) == 20.0f);
  assert(KnobPickup::kThreshold < 0.1f);
  // Ends reach the lab's full 4..40 ms slider range, and the two halves
  // are scaled independently so the detent lands on 20 rather than the
  // 22.2 a single linear map would give.
  assert(near(map_grain(0.0f), 4.0f));
  assert(near(map_grain(1.0f), 40.0f));
  assert(map_grain(0.56f) > 20.0f && map_grain(0.44f) < 20.0f);
  {
    float prev = 0.0f;
    for (int i = 0; i <= 100; ++i) {
      const float g = map_grain(i / 100.0f);
      assert(g >= prev && g >= 4.0f && g <= 40.0f);
      prev = g;
    }
  }
  apply_knob(MenuLayer::Menu3, 5, 0.0f, v);   assert(near(v.grain_ms, 4.0f));
  apply_knob(MenuLayer::Menu3, 5, 1.0f, v);   assert(near(v.grain_ms, 40.0f));
  apply_knob(MenuLayer::Menu3, 4, 0.0f, v);   assert(near(v.detune_cents, 0.0f));
  apply_knob(MenuLayer::Menu3, 4, 1.0f, v);   assert(near(v.detune_cents, 60.0f));

  printf("test_param_map OK\n");
  return 0;
}
