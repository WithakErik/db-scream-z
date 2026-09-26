// chord_ui.hpp - chord mode control surface (chord mode design spec
// sections 3-4). A separate class from UiController on purpose: chord
// mode has no slots, no pages, no save chord and a momentary right stomp,
// and folding that into the normal-mode state machine would put a mode
// branch in every gesture path the normal pedal depends on.
//
//   left tap          engage / bypass (in the menu: leave the menu)
//   left hold ~1 s    latch the chord menu, under the foot
//   right, held       open mouth (momentary); never a menu, never a save
//   both together     charge, engaged and outside the menu only
//   toggle 3          gate level; toggles 1 and 2 do nothing
//
// Storage is automatic: the one ChordParams is saved kAutoSaveMs after the
// last change, or at once on leaving the menu, and only when it differs
// from what was last saved.
//
// Concurrency mirrors UiController: tick()/take_engage_edge() run in the
// audio ISR; save_pending()/save_snapshot()/save_done()/leds()/
// bootloader_armed() run on the main loop. save_snap_ is written by the
// ISR before save_pending_ goes up and left alone until the ack.
#pragma once
#include <cstdint>
#include <cstring>

#include "chord_map.hpp"
#include "knob_pickup.hpp"
#include "ui_controller.hpp"  // UiInputs, LedState, TogglePos
#include "voice_params.hpp"

class ChordUiController {
 public:
  static constexpr uint32_t kHoldMs = 1000;      // same latch as normal mode
  static constexpr uint32_t kMenuBlinkMs = 250;
  static constexpr uint32_t kBootFlashMs = 150;  // 3 flashes on entry
  static constexpr uint32_t kAutoSaveMs = 3000;
  // A live knob is re-applied only when it has moved more than this (in
  // normalized travel) from where it was last applied. WHY: libDaisy's
  // AnalogControl output wanders by a few LSB every block, so without a
  // deadband a live knob changes chord_ bit for bit nearly every tick, the
  // 3 s settle keeps restarting and main-layer edits are never auto-saved
  // (or, with intermittent wander, QSPI is rewritten every few seconds).
  // 1/256 is far above that wander and far below a deliberate turn
  // (final review, 2026-09-25).
  static constexpr float kChordKnobDeadband = 1.0f / 256.0f;

  void init(const ChordParams& saved, const ChargeConfig& charge,
            const UiInputs& in) {
    chord_ = saved;
    persisted_ = saved;
    config_ = charge;
    engaged_ = false;
    menu_ = false;
    engage_edge_ = false;
    left_down_ = in.left_down;
    right_down_ = in.right_down;
    left_start_ = in.now_ms;
    // The boot grip (both stomps held through power-up) is not a gesture:
    // treat it like any other both-down so its releases are swallowed.
    both_cancel_ = in.left_down && in.right_down;
    hold_latched_ = false;
    mouth_open_ = false;
    last_gate_ = in.t_gate;
    boot_ms_ = in.now_ms;
    last_change_ms_ = in.now_ms;
    exit_save_ = false;
    save_pending_ = false;
    save_ack_ = false;
    charge_level_ = 0.0f;
    charging_ = false;
    charge_session_ = false;
    last_now_ = in.now_ms;
    charge_led_flip_ = false;
    charge_led_ms_ = 0;
    pickup_.rearm(in.knobs);
  }

