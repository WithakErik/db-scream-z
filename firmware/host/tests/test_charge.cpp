// test_charge.cpp - charge mode overlay + config data (charge spec
// sections 3-5 and 7). The overlay is a pure function: the edit buffer
// is never written, the caller feeds the returned copy to the engine.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "charge.hpp"

static bool near(float a, float b, float eps = 1e-4f) {
  return std::fabs(a - b) <= eps;
}

int main() {
  const VoiceParams base = factory_voice(0);  // Wukong: drive 0, tone 0,
                                              // octave 0, vocal_vol 1

  // ---- store schema: v2 carries the factory charge config
  {
    VoiceStore s = factory_store();
    assert(s.version == 3 && kVoiceStoreVersion == 3);
    assert(s.charge.gain == 1);   // on
    assert(s.charge.time == 1);   // Hamekameka
    assert(s.charge.decay == 1);  // slow
    assert(s.charge.pitch == 1);  // fall
    assert(s.charge.tone == 1);   // darker
    assert(s.charge.aspir == 1);  // low
    assert(s.charge.pad_[0] == 0 && s.charge.pad_[1] == 0);
  }

  // ---- level 0 is a bit-exact identity, whatever the config
  {
    ChargeConfig c = factory_charge_config();
    c.gain = 2; c.pitch = 2; c.tone = 2; c.aspir = 2;
    VoiceParams v = apply_charge(base, c, 0.0f, true);
    assert(std::memcmp(&v, &base, sizeof v) == 0);
  }

  // ---- gain on: x1.5 vocal and +0.3 drive at full charge, scaled by level
  {
    ChargeConfig c{};  // all off
    c.gain = 1;
    VoiceParams v = apply_charge(base, c, 1.0f, true);
    assert(near(v.vocal_vol, 1.5f));
    assert(near(v.drive, 0.3f));
    v = apply_charge(base, c, 0.5f, true);
    assert(near(v.vocal_vol, 1.25f));
    assert(near(v.drive, 0.15f));
    assert(near(v.tone, base.tone));  // other dimensions untouched
    assert(v.octave == base.octave);
  }

  // ---- gain Above 9000!: drive pinned to 1, vocal x2 (clamped at 2)
  {
    ChargeConfig c{};
    c.gain = 2;
    VoiceParams v = apply_charge(base, c, 1.0f, true);
    assert(near(v.drive, 1.0f));
    assert(near(v.vocal_vol, 2.0f));
  }

  // ---- pitch rise while charging: octave +1 with the long sweep glide
  {
    ChargeConfig c{};
    c.pitch = 2;
    VoiceParams v = apply_charge(base, c, 0.5f, true);
    assert(v.octave == 1);
    assert(near(v.glide_ms, 2000.0f));
  }

  // ---- pitch fall while charging: octave -1
  {
    ChargeConfig c{};
    c.pitch = 1;
    VoiceParams v = apply_charge(base, c, 0.5f, true);
    assert(v.octave == -1);
  }

  // ---- pitch saturates: a +1 voice charging with rise stays +1, no
  // glide override either
  {
    VoiceParams hi = base;
    hi.octave = 1;
    ChargeConfig c{};
    c.pitch = 2;
    VoiceParams v = apply_charge(hi, c, 1.0f, true);
    assert(v.octave == 1);
    assert(near(v.glide_ms, hi.glide_ms));
  }

  // ---- pitch during decay: octave back home, glide = the decay time
  {
    ChargeConfig c{};
    c.pitch = 2;
    c.decay = 1;  // slow, 1200 ms
    VoiceParams v = apply_charge(base, c, 0.7f, false);
    assert(v.octave == base.octave);
    assert(near(v.glide_ms, 1200.0f));
  }

  // ---- tone: ramps toward +1 (brighter) / -1 (darker) by level
  {
    ChargeConfig c{};
    c.tone = 2;
    VoiceParams v = apply_charge(base, c, 0.5f, true);
    assert(near(v.tone, 0.5f));  // 0 -> +1 at half level
    c.tone = 1;
    v = apply_charge(base, c, 0.5f, true);
    assert(near(v.tone, -0.5f));
  }

  // ---- aspiration high: +0.625 at full charge, low: +0.25, clamped to 1
  {
    ChargeConfig c{};
    c.aspir = 2;
    VoiceParams v = apply_charge(base, c, 1.0f, true);
    assert(near(v.aspiration, base.aspiration + 0.625f));
    c.aspir = 1;
    v = apply_charge(base, c, 1.0f, true);
    assert(near(v.aspiration, base.aspiration + 0.25f));
    c.aspir = 2;
    VoiceParams big = base;
    big.aspiration = 0.8f;
    v = apply_charge(big, c, 1.0f, true);
    assert(near(v.aspiration, 1.0f));  // clamped

    // The ramp is quantised to 1/32 so it does not re-dirty the grain
    // tables on every pass: the added breath must land ON the grid, and
    // sweeping the whole level range must produce far fewer distinct
    // values than levels sampled.
    v = apply_charge(base, c, 0.5f, true);
    assert(near(v.aspiration, base.aspiration + 10.0f / 32.0f));  // 0.3125
    int changes = 0;
    float prev = -1.0f;
    for (int i = 0; i <= 1000; ++i) {
      v = apply_charge(base, c, i / 1000.0f, true);
      // every value is base plus an exact 1/32 step
      const float add = v.aspiration - base.aspiration;
      assert(near(add * 32.0f, std::floor(add * 32.0f + 0.5f)));
      if (v.aspiration != prev) changes++;
      prev = v.aspiration;
    }
    assert(changes <= 24);  // ~21 steps over the 0.625 range, not 1001
  }

  printf("test_charge OK\n");
  return 0;
}
