#pragma once
#include <algorithm>
#include <cmath>

// Transcription of the envelope/gate half of liveSample()
// (fof-processor.js lines 823-838) + the constructor state (555-564).
// Input x is POST-inputGain. Returns nothing per call; read the accessors.
class LiveFrontEnd {
 public:
  explicit LiveFrontEnd(double sr)
      : c_env_a_(1 - std::exp(-1 / (0.006 * sr))),
        c_env_r_(1 - std::exp(-1 / (0.080 * sr))),
        c_pk_decay_(std::exp(-1 / (4.0 * sr))),
        c_gate_a_(1 - std::exp(-1 / (0.005 * sr))),
        c_gate_r_(1 - std::exp(-1 / (0.060 * sr))) { reset(); }

  void reset() {
    env_fast_ = 0; env_peak_ = 0.05; gate_open_ = false; gate_gain_ = 0;
    live_amp_ = 0;
  }

  void process(double x, double gate_threshold) {
    double a = std::fabs(x);
    env_fast_ += (a - env_fast_) * (a > env_fast_ ? c_env_a_ : c_env_r_);
    env_peak_ = std::max({env_peak_ * c_pk_decay_, env_fast_, 0.05});
    double raw_amp = std::min(1.0, env_fast_ / env_peak_);
    if (gate_open_) {
      if (env_fast_ < gate_threshold * 0.5) gate_open_ = false;
    } else if (env_fast_ > gate_threshold) gate_open_ = true;
    double tgt = gate_open_ ? 1.0 : 0.0;
    gate_gain_ += (tgt - gate_gain_) * (tgt > gate_gain_ ? c_gate_a_ : c_gate_r_);
    live_amp_ = raw_amp * gate_gain_;
  }

  double live_amp()  const { return live_amp_; }    // pre-0.5-headroom scale
  double env_fast()  const { return env_fast_; }
  double gate_gain() const { return gate_gain_; }
  bool   gate_open() const { return gate_open_; }

 private:
  double c_env_a_, c_env_r_, c_pk_decay_, c_gate_a_, c_gate_r_;
  double env_fast_, env_peak_, gate_gain_, live_amp_;
  bool gate_open_;
};
