// knob-pickup.js - port of firmware/hothouse/knob_pickup.hpp.
// After any layer or source change a knob is inert until it moves past a
// small threshold, then it takes over its parameter. Prevents 6 physical
// positions from slamming 18 parameters.

export class KnobPickup {
  // Normalized knob travel. Big enough to ignore ADC drift on the Hothouse
  // pots, small enough that a deliberate nudge takes over.
  static kThreshold = 0.02;

  constructor() {
    this.ref = [0, 0, 0, 0, 0, 0];
    this.liveFlags = [false, false, false, false, false, false];
  }

  // Called at boot and on every layer/source change (menu enter/exit, slot
  // recall): capture the current positions, all knobs go inert.
  rearm(pos) {
    for (let i = 0; i < 6; i++) { this.ref[i] = pos[i]; this.liveFlags[i] = false; }
  }

  // True when knob i's position should be applied to its parameter.
  update(i, pos) {
    if (!this.liveFlags[i] &&
        (pos > this.ref[i] + KnobPickup.kThreshold ||
         pos < this.ref[i] - KnobPickup.kThreshold)) {
      this.liveFlags[i] = true;
    }
    return this.liveFlags[i];
  }

  live(i) { return this.liveFlags[i]; }
}
