// test_knob_pickup.cpp - milestone 5 knob pickup (spec section 3)
#include <cassert>
#include <cstdio>

#include "knob_pickup.hpp"

int main() {
  KnobPickup kp;
  float pos[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
  kp.rearm(pos);

  // inert at the rearm position and within the threshold
  assert(!kp.update(0, 0.5f));
  assert(!kp.update(0, 0.51f));
  assert(!kp.update(0, 0.49f));
  assert(!kp.live(0));

  // crossing the threshold arms the knob, in either direction
  assert(kp.update(0, 0.53f));
  assert(kp.live(0));
  // once live, it stays live even back at the original position
  assert(kp.update(0, 0.5f));

  // other knobs are independent
  assert(!kp.update(1, 0.5f));
  assert(kp.update(2, 0.1f));

  // rearm resets everything to the new reference positions
  float pos2[6] = {0.9f, 0.1f, 0.5f, 0.5f, 0.5f, 0.5f};
  kp.rearm(pos2);
  assert(!kp.update(0, 0.9f));
  assert(!kp.update(2, 0.5f));
  assert(kp.update(0, 0.85f));

  printf("test_knob_pickup OK\n");
  return 0;
}
