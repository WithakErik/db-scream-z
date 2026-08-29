// test_grain.cpp
#include "grain.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static std::vector<float> load_f32(const char* p) {
  FILE* f = std::fopen(p, "rb"); assert(f);
  std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  std::vector<float> v(sz / 4);
  assert(std::fread(v.data(), 4, v.size(), f) == v.size());
  std::fclose(f); return v;
}

static float grains_buf[kMaxUnison][kMaxGrainLen];

int main() {
  FofParams p;                       // Wukong v12 (baked formants)
  p.f1 = 858.4; p.f2 = 1234.0; p.f3 = 3111.7; p.formant_scale = 1.0;
  int n = build_grains(p, 48000.0, grains_buf);
  assert(n == 960);
  std::vector<float> ref = load_f32("../../artifacts/ref/grains_Wukong.f32");
  assert((int)ref.size() == 8 * n);
  double maxerr = 0;
  for (int u = 0; u < 8; u++)
    for (int i = 0; i < n; i++)
      maxerr = std::max(maxerr, (double)std::fabs(grains_buf[u][i] - ref[u * n + i]));
  std::printf("grain maxerr %.3g\n", maxerr);
  assert(maxerr < 2e-6);             // float storage, double math
  // exercise the aspiration branch (no reference; just finite + differs)
  p.aspiration = 0.5;
  build_grains(p, 48000.0, grains_buf);
  assert(std::isfinite(grains_buf[0][100]) && grains_buf[0][100] != ref[100]);
  // grain_ms beyond the lab UI's cap must clamp n to kMaxGrainLen instead of
  // overflowing the fixed-size grains buffer.
  p.grain_ms = 150;
  int n_clamped = build_grains(p, 48000.0, grains_buf);
  assert(n_clamped == kMaxGrainLen);
  assert(std::isfinite(grains_buf[0][kMaxGrainLen - 1]));
  std::puts("test_grain OK");
  return 0;
}
