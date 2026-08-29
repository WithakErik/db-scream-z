// test_front_end.cpp
#include "front_end.hpp"
#include "../wav_io.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
  WavData g = wav_read_mono("../../dbscreamz_lab/static/audio/guitar_long.wav");
  LiveFrontEnd fe(48000.0);
  FILE* ref = std::fopen("../../artifacts/ref/frontend_Wukong.csv", "r");
  assert(ref);
  char hdr[128]; assert(std::fgets(hdr, sizeof hdr, ref));
  long sample; double envFast, liveAmp, gateGain;
  long i = 0; int rows = 0; double maxerr = 0;
  while (std::fscanf(ref, "%ld,%lf,%lf,%lf", &sample, &envFast, &liveAmp, &gateGain) == 4) {
    for (; i <= sample; i++) fe.process(g.samples[i] * 4.0, 0.02);  // inputGain 4, gate 0.02
    maxerr = std::max({maxerr, std::fabs(fe.env_fast() - envFast),
                       std::fabs(fe.live_amp() - liveAmp),
                       std::fabs(fe.gate_gain() - gateGain)});
    rows++;
  }
  std::fclose(ref);
  std::printf("front-end rows %d maxerr %.3g\n", rows, maxerr);
  assert(rows > 13000);          // 35 s of 128-sample blocks
  assert(maxerr < 1e-9);
  std::puts("test_front_end OK");
  return 0;
}
