// test_chord_ui.cpp - chord mode control surface (chord mode spec
// sections 3 and 4): gestures, knob layers, gate toggle, charge, LEDs and
// the auto-save handshake. Normal mode's UiController is covered by
// test_ui_controller.cpp and is not touched by chord mode.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "chord_ui.hpp"

static UiInputs base_in() {
  UiInputs in{};
  in.left_down = in.right_down = false;
  in.t_octave = TogglePos::Middle;
  in.t_page = TogglePos::Up;
  in.t_gate = TogglePos::Middle;
  for (int i = 0; i < 6; i++) in.knobs[i] = 0.5f;
  in.now_ms = 0;
  return in;
}

// 1 ms ticks, like test_ui_controller.cpp's Sim. `ack` emulates the main
// loop: when true, every pending save is persisted on the next step.
struct Sim {
  ChordUiController ui;
  UiInputs in = base_in();
  uint32_t t = 1000;
  int saves = 0;
  ChordParams last_saved{};
  bool ack = true;
  explicit Sim(bool both_at_boot = false) {
    in.now_ms = t;
    in.left_down = in.right_down = both_at_boot;
    ui.init(factory_chord(), factory_charge_config(), in);
  }
  void step(uint32_t dt = 1) {
    for (uint32_t i = 0; i < dt; i++) {
      t += 1;
      in.now_ms = t;
      ui.tick(in);
      if (ack && ui.save_pending()) {
        last_saved = ui.save_snapshot();
        saves++;
        ui.save_done();
      }
    }
  }
  void press(Side s) { down(s) = true; step(); }
  void release(Side s) { down(s) = false; step(); }
  void tap(Side s) { press(s); step(50); release(s); }
  bool& down(Side s) { return s == Side::Left ? in.left_down : in.right_down; }
  void knob(int i, float v) { in.knobs[i] = v; step(); }
};

