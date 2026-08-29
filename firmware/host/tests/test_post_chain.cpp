// test_post_chain.cpp - milestone 5 voice post chain (spec section 5)
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "post_chain.hpp"

static bool near(float a, float b, float eps = 1e-5f) {
  return std::fabs(a - b) <= eps;
}

// RMS of the chain's voice-path response to a sine, after settling.
static float sine_rms(PostChain& pc, float freq, float sr) {
  double acc = 0.0;
  const int n = 4800;
  for (int i = 0; i < n; i++) {
    float x = std::sin(2.0f * (float)M_PI * freq * (float)i / sr);
    float y = pc.process(0.0f, x);  // dry 0, mix 1: voice path only
    if (i >= n / 2) acc += (double)y * y;
  }
  return (float)std::sqrt(acc / (n / 2));
}

int main() {
  const float sr = 48000.0f;

  // ---- neutral settings are bit-transparent on the voice
  {
    PostChain pc(sr);
    pc.set(0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    for (float x : {0.0f, 0.1f, -0.37f, 0.9f, -1.0f})
      assert(pc.process(0.5f, x) == x);  // exactly x, not just close
  }

  // ---- mix 0 is exactly the dry input (the bypass sound), voice ignored
  {
    PostChain pc(sr);
    pc.set(1.0f, -1.0f, 2.0f, 0.0f, 1.0f);
    for (float d : {0.0f, 0.25f, -0.8f})
      assert(pc.process(d, 0.9f) == d);
  }

  // ---- mix crossfade and master
  {
    PostChain pc(sr);
    pc.set(0.0f, 0.0f, 1.0f, 0.25f, 2.0f);
    // out = 2 * (dry*0.75 + voice*0.25)
    assert(near(pc.process(0.4f, 0.8f), 2.0f * (0.4f * 0.75f + 0.8f * 0.25f)));
  }

  // ---- vocal volume scales the voice only
  {
    PostChain pc(sr);
    pc.set(0.0f, 0.0f, 0.5f, 1.0f, 1.0f);
    assert(near(pc.process(0.9f, 0.6f), 0.3f));
  }

  // ---- drive: small signals near unity, big signals compressed, monotonic
  {
    PostChain pc(sr);
    pc.set(1.0f, 0.0f, 1.0f, 1.0f, 1.0f);  // g = 10
    float small_ = pc.process(0.0f, 0.01f);
    assert(std::fabs(small_ - 0.01f) < 0.01f * 0.05f);  // within 5% of unity
    float big = pc.process(0.0f, 0.9f);
    assert(big < 0.11f && big > 0.0f);  // ceiling 1/g = 0.1
    assert(pc.process(0.0f, 0.5f) < pc.process(0.0f, 0.9f) + 1e-6f);
  }

  // ---- drive 0 is an exact passthrough (unity at CCW), not tanh(x)
  {
    PostChain pc(sr);
    pc.set(0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    assert(pc.process(0.0f, 0.9f) == 0.9f);
  }

  // ---- tone: dark attenuates highs, bright boosts them, center flat
  {
    PostChain dark(sr), flat(sr), bright(sr);
    dark.set(0.0f, -1.0f, 1.0f, 1.0f, 1.0f);
    flat.set(0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    bright.set(0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    float hi_dark = sine_rms(dark, 6000.0f, sr);
    float hi_flat = sine_rms(flat, 6000.0f, sr);
    float hi_bright = sine_rms(bright, 6000.0f, sr);
    assert(hi_dark < 0.5f * hi_flat);    // well attenuated at 6 kHz
    assert(hi_bright > 1.3f * hi_flat);  // boosted at 6 kHz
    PostChain dark2(sr);
    dark2.set(0.0f, -1.0f, 1.0f, 1.0f, 1.0f);
    float lo_dark = sine_rms(dark2, 100.0f, sr);
    PostChain flat2(sr);
    flat2.set(0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    float lo_flat = sine_rms(flat2, 100.0f, sr);
    assert(near(lo_dark / lo_flat, 1.0f, 0.05f));  // lows survive full dark
  }

  printf("test_post_chain OK\n");
  return 0;
}
