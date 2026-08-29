// ui_controller.hpp - milestone 5 control-surface state machine (spec
// sections 2-4 and 7). Pure logic, no libDaisy types: main.cpp feeds
// UiInputs from the Hothouse each audio block and applies the outputs.
//
// Physical mapping (derived in the milestone 5 plan from the Hothouse PCB
// netlists, cross-checked against the Earth port build doc):
// FOOTSWITCH_1 / LED_1 = physical LEFT, FOOTSWITCH_2 / LED_2 = RIGHT.
// So: menu 2 = hold RIGHT (FS2), menu 3 = hold LEFT (FS1).
//
// Gesture timing is our own (kHoldMs = 1000): the native Hothouse
// FootswitchCallbacks long press is hardcoded to 2000 ms, too long for the
// spec's ~1 s latch.
// LED language amendment 2026-08-28: while a menu is latched only that
// menu's own LED is lit (blinking); the other is off regardless of what is
// engaged. See leds().
// Taps fire on RELEASE. Holds fire the MOMENT the press crosses kHoldMs
// (amendment 2026-08-28: the menu comes up under your foot, it does not
// wait for the release); that side's release is then swallowed.
// The save chord (hold one stomp past kHoldMs, press the other while
// still holding; spec amendment 2026-08-27) fires on the second PRESS
// edge, and because it shares kHoldMs with the latch it arrives after the
// held side has already latched its menu. The chord wins: it unwinds that
// latch first (see on_second_press), so both gestures survive sharing the
// threshold. Menus never save: in a menu, tapping the OTHER stomp switches
// menus (2 <-> 3) and only the latched menu's own stomp (the blinking
// side) exits.
//
// Concurrency: tick()/take_engage_edge() run in the audio ISR;
// save_pending()/save_slot()/save_snapshot()/save_done()/leds()/
// bootloader_armed() run on the main loop. save_pending_ and save_ack_
// are volatile single-writer flags (ISR->main and main->ISR); save_snap_
// is written by the ISR before save_pending_ goes up and left alone until
// the ack round-trips.
#pragma once
#include <cstdint>
#include <cstring>

#include "knob_pickup.hpp"
#include "param_map.hpp"
#include "voice_params.hpp"

enum class TogglePos : unsigned char { Up, Middle, Down };
enum class EngagedSource : unsigned char { None, SlotR, SlotL, Freeform };

struct UiInputs {
  bool left_down, right_down;          // debounced, physical left/right
  TogglePos t_octave, t_page, t_gate;  // toggles 1, 2, 3 (left to right)
  float knobs[6];                      // 0..1
  uint32_t now_ms;
};

struct LedState {
  bool left, right;
};

class UiController {
 public:
  static constexpr uint32_t kHoldMs = 1000;        // latch threshold
  static constexpr uint32_t kMenuBlinkMs = 250;    // menu blink half-period
  static constexpr uint32_t kSaveBlinkMs = 150;    // save confirm half-period
  static constexpr uint32_t kRejectFlickMs = 60;   // reject flicker phase

  void init(const VoiceStore* store, const UiInputs& in) {
    store_ = store;
    page_ = page_from(in.t_page);
    const Page load = page_ == Page::Freeform ? Page::Set1 : page_;
    edit_ = store_->slots[slot_index(load, Side::Right)];
    src_ = EngagedSource::None;
    engaged_slot_ = -1;
    menu_ = Menu::None;
    engage_edge_ = false;
    save_pending_ = false;
    save_ack_ = false;
    config_save_pending_ = false;
    config_save_ack_ = false;
    confirm_start_ = 0;
    reject_start_ = 0;
    last_octave_ = in.t_octave;
    last_page_ = in.t_page;
    last_gate_ = in.t_gate;
    persisted_config_ = store->charge;
    left_down_ = in.left_down;
    right_down_ = in.right_down;
    left_start_ = right_start_ = in.now_ms;
    both_cancel_ = in.left_down && in.right_down;
    hold_latched_ = false;
    menu_before_hold_ = Menu::None;
    config_ = store->charge;
    charge_level_ = 0.0f;
    charging_ = false;
    charge_session_ = false;
    last_now_ = in.now_ms;
    charge_led_flip_ = false;
    charge_led_ms_ = 0;
    pickup_.rearm(in.knobs);
  }

