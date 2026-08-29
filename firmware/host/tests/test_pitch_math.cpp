#include "pitch_math.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
  FILE* f = std::fopen("../../artifacts/ref/pitch_math.csv", "r");
  assert(f);
  double in, q, ac; int rows = 0;
  while (std::fscanf(f, "%lf,%lf,%lf", &in, &q, &ac) == 3) {
    rows++;
    double cq = quantize_hz(in, true);
    assert(std::fabs(cq - q) < 1e-9 * q);
    assert(std::fabs(amp_comp_gain(in) - ac) < 1e-12);
  }
  std::fclose(f);
  assert(rows == 641);
  assert(quantize_hz(123.4, false) == 123.4);
  FofParams p;                              // octave_shift 0 default
  assert(register_map(200.0, p) == 200.0);
  p.octave_shift = 2;  assert(std::fabs(register_map(200.0, p) - 800.0) < 1e-12);
  p.octave_shift = -3; assert(std::fabs(register_map(200.0, p) - 25.0) < 1e-12);
  p.follow_pitch = false; assert(register_map(200.0, p) == p.target_f0);
  p.follow_pitch = true;  assert(register_map(39.9, p) == p.target_f0);
  std::puts("test_pitch_math OK");
  return 0;
}
