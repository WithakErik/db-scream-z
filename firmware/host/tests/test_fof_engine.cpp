// test_fof_engine.cpp - guards the synth loop against silent drift.
#include <cassert>
#include <cmath>
#include <cstdio>
#include "fof_engine.hpp"

static FofParams base_params() {
  FofParams p;
  p.follow_pitch = false;
  p.target_f0 = 164.8;
  p.input_gain = 1.0;
  p.gate = 0.02;
  p.quantize = false;
  p.unison = 1;
  p.glide_ms = 0;
  return p;
}

static void run(const FofParams& p, float* out, int blocks) {
  FofEngine eng(48000.0);
  eng.set_params(p);
  eng.rebuild_grains_if_dirty();
  float in[48], ob[48];
  int n = 0;
  for (int b = 0; b < blocks; b++) {
    for (int i = 0; i < 48; i++, n++)
      in[i] = (float)(0.3 * std::sin(2.0 * M_PI * 300.0 * n / 48000.0));
    eng.rebuild_grains_if_dirty();
    eng.process_block(in, ob, 48);
    for (int i = 0; i < 48; i++) out[b * 48 + i] = ob[i];
  }
}

// Captured from the engine BEFORE the LFO was deleted, with vib_rate and
// vib_depth at 0 (the firmware's own values). Deleting dead code must not
// move a single sample.
static const int kGoldenIdx[] = {9600,9737,9874,10011,10148,10285,
                                 10422,10559,10696,10833,10970,11107};
static const float kGolden[] = {
  0.0232518315f, -0.0630833954f, -0.0337015167f, 0.0289526209f,
  -0.065220736f, -0.0632739738f, 0.0747298524f, 0.0326967873f,
  0.0236229375f, 0.0827101693f, 0.00832190178f, 0.0811890364f
};

int main() {
  static float buf[19200];
  run(base_params(), buf, 400);
  for (int k = 0; k < 12; k++) assert(buf[kGoldenIdx[k]] == kGolden[k]);
  printf("test_fof_engine OK\n");
  return 0;
}
