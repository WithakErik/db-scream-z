// post_chain.hpp - milestone 5 voice-only post chain + mix/master (spec
// section 5). The dry guitar path is never touched: voice -> DRIVE -> TONE
// -> VOCAL VOL, then a dry/wet crossfade and MASTER on the sum. Zero added
// latency anywhere. Host-testable: no dependencies beyond <cmath>.
#pragma once
#include <cmath>

class PostChain {
 public:
  explicit PostChain(float sr)
      : c_lp_(1.0f - std::exp(-2.0f * 3.14159265f * 1500.0f / sr)) {}

  void set(float drive, float tone, float vocal_vol, float mix, float master) {
    drive_ = drive;
    tone_ = tone;
    vocal_ = vocal_vol;
    mix_ = mix;
    master_ = master;
  }

  // One sample. dry = raw input, voice = engine output.
  float process(float dry, float voice) {
    float v = voice;
    // DRIVE: tanh soft clip BEFORE any makeup gain (the lab's WaveShaper
    // lesson: the shaper must see pre-makeup levels; vocal_vol comes
    // after). drive == 0 is an exact passthrough (unity at CCW);
    // otherwise y = tanh(g x)/g: unity small-signal gain, ceiling 1/g.
    if (drive_ > 0.001f) {
      const float g = 1.0f + 9.0f * drive_;
      v = std::tanh(g * v) / g;
    }
    // TONE: one-pole tilt around a 1.5 kHz lowpass. tone 0 leaves the
    // sample untouched (bit-transparent center, spec requirement); -1 is
    // the pure lowpass (full dark); +1 adds the residual highs back on
    // top (bright). The lowpass state always runs so sweeping through
    // center does not step.
    lp_ += c_lp_ * (v - lp_);
    if (tone_ < 0.0f)
      v = v + (-tone_) * (lp_ - v);
    else if (tone_ > 0.0f)
      v = v + tone_ * (v - lp_);
    v *= vocal_;
    return master_ * (dry * (1.0f - mix_) + v * mix_);
  }

  void reset() { lp_ = 0.0f; }

 private:
  float c_lp_;
  float lp_ = 0.0f;
  float drive_ = 0.0f, tone_ = 0.0f, vocal_ = 1.0f, mix_ = 1.0f,
        master_ = 1.0f;
};
