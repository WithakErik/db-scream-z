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

  // ---- gain middle: HALFWAY from wherever the voice sits to the top of
  // each range, at full charge. base is clean (drive 0, vocal unity).
  {
    ChargeConfig c{};  // all off
    c.gain = 1;
    VoiceParams v = apply_charge(base, c, 1.0f, true);
    assert(near(v.drive, 0.5f));         // 0 -> halfway to 1
    assert(near(v.vocal_vol, 1.5f));     // 1 -> halfway to 2
    v = apply_charge(base, c, 0.5f, true);
    assert(near(v.drive, 0.25f));        // scaled by level
    assert(near(v.vocal_vol, 1.25f));
    assert(near(v.tone, base.tone));     // other dimensions untouched
    assert(v.octave == base.octave);
  }

  // ---- gain up: all the way to the top of both ranges at full charge
  {
    ChargeConfig c{};
    c.gain = 2;
    VoiceParams v = apply_charge(base, c, 1.0f, true);
    assert(near(v.drive, 1.0f));
    assert(near(v.vocal_vol, 2.0f));
  }

  // ---- the gap is measured from the VOICE, not from zero: a voice already
  // driven hard has less of it left, and neither setting can overshoot.
  {
    VoiceParams hot = base;
    hot.drive = 0.6f;
    hot.vocal_vol = 1.5f;
    ChargeConfig c{};
    c.gain = 1;
    VoiceParams v = apply_charge(hot, c, 1.0f, true);
    assert(near(v.drive, 0.8f));         // 0.6 -> halfway to 1
    assert(near(v.vocal_vol, 1.75f));    // 1.5 -> halfway to 2
    c.gain = 2;
    v = apply_charge(hot, c, 1.0f, true);
    assert(near(v.drive, 1.0f));
    assert(near(v.vocal_vol, 2.0f));
    // and a voice already at the ceiling simply has nothing to give
    VoiceParams maxed = base;
    maxed.drive = 1.0f;
    maxed.vocal_vol = 2.0f;
    v = apply_charge(maxed, c, 1.0f, true);
    assert(near(v.drive, 1.0f));
    assert(near(v.vocal_vol, 2.0f));
  }

  // ---- pitch rise while charging: TWO octaves up, with the sweep glide
  {
    ChargeConfig c{};
    c.pitch = 2;
    VoiceParams v = apply_charge(base, c, 0.5f, true);
    assert(v.octave == 2);
    assert(near(v.glide_ms, 2000.0f));
  }

  // ---- pitch fall while charging: two octaves down
  {
    ChargeConfig c{};
    c.pitch = 1;
    VoiceParams v = apply_charge(base, c, 0.5f, true);
    assert(v.octave == -2);
  }

  // ---- the result is CAPPED at +/-2 rather than cancelled: a voice whose
  // own octave toggle is already +1 keeps one octave of travel and still
  // gets the sweep glide.
  {
    VoiceParams hi = base;
    hi.octave = 1;
    ChargeConfig c{};
    c.pitch = 2;
    VoiceParams v = apply_charge(hi, c, 1.0f, true);
    assert(v.octave == 2);
    assert(near(v.glide_ms, 2000.0f));
    // and downward from the same voice still reaches the bottom cap
    c.pitch = 1;
    v = apply_charge(hi, c, 1.0f, true);
    assert(v.octave == -1);              // 1 - 2, inside the cap
    VoiceParams lo = base;
    lo.octave = -1;
    v = apply_charge(lo, c, 1.0f, true);
    assert(v.octave == -2);              // -1 - 2 = -3, capped
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

  // ---- aspiration: reaches a full 1.0 on up, halfway on middle, and the
  // gap is measured from the voice's own breath.
  {
    ChargeConfig c{};
    c.aspir = 2;
    VoiceParams v = apply_charge(base, c, 1.0f, true);
    assert(near(v.aspiration, 1.0f));            // base 0 -> full
    c.aspir = 1;
    v = apply_charge(base, c, 1.0f, true);
    assert(near(v.aspiration, 0.5f));            // base 0 -> halfway
    VoiceParams breathy = base;
    breathy.aspiration = 0.4f;
    v = apply_charge(breathy, c, 1.0f, true);
    assert(near(v.aspiration, 0.7f));            // 0.4 -> halfway to 1
    c.aspir = 2;
    v = apply_charge(breathy, c, 1.0f, true);
    assert(near(v.aspiration, 1.0f));

    // The ramp is stepped so it does not re-dirty the grain tables on
    // every pass, but the endpoints must stay exact and it must never
    // overshoot the top of the range.
    int changes = 0;
    float prev = -1.0f;
    for (int i = 0; i <= 1000; ++i) {
      v = apply_charge(base, c, i / 1000.0f, true);
      assert(v.aspiration >= 0.0f && v.aspiration <= 1.0f);
      if (v.aspiration != prev) changes++;
      prev = v.aspiration;
    }
    assert(changes <= 36);                       // ~33 steps, not 1001
  }

  printf("test_charge OK\n");
  return 0;
}