  void tick(const UiInputs& in) {
    if (save_ack_) {
      save_ack_ = false;
      save_pending_ = false;
      persisted_ = save_snap_;  // the write landed: new baseline
    }

    // ---- stomps ----
    const bool left_edge = in.left_down && !left_down_;
    const bool right_edge = in.right_down && !right_down_;
    if (left_edge) left_start_ = in.now_ms;
    if ((left_edge && in.right_down) || (right_edge && in.left_down)) {
      if (charge_session_) {
        charging_ = true;  // re-press before both released: resume
      } else if (!both_cancel_ && engaged_ && !menu_) {
        charge_session_ = true;
        charging_ = true;
        charge_led_flip_ = true;
        charge_led_ms_ = 0;
      }
    }
    if (in.left_down && in.right_down) both_cancel_ = true;
    if (charge_session_ && ((!in.left_down && left_down_) ||
                            (!in.right_down && right_down_)))
      charging_ = false;
    // Only the LEFT stomp latches, at the threshold, under the foot. The
    // right stomp is the mouth and is routinely held past kHoldMs.
    if (!hold_latched_ && !both_cancel_ && in.left_down &&
        in.now_ms - left_start_ >= kHoldMs) {
      hold_latched_ = true;
      if (!menu_) {
        menu_ = true;
        pickup_.rearm(in.knobs);  // layer change
      }
    }
    if (!in.left_down && left_down_) {
      if (hold_latched_) {
        hold_latched_ = false;  // the latching hold's release is swallowed
      } else if (!both_cancel_ && in.now_ms - left_start_ < kHoldMs) {
        on_left_tap(in);
      }
    }
    if (!in.left_down && !in.right_down) {
      both_cancel_ = false;
      charge_session_ = false;
      hold_latched_ = false;
    }
    // A both-press closes the mouth until both stomps are up, except after
    // a charge: once left is let go with right still held, the mouth opens
    // again (final review, 2026-09-25). both_cancel_ itself stays set, so
    // the menu-exit swallow and the hold latch are unchanged.
    mouth_open_ = in.right_down &&
                  (!both_cancel_ || (charge_session_ && !in.left_down));
    left_down_ = in.left_down;
    right_down_ = in.right_down;

    // ---- toggle 3 and knobs; note whether anything actually changed ----
    const ChordParams before = chord_;
    if (in.t_gate != last_gate_) chord_.gate_level = gate_from(in.t_gate);
    last_gate_ = in.t_gate;
    const ChordLayer lay = menu_ ? ChordLayer::Menu : ChordLayer::Main;
    for (int i = 0; i < 6; i++) {
      const bool was_live = pickup_.live(i);
      if (!pickup_.update(i, in.knobs[i])) continue;
      const float d = in.knobs[i] - applied_[i];
      // The pickup itself always applies (the rearm made the knob inert,
      // so this is the first application since the layer change).
      if (!was_live || d > kChordKnobDeadband || d < -kChordKnobDeadband) {
        applied_[i] = in.knobs[i];
        apply_chord_knob(lay, i, in.knobs[i], chord_);
      }
    }
    if (std::memcmp(&before, &chord_, sizeof chord_) != 0)
      last_change_ms_ = in.now_ms;

    // ---- auto-save (spec section 4) ----
    if (!save_pending_) {
      const bool dirty =
          std::memcmp(&chord_, &persisted_, sizeof chord_) != 0;
      if (!dirty) {
        exit_save_ = false;
      } else if (exit_save_ || in.now_ms - last_change_ms_ >= kAutoSaveMs) {
        save_snap_ = chord_;
        exit_save_ = false;
        save_pending_ = true;  // main loop persists, then save_done()
      }
    }

    // ---- charge level: same ramp as UiController (charge spec sec 3) ----
    const uint32_t dt = in.now_ms - last_now_;
    last_now_ = in.now_ms;
    if (charging_) {
      charge_level_ =
          charge_level_ + static_cast<float>(dt) / kChargeTimesMs[config_.time];
      if (charge_level_ > 1.0f) charge_level_ = 1.0f;
    } else if (charge_level_ > 0.0f) {
      const uint32_t d = kDecayTimesMs[config_.decay];
      if (d == 0) {
        charge_level_ = 0.0f;
      } else {
        charge_level_ = charge_level_ - static_cast<float>(dt) / d;
        if (charge_level_ < 0.0f) charge_level_ = 0.0f;
      }
    }
    if (charge_level_ > 0.0f) {
      charge_led_ms_ += dt;
      const uint32_t half =
          static_cast<uint32_t>(400.0f - 320.0f * charge_level_);
      if (charge_led_ms_ >= half) {
        charge_led_ms_ = 0;
        charge_led_flip_ = !charge_led_flip_;
      }
    }
  }

  const ChordParams& chord() const { return chord_; }
  bool engaged() const { return engaged_; }
  bool mouth_open() const { return mouth_open_; }
  bool in_menu() const { return menu_; }
  bool take_engage_edge() {
    const bool e = engage_edge_;
    engage_edge_ = false;
    return e;
  }
  float charge_level() const { return charge_level_; }
  bool charging() const { return charging_; }
  const ChargeConfig& charge_config() const { return config_; }
  bool bootloader_armed() const { return !engaged_; }

  bool save_pending() const { return save_pending_; }
  const ChordParams& save_snapshot() const { return save_snap_; }
  void save_done() { save_ack_ = true; }

  LedState leds(uint32_t now_ms) const {
    if (now_ms - boot_ms_ < 6 * kBootFlashMs) {
      const bool on = ((now_ms - boot_ms_) / kBootFlashMs) % 2 == 0;
      return {on, on};  // "you are in chord mode"
    }
    if (charge_level_ > 0.0f && !menu_)
      return {charge_led_flip_, !charge_led_flip_};
    if (menu_) return {(now_ms / kMenuBlinkMs) % 2 == 0, false};
    return {engaged_, mouth_open_};
  }

 private:
  static uint8_t gate_from(TogglePos t) {
    if (t == TogglePos::Up) return 2;    // high
    if (t == TogglePos::Down) return 0;  // low
    return 1;                            // medium
  }

  void on_left_tap(const UiInputs& in) {
    if (menu_) {
      menu_ = false;
      exit_save_ = true;
      pickup_.rearm(in.knobs);  // layer change
      return;
    }
    charging_ = false;  // any engage-state change kills the charge
    charge_level_ = 0.0f;
    if (engaged_) {
      engaged_ = false;
    } else {
      engaged_ = true;
      engage_edge_ = true;
    }
  }

  ChordParams chord_{};
  ChordParams persisted_{};  // last saved (ISR only)
  ChordParams save_snap_{};
  ChargeConfig config_{};
  volatile bool engaged_ = false;  // ISR writes, main loop reads
  volatile bool menu_ = false;
  volatile bool mouth_open_ = false;
  bool engage_edge_ = false;
  bool left_down_ = false, right_down_ = false;
  uint32_t left_start_ = 0;
  bool both_cancel_ = false;
  bool hold_latched_ = false;
  TogglePos last_gate_ = TogglePos::Middle;
  uint32_t boot_ms_ = 0;
  uint32_t last_change_ms_ = 0;
  bool exit_save_ = false;
  volatile bool save_pending_ = false;  // ISR writes, main loop reads
  volatile bool save_ack_ = false;      // main loop writes, ISR consumes
  volatile float charge_level_ = 0.0f;
  bool charging_ = false;
  bool charge_session_ = false;
  uint32_t last_now_ = 0;
  volatile bool charge_led_flip_ = false;
  uint32_t charge_led_ms_ = 0;
  KnobPickup pickup_;
  float applied_[6] = {0, 0, 0, 0, 0, 0};  // position each knob last applied
};