  void tick(const UiInputs& in) {
    // Save handshake ack from the main loop (spec: on save, all menus
    // exit, both LEDs blink 3 times, playback continues).
    if (save_ack_) {
      save_ack_ = false;
      save_pending_ = false;
      confirm_start_ = save_ack_ms_;
      // No layer change: the save chord only fires outside menus, so the
      // active layer is already menu 1 and the knobs stay live.
    }

    if (config_save_ack_) {
      config_save_ack_ = false;
      config_save_pending_ = false;
      persisted_config_ = config_;  // the write landed: new baseline
    }

    // ---- stomp gestures: tap = release < kHoldMs, hold = longer ----
    // Save-window guard: while a save is pending (the ~100 ms blocking
    // QSPI write), ignore taps and holds entirely so a recall can never
    // read a slot mid-write. Press/release tracking still runs.
    const bool save_guard = save_pending_ || config_save_pending_;
    const bool left_edge = in.left_down && !left_down_;
    const bool right_edge = in.right_down && !right_down_;
    if (left_edge) left_start_ = in.now_ms;
    if (right_edge) right_start_ = in.now_ms;
    // Second-press dispatch (charge spec section 2): decides save chord
    // vs charge session vs nothing. Runs BEFORE both_cancel_ is raised
    // so a fresh together-press is distinguishable from an ongoing
    // both-down state.
    if (left_edge && in.right_down) on_second_press(Side::Right, in);
    else if (right_edge && in.left_down) on_second_press(Side::Left, in);
    if (in.left_down && in.right_down) both_cancel_ = true;
    // A release while a charge session holds both stomps ends the ramp
    // and starts the decay; the release itself is swallowed below.
    if (charge_session_ && ((!in.left_down && left_down_) ||
                            (!in.right_down && right_down_)))
      charging_ = false;
    // The latch fires here, under the foot, the tick the press crosses
    // kHoldMs. Only a single-footed hold can latch: both down is charge
    // (or DFU), and the save window ignores gestures outright.
    if (!hold_latched_ && !both_cancel_ && !save_guard) {
      if (in.left_down && in.now_ms - left_start_ >= kHoldMs)
        arm_hold(Side::Left, in);
      else if (in.right_down && in.now_ms - right_start_ >= kHoldMs)
        arm_hold(Side::Right, in);
    }
    // Releases: a latched hold's release is swallowed, and so is one that
    // crossed kHoldMs while a guard was up (the gesture was ignored, it
    // does not become a tap on the way out). Everything shorter is a tap.
    if (!in.left_down && left_down_) {
      if (hold_latched_ && hold_latched_side_ == Side::Left) {
        hold_latched_ = false;
      } else if (!both_cancel_ && !save_guard &&
                 in.now_ms - left_start_ < kHoldMs) {
        on_tap(Side::Left, in);
      }
    }
    if (!in.right_down && right_down_) {
      if (hold_latched_ && hold_latched_side_ == Side::Right) {
        hold_latched_ = false;
      } else if (!both_cancel_ && !save_guard &&
                 in.now_ms - right_start_ < kHoldMs) {
        on_tap(Side::Right, in);
      }
    }
    if (!in.left_down && !in.right_down) {
      both_cancel_ = false;
      charge_session_ = false;
      hold_latched_ = false;
    }
    left_down_ = in.left_down;
    right_down_ = in.right_down;

    // ---- toggles (m5 spec sec 7 + charge spec sec 4 and 8) ----
    // Outside menus: a page move selects the page; octave/gate moves
    // (or freeform live-tracking) write the edit buffer. Inside a
    // latched menu: toggle MOVES edit the global charge config instead,
    // and page/octave/gate are untouched. The page is move-event-driven
    // so a config edit can never page-switch on menu exit.
    if (menu_ == Menu::None) {
      if (in.t_page != last_page_) page_ = page_from(in.t_page);
      const bool freeform_live = src_ == EngagedSource::Freeform;
      if (in.t_octave != last_octave_ || freeform_live)
        edit_.octave = octave_from(in.t_octave);
      if (in.t_gate != last_gate_ || freeform_live)
        edit_.gate_level = gate_from(in.t_gate);
    } else {
      if (in.t_octave != last_octave_) set_charge_field(0, in.t_octave);
      if (in.t_page != last_page_) set_charge_field(1, in.t_page);
      if (in.t_gate != last_gate_) set_charge_field(2, in.t_gate);
    }
    last_octave_ = in.t_octave;
    last_page_ = in.t_page;
    last_gate_ = in.t_gate;

    // ---- knobs: pickup, then route into the active layer ----
    const MenuLayer lay = layer();
    for (int i = 0; i < 6; i++)
      if (pickup_.update(i, in.knobs[i])) apply_knob(lay, i, in.knobs[i], edit_);

    // ---- charge level (charge spec section 3) ----
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
      // LED alternation clock (charge spec section 6): the half-period
      // shrinks from ~400 ms at level 0 to ~80 ms at full charge, and
      // runs backwards through the decay.
      charge_led_ms_ += dt;
      const uint32_t half =
          static_cast<uint32_t>(400.0f - 320.0f * charge_level_);
      if (charge_led_ms_ >= half) {
        charge_led_ms_ = 0;
        charge_led_flip_ = !charge_led_flip_;
      }
    }
  }

  const VoiceParams& edit_buffer() const { return edit_; }
  bool engaged() const { return src_ != EngagedSource::None; }
  EngagedSource source() const { return src_; }
  Page page() const { return page_; }

  MenuLayer layer() const {
    if (menu_ == Menu::Menu2) return MenuLayer::Menu2;
    if (menu_ == Menu::Menu3) return MenuLayer::Menu3;
    return MenuLayer::Menu1;
  }

  // True exactly once per bypass -> engage transition. main.cpp must then
  // clear the engine's output state (burp fix) and ramp in.
  bool take_engage_edge() {
    const bool e = engage_edge_;
    engage_edge_ = false;
    return e;
  }

  // ---- save handshake (main-loop side) ----
  bool save_pending() const { return save_pending_; }
  int save_slot() const { return save_slot_; }
  const VoiceParams& save_snapshot() const { return save_snap_; }
  void save_done(uint32_t now_ms) {
    save_ack_ms_ = now_ms;
    save_ack_ = true;
  }

  // ---- charge-config persist handshake (main-loop side): mirrors the
  // save handshake but silent, no confirm blink (charge spec sec 7).
  bool config_save_pending() const { return config_save_pending_; }
  void config_save_done() { config_save_ack_ = true; }

  // Bootloader gesture armed ONLY while bypassed (spec section 7).
  bool bootloader_armed() const { return src_ == EngagedSource::None; }

  // ---- charge mode (charge spec sections 2-3) ----
  float charge_level() const { return charge_level_; }
  bool charging() const { return charging_; }
  const ChargeConfig& charge_config() const { return config_; }

  // LED language (spec section 4). Pure function of state + time.
  LedState leds(uint32_t now_ms) const {
    if (confirm_start_ != 0 && now_ms - confirm_start_ < 6 * kSaveBlinkMs) {
      const bool on = ((now_ms - confirm_start_) / kSaveBlinkMs) % 2 == 0;
      return {on, on};  // 3 simultaneous blinks
    }
    if (reject_start_ != 0 && now_ms - reject_start_ < 3 * kRejectFlickMs) {
      const uint32_t ph = (now_ms - reject_start_) / kRejectFlickMs;
      const bool on = ph != 1;  // on-off-on: one short double-flicker
      return {on, on};
    }
    if (charge_level_ > 0.0f && menu_ == Menu::None) {
      // Charge animation overrides the normal language while active,
      // except when a menu is latched: the blink is actionable feedback,
      // the charge glow is decoration.
      return {charge_led_flip_, !charge_led_flip_};
    }
    LedState l;
    l.left = src_ == EngagedSource::SlotL || src_ == EngagedSource::Freeform;
    l.right = src_ == EngagedSource::SlotR || src_ == EngagedSource::Freeform;
    const bool blink = (now_ms / kMenuBlinkMs) % 2 == 0;
    // A latched menu owns BOTH LEDs (amended 2026-08-28): the menu's side
    // blinks and the other goes dark, even when that side is the engaged
    // slot. Previously the other LED kept showing the engaged source, and
    // one blinking plus one solid read as "both lit" at a glance, which is
    // the state that matters least to see. Which slot is being edited is
    // recoverable from the page toggle; that a menu is latched at all is
    // not.
    if (menu_ == Menu::Menu2) { l.right = blink; l.left = false; }
    if (menu_ == Menu::Menu3) { l.left = blink; l.right = false; }
    return l;
  }

 private:
  enum class Menu : unsigned char { None, Menu2, Menu3 };

  static Page page_from(TogglePos t) {
    if (t == TogglePos::Up) return Page::Set1;
    if (t == TogglePos::Down) return Page::Set2;
    return Page::Freeform;
  }
  static int8_t octave_from(TogglePos t) {
    if (t == TogglePos::Up) return 1;
    if (t == TogglePos::Down) return -1;
    return 0;
  }
  static uint8_t gate_from(TogglePos t) {
    if (t == TogglePos::Up) return 2;   // high
    if (t == TogglePos::Down) return 0; // low
    return 1;                           // medium
  }
  // Uniform charge-config value mapping: Up = 2, Middle = 1, Down = 0
  // (charge spec section 4, every row).
  static uint8_t charge_val(TogglePos t) {
    if (t == TogglePos::Up) return 2;
    if (t == TogglePos::Down) return 0;
    return 1;
  }
  // Toggle move while a menu is latched edits the global charge config.
  // Menu 2: gain/time/decay. Menu 3: pitch/tone/vib.
  void set_charge_field(int toggle, TogglePos pos) {
    const uint8_t v = charge_val(pos);
    uint8_t* f;
    if (menu_ == Menu::Menu2)
      f = toggle == 0 ? &config_.gain
        : toggle == 1 ? &config_.time
                      : &config_.decay;
    else
      f = toggle == 0 ? &config_.pitch
        : toggle == 1 ? &config_.tone
                      : &config_.vib;
    *f = v;  // dirtiness is judged at menu exit vs persisted_config_
  }
  static Menu menu_of(Side s) {
    return s == Side::Right ? Menu::Menu2 : Menu::Menu3;
  }

  // Remembers which side is holding and what the menu was before it
  // latched, so on_second_press can unwind the latch for a save chord.
  void arm_hold(Side side, const UiInputs& in) {
    hold_latched_ = true;
    hold_latched_side_ = side;
    menu_before_hold_ = menu_;
    on_hold(side, in);
  }

  void on_hold(Side side, const UiInputs& in) {
    // Hold latches that side's menu at the threshold (spec section 3,
    // amended 2026-08-28: no longer on release). Holding the OTHER side
    // while a menu is latched switches the latch (the spec leaves this
    // gesture undefined; switching is the deterministic choice and cannot
    // fire a save by accident).
    const Menu m = menu_of(side);
    if (menu_ != m) {
      menu_ = m;
      pickup_.rearm(in.knobs);  // layer change
    }
  }

  // Undo the latch the still-held `held` side just made, back to whatever
  // menu was up before it. The save chord's own hold now always trips the
  // latch first, and "menus never save" would otherwise eat every save.
  void unwind_hold_latch(Side held, const UiInputs& in) {
    if (!hold_latched_ || hold_latched_side_ != held) return;
    hold_latched_ = false;
    if (menu_ != menu_before_hold_) {
      menu_ = menu_before_hold_;
      pickup_.rearm(in.knobs);  // layer change
    }
  }

  // Save chord: second press edge while `held` has been down past kHoldMs.
  // Menus never save (spec amendment 2026-08-27); the target slot side is
  // the held side, matching the old latched-menu save mapping.
  void on_save_chord(Side held, const UiInputs& in) {
    if (menu_ != Menu::None || save_pending_ || config_save_pending_) return;
    if (page_ == Page::Freeform) {
      reject_start_ = in.now_ms;  // "pick a page first"
      return;
    }
    save_slot_ = slot_index(page_, held);
    save_snap_ = edit_;
    save_pending_ = true;  // main loop persists, then save_done()
  }

  // Called on a press edge while the other side (`held`) is also down.
  // Resume an ongoing charge session, fire the save chord, or start a
  // fresh charge session (charge spec section 2).
  void on_second_press(Side held, const UiInputs& in) {
    if (charge_session_) {  // re-press before both released: resume
      charging_ = true;
      return;
    }
    if (both_cancel_) return;  // ongoing non-charge both-down (menu,
                               // bypassed DFU hold): stays a no-op
    const uint32_t start = held == Side::Left ? left_start_ : right_start_;
    if (in.now_ms - start >= kHoldMs) {
      unwind_hold_latch(held, in);  // the chord outranks its own latch
      on_save_chord(held, in);
      return;
    }
    // Both down together: charge, armed only engaged, outside menus,
    // with no save window open.
    if (src_ != EngagedSource::None && menu_ == Menu::None &&
        !save_pending_ && !config_save_pending_) {
      charge_session_ = true;
      charging_ = true;
      charge_led_flip_ = true;  // first animation phase: LEFT on (spec
      charge_led_ms_ = 0;       // section 6); a resume keeps its phase
    }
  }

  void on_tap(Side side, const UiInputs& in) {
    // Menu mode (spec amendments 2026-08-27): tapping the OTHER stomp
    // switches to that side's menu; only the latched menu's own stomp
    // (the blinking side) exits. Menus never save; saving is only ever
    // the hold+press chord, outside menus.
    if (menu_ != Menu::None) {
      const Menu m = menu_of(side);
      menu_ = menu_ == m ? Menu::None : m;  // own side exits, other switches
      // On exit, persist only a NET config change (an edit reverted in
      // the same session is not a change; charge spec section 7).
      if (menu_ == Menu::None &&
          std::memcmp(&config_, &persisted_config_, sizeof(ChargeConfig)) != 0)
        config_save_pending_ = true;  // main loop persists, silently
      pickup_.rearm(in.knobs);              // layer change either way
      return;
    }
    // Any engage-state change kills the charge instantly (charge spec
    // section 2): every path below bypasses, engages, or recalls.
    charging_ = false;
    charge_level_ = 0.0f;
    // No menu latched: engage / bypass / recall.
    if (page_ == Page::Freeform) {
      if (src_ != EngagedSource::None) {
        src_ = EngagedSource::None;  // engaged -> bypass
        engaged_slot_ = -1;
      } else {
        src_ = EngagedSource::Freeform;  // edit buffer as-is, no load
        engage_edge_ = true;
      }
      return;
    }
    const int target = slot_index(page_, side);
    if (src_ != EngagedSource::None && engaged_slot_ == target) {
      src_ = EngagedSource::None;  // tap again = bypass
      engaged_slot_ = -1;
      return;
    }
    // Recall + engage: copy the slot into the edit buffer (discards
    // unsaved tweaks, by design), stored octave/gate win until a toggle
    // moves (nothing to do: the toggle handler is event-driven).
    const bool was_bypassed = src_ == EngagedSource::None;
    edit_ = store_->slots[target];
    engaged_slot_ = target;
    src_ = side == Side::Right ? EngagedSource::SlotR : EngagedSource::SlotL;
    if (was_bypassed) engage_edge_ = true;
    pickup_.rearm(in.knobs);  // source change
  }

  const VoiceStore* store_ = nullptr;
  VoiceParams edit_{};
  Page page_ = Page::Set1;
  // ISR writes (on_tap/on_hold/tick), main loop reads (bootloader_armed(),
  // leds()): single-writer ISR-to-main, same pattern as save_pending_ below
  // (see also grain_dirty_ in firmware/engine/fof_engine.hpp).
  volatile EngagedSource src_ = EngagedSource::None;
  int engaged_slot_ = -1;
  volatile Menu menu_ = Menu::None;  // ISR writes, main loop reads (leds())
  bool engage_edge_ = false;

  volatile bool save_pending_ = false;  // ISR writes, main loop reads
  volatile bool save_ack_ = false;      // main loop writes, ISR consumes
  volatile uint32_t save_ack_ms_ = 0;   // written before save_ack_; volatile
                                        // so the two stores cannot reorder
  int save_slot_ = -1;
  VoiceParams save_snap_{};

  volatile bool config_save_pending_ = false;  // ISR writes, main reads
  volatile bool config_save_ack_ = false;      // main writes, ISR consumes

  // Charge mode. charge_level_ is ISR-written, main-loop-read (leds());
  // aligned 32-bit float loads are atomic on the M7, same single-writer
  // pattern as the flags above. config_ is ISR-written on toggle moves,
  // main-loop-read for persistence (byte fields, torn reads benign).
  volatile float charge_level_ = 0.0f;
  bool charging_ = false;        // both held, ramp rising (ISR only)
  bool charge_session_ = false;  // both-down session claims the gestures
  ChargeConfig config_{};
  uint32_t last_now_ = 0;
  volatile bool charge_led_flip_ = false;  // ISR writes, leds() reads
  uint32_t charge_led_ms_ = 0;

  // ISR writes, main loop reads (leds()): single-writer ISR-to-main.
  volatile uint32_t confirm_start_ = 0, reject_start_ = 0;

  TogglePos last_octave_ = TogglePos::Middle, last_gate_ = TogglePos::Middle;
  TogglePos last_page_ = TogglePos::Up;
  ChargeConfig persisted_config_{};  // last QSPI-persisted config (ISR only)
  bool left_down_ = false, right_down_ = false;
  uint32_t left_start_ = 0, right_start_ = 0;
  bool both_cancel_ = false;
  // An ongoing hold that has already latched: its release is swallowed,
  // and a save chord on the same side unwinds it back to menu_before_hold_.
  bool hold_latched_ = false;
  Side hold_latched_side_ = Side::Left;
  Menu menu_before_hold_ = Menu::None;

  KnobPickup pickup_;
};