int main() {
  // ---- boot: bypassed, main layer, 3 simultaneous flashes then dark
  {
    Sim s;
    assert(!s.ui.engaged() && !s.ui.in_menu() && s.ui.bootloader_armed());
    LedState l = s.ui.leds(s.t);
    assert(l.left && l.right);                                   // flash 1 on
    l = s.ui.leds(1000 + ChordUiController::kBootFlashMs + 1);
    assert(!l.left && !l.right);                                 // off phase
    l = s.ui.leds(1000 + 6 * ChordUiController::kBootFlashMs + 1);
    assert(!l.left && !l.right);                                 // done, bypassed
  }

  // ---- the boot grip is not a gesture: held past kHoldMs, released
  {
    Sim s(true);
    s.step(1500);
    s.in.left_down = s.in.right_down = false;
    s.step();
    assert(!s.ui.engaged() && !s.ui.in_menu() && !s.ui.mouth_open());
    assert(!s.ui.take_engage_edge());
  }

  // ---- left tap engages (one edge), tap again bypasses
  {
    Sim s;
    s.tap(Side::Left);
    assert(s.ui.engaged() && s.ui.take_engage_edge() && !s.ui.take_engage_edge());
    assert(!s.ui.bootloader_armed());
    s.step(1000);
    assert(s.ui.leds(s.t).left && !s.ui.leds(s.t).right);
    s.tap(Side::Left);
    assert(!s.ui.engaged());
  }

  // ---- left hold latches the chord menu under the foot; release is
  // swallowed; left tap leaves it
  {
    Sim s;
    s.tap(Side::Left);  // engaged
    s.press(Side::Left);
    s.step(ChordUiController::kHoldMs);
    assert(s.ui.in_menu());               // latched while still held
    s.release(Side::Left);
    assert(s.ui.in_menu() && s.ui.engaged());
    s.step(2000);
    const LedState l = s.ui.leds(0);      // blink phase "on" at t = 0
    assert(l.left && !l.right);
    assert(!s.ui.leds(ChordUiController::kMenuBlinkMs).left);
    s.tap(Side::Left);
    assert(!s.ui.in_menu() && s.ui.engaged());
  }

  // ---- long right hold (Review Focus 4): mouth open throughout, never a
  // menu, release changes nothing else
  {
    Sim s;
    s.tap(Side::Left);
    s.press(Side::Right);
    for (int i = 0; i < 50; i++) {
      s.step(100);
      assert(s.ui.mouth_open() && !s.ui.in_menu());
    }
    s.step(1000);
    assert(s.ui.leds(s.t).right);
    s.release(Side::Right);
    assert(!s.ui.mouth_open() && s.ui.engaged() && !s.ui.in_menu());
    // right tap does nothing but open the mouth for the tap
    s.tap(Side::Right);
    assert(s.ui.engaged() && !s.ui.in_menu());
  }

  // ---- both together: charge only when engaged and outside the menu
  {
    Sim s;
    s.in.left_down = s.in.right_down = true;  // bypassed
    s.step(500);
    assert(!s.ui.charging() && s.ui.charge_level() == 0.0f);
    s.in.left_down = s.in.right_down = false;
    s.step();
    s.tap(Side::Left);  // engaged
    s.in.left_down = s.in.right_down = true;
    s.step(500);
    assert(s.ui.charging() && s.ui.charge_level() > 0.0f);
    assert(!s.ui.mouth_open() && !s.ui.in_menu());
    s.in.left_down = s.in.right_down = false;
    s.step();
    assert(!s.ui.charging() && s.ui.engaged());  // no tap on the way out
  }

  // ---- knob layers with pickup: menu 1 knob 6 = drive, menu knob 1 =
  // closed vowel; after a layer change knobs are inert until moved
  {
    Sim s;
    s.knob(5, 0.0f);  // not 1.0: 40x is the factory drive, proving nothing
    assert(std::fabs(s.ui.chord().drive - 1.0f) < 1e-3f);
    s.press(Side::Left);
    s.step(ChordUiController::kHoldMs);
    s.release(Side::Left);
    assert(s.ui.in_menu());
    const float cv = s.ui.chord().closed_vowel;
    s.knob(0, 0.5f);                        // unmoved since the latch
    assert(s.ui.chord().closed_vowel == cv);
    s.knob(0, 1.0f);
    assert(s.ui.chord().closed_vowel == 4.0f);
    assert(std::fabs(s.ui.chord().drive - 1.0f) < 1e-3f);  // untouched
  }

  // ---- toggles: octave and gate moves are stored, page does nothing
  {
    Sim s;
    const ChordParams before = s.ui.chord();
    s.in.t_page = TogglePos::Down; s.step();
    assert(std::memcmp(&before, &s.ui.chord(), sizeof before) == 0);
    s.in.t_octave = TogglePos::Up; s.step();
    assert(s.ui.chord().octave == 1);
    s.in.t_octave = TogglePos::Down; s.step();
    assert(s.ui.chord().octave == -1);
    s.in.t_octave = TogglePos::Middle; s.step();
    assert(s.ui.chord().octave == 0);
    s.in.t_gate = TogglePos::Up; s.step();
    assert(s.ui.chord().gate_level == 2);
  }

  // ---- the saved octave wins at power-up until toggle 1 actually moves,
  // exactly as the gate does (and as regular mode's octave does)
  {
    ChordParams saved = factory_chord();
    saved.octave = -1;
    UiInputs in = base_in();
    in.now_ms = 1000;
    in.t_octave = TogglePos::Up;  // disagrees with the saved -1
    ChordUiController ui;
    ui.init(saved, factory_charge_config(), in);
    for (int i = 0; i < 100; i++) { in.now_ms++; ui.tick(in); }
    assert(ui.chord().octave == -1);
    in.t_octave = TogglePos::Middle;
    in.now_ms++;
    ui.tick(in);
    assert(ui.chord().octave == 0);
  }

  // ---- a toggle 1 move auto-saves like a knob turn
  {
    Sim s;
    s.in.t_octave = TogglePos::Up; s.step();
    s.step(ChordUiController::kAutoSaveMs - 10);
    assert(s.saves == 0);
    s.step(20);
    assert(s.saves == 1 && s.last_saved.octave == 1);
  }

  // ---- toggle 1 works inside the chord menu too, like the gate toggle
  {
    Sim s;
    s.press(Side::Left);
    s.step(ChordUiController::kHoldMs);
    s.release(Side::Left);
    assert(s.ui.in_menu());
    s.in.t_octave = TogglePos::Down; s.step();
    assert(s.ui.chord().octave == -1 && s.ui.in_menu());
  }

  // ---- auto-save: 3 s after the last change, not before; one write
  {
    Sim s;
    s.knob(5, 0.8f);
    s.step(ChordUiController::kAutoSaveMs - 10);
    assert(s.saves == 0);
    s.step(20);
    assert(s.saves == 1);
    assert(std::memcmp(&s.last_saved, &s.ui.chord(), sizeof s.last_saved) == 0);
    s.step(10000);
    assert(s.saves == 1);
  }

  // ---- auto-save: leaving the menu saves at once
  {
    Sim s;
    s.press(Side::Left);
    s.step(ChordUiController::kHoldMs);
    s.release(Side::Left);
    s.knob(3, 0.9f);
    s.tap(Side::Left);
    s.step(2);
    assert(s.saves == 1 && std::fabs(s.last_saved.resonance - 0.9f) < 1e-6f);
  }

  // ---- no-op edits never save (Review Focus 5)
  {
    Sim s;
    s.knob(5, 0.8f);
    s.step(ChordUiController::kAutoSaveMs + 10);
    assert(s.saves == 1);
    s.knob(5, 0.3f);
    s.knob(5, 0.8f);                        // back where it was saved
    s.step(ChordUiController::kAutoSaveMs + 10);
    assert(s.saves == 1);
    s.press(Side::Left);                    // menu in and out, no edits
    s.step(ChordUiController::kHoldMs);
    s.release(Side::Left);
    s.tap(Side::Left);
    s.step(ChordUiController::kAutoSaveMs + 10);
    assert(s.saves == 1);
  }

  // ---- charge is never saved (Review Focus 3): a save that fires
  // mid-charge carries the knob values
  {
    Sim s;
    s.tap(Side::Left);
    s.knob(0, 0.25f);                       // vocal vol 0.5
    s.in.left_down = s.in.right_down = true;
    s.step(ChordUiController::kAutoSaveMs + 10);
    assert(s.ui.charge_level() > 0.0f && s.saves == 1);
    assert(std::fabs(s.last_saved.vocal_vol - 0.5f) < 1e-6f);
  }

  // ---- a pending save is not duplicated while the main loop is busy
  {
    Sim s;
    s.ack = false;
    s.knob(5, 0.8f);
    s.step(ChordUiController::kAutoSaveMs + 10);
    assert(s.ui.save_pending());
    const ChordParams snap = s.ui.save_snapshot();
    s.knob(5, 0.1f);                        // edit while the write is pending
    assert(std::memcmp(&snap, &s.ui.save_snapshot(), sizeof snap) == 0);
    s.ui.save_done();
    s.ack = true;
    s.step(ChordUiController::kAutoSaveMs + 10);
    assert(s.saves == 1 && std::fabs(s.last_saved.drive - map_drive(0.1f)) < 1e-4f);
  }

  // ---- ADC jitter never defeats the auto-save (final review, 2026-09-25):
  // a live knob wandering by a few LSB must not keep resetting the 3 s
  // settle, and must not re-trigger a save once the edit has landed
  {
    Sim s;
    s.knob(5, 0.8f);                        // pick up
    for (int i = 1; i <= 20; i++) s.knob(5, 0.8f - 0.01f * i);  // real move
    const uint32_t last_move = s.t;
    const float rest = s.in.knobs[5];
    // Deterministic few-LSB wander around the resting position, 20 s.
    static const float kJitter[4] = {3e-5f, -2e-5f, -3e-5f, 1e-5f};
    int saves_at_settle = -1;
    for (uint32_t ms = 1; ms <= 20000; ms++) {
      s.in.knobs[5] = rest + kJitter[ms % 4];
      s.step();
      if (s.t == last_move + ChordUiController::kAutoSaveMs - 10)
        assert(s.saves == 0);               // not before the settle
      if (s.t == last_move + ChordUiController::kAutoSaveMs + 10)
        saves_at_settle = s.saves;
    }
    assert(saves_at_settle == 1);           // one save, ~3 s after the move
    assert(s.saves == 1);                   // and none while jitter goes on
    assert(std::fabs(s.last_saved.drive - map_drive(rest)) < 1e-3f);
  }

  // ---- left while right is held: charge, then the mouth reopens as soon
  // as left is let go (final review, 2026-09-25)
  {
    Sim s;
    s.tap(Side::Left);                      // engaged
    s.press(Side::Right);
    s.step(200);
    assert(s.ui.mouth_open());
    s.press(Side::Left);
    s.step(300);
    assert(s.ui.charging() && !s.ui.mouth_open());
    s.release(Side::Left);
    assert(!s.ui.charging() && s.ui.mouth_open() && s.ui.engaged());
    s.step(500);
    assert(s.ui.mouth_open() && !s.ui.in_menu());
    s.release(Side::Right);
    assert(!s.ui.mouth_open() && s.ui.engaged());
  }

  // ---- in the chord menu a left tap while right is held is still
  // swallowed (same as normal mode): lift right first to leave
  {
    Sim s;
    s.press(Side::Left);
    s.step(ChordUiController::kHoldMs);
    s.release(Side::Left);
    assert(s.ui.in_menu());
    s.press(Side::Right);
    s.tap(Side::Left);
    assert(s.ui.in_menu());
    s.release(Side::Right);
    s.tap(Side::Left);
    assert(!s.ui.in_menu());
  }

  std::printf("test_chord_ui OK\n");
  return 0;
}
