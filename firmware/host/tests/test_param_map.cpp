// test_param_map.cpp - voice_params.hpp + param_map.hpp
#include <cassert>
#include <cmath>
#include <cstdio>
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
  assert(near(s.slots[0].a1, 1.f) && near(s.slots[0].vib_jitter, 0.10f));
  assert(near(s.slots[0].mix, 1.0f));     // factory full wet (plan: resolved items)
  assert(near(s.slots[0].master_vol, 1.0f) && near(s.slots[0].vocal_vol, 1.0f));
  assert(near(s.slots[0].drive, 0.f) && near(s.slots[0].tone, 0.f));
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

  // ---- apply_knob ranges (lab-authoritative, plan: resolved items)
  VoiceParams v = factory_voice(0);
  apply_knob(MenuLayer::Menu1, 0, 0.25f, v); assert(near(v.mix, 0.25f));
  apply_knob(MenuLayer::Menu1, 1, 1.0f, v);  assert(near(v.glide_ms, 300.f));
  apply_knob(MenuLayer::Menu1, 2, 0.5f, v);  assert(near(v.master_vol, 1.0f));
  apply_knob(MenuLayer::Menu1, 3, 1.0f, v);  assert(near(v.vocal_vol, 2.0f));
  apply_knob(MenuLayer::Menu1, 4, 0.5f, v);  assert(near(v.drive, 0.5f));
  apply_knob(MenuLayer::Menu1, 5, 0.5f, v);  assert(v.tone == 0.0f);
  apply_knob(MenuLayer::Menu2, 0, 0.0f, v);  assert(near(v.f1, 200.f));
  apply_knob(MenuLayer::Menu2, 1, 1.0f, v);  assert(near(v.bw1, 300.f));
  apply_knob(MenuLayer::Menu2, 2, 0.5f, v);  assert(near(v.a1, 1.0f));
  apply_knob(MenuLayer::Menu2, 3, 1.0f, v);  assert(near(v.f2, 2600.f));
  apply_knob(MenuLayer::Menu2, 4, 1.0f, v);  assert(near(v.bw2, 400.f));
  apply_knob(MenuLayer::Menu2, 5, 1.0f, v);  assert(near(v.a2, 2.0f));
  apply_knob(MenuLayer::Menu3, 0, 1.0f, v);  assert(near(v.f3, 4500.f));
  apply_knob(MenuLayer::Menu3, 1, 1.0f, v);  assert(near(v.bw3, 500.f));
  apply_knob(MenuLayer::Menu3, 2, 0.0f, v);  assert(near(v.a3, 0.0f));
  apply_knob(MenuLayer::Menu3, 3, 0.5f, v);  assert(near(v.vib_rate, 7.0f));
  apply_knob(MenuLayer::Menu3, 4, 0.5f, v);  assert(near(v.vib_depth, 2.0f));
  apply_knob(MenuLayer::Menu3, 5, 0.5f, v);  assert(near(v.vib_jitter, 0.3f));

  printf("test_param_map OK\n");
  return 0;
}
