// test_wav_io.cpp
#include "../wav_io.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
  // roundtrip a 1 kHz sine, 480 samples @ 48k
  std::vector<float> x(480);
  for (int i = 0; i < 480; i++) x[i] = 0.5f * std::sin(2 * M_PI * 1000.0 * i / 48000.0);
  wav_write_mono("/tmp/wavio_rt.wav", 48000, x);
  WavData r = wav_read_mono("/tmp/wavio_rt.wav");
  assert(r.sample_rate == 48000);
  assert(r.samples.size() == 480);
  for (int i = 0; i < 480; i++) assert(std::fabs(r.samples[i] - x[i]) < 1.0 / 32768 + 1e-7);
  // read the real reference clip
  WavData g = wav_read_mono("../../dbscreamz_lab/static/audio/guitar_long.wav");
  assert(g.sample_rate == 48000);
  assert(g.samples.size() == 48000u * 35);
  std::puts("test_wav_io OK");
  return 0;
}
