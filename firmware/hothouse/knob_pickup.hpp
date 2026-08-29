// knob_pickup.hpp - milestone 5 knob pickup (spec section 3): after any
// layer or source change a knob is inert until it moves past a small
// threshold, then it takes over its parameter. Prevents 6 physical
// positions from slamming 18 parameters. Host-testable, no dependencies.
#pragma once

class KnobPickup {
 public:
  // Normalized knob travel. Big enough to ignore ADC drift on the
  // Hothouse pots, small enough that a deliberate nudge takes over.
  static constexpr float kThreshold = 0.02f;

  // Called at boot and on every layer/source change (menu enter/exit,
  // slot recall): capture the current positions, all knobs go inert.
  void rearm(const float pos[6]) {
    for (int i = 0; i < 6; i++) {
      ref_[i] = pos[i];
      live_[i] = false;
    }
  }

  // Returns true when knob i's position should be applied to its
  // parameter (i.e. the knob has taken over since the last rearm).
  bool update(int i, float pos) {
    if (!live_[i] &&
        (pos > ref_[i] + kThreshold || pos < ref_[i] - kThreshold)) {
      live_[i] = true;
    }
    return live_[i];
  }

  bool live(int i) const { return live_[i]; }

 private:
  float ref_[6] = {0, 0, 0, 0, 0, 0};
  bool live_[6] = {false, false, false, false, false, false};
};
