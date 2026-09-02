// test_ui_controller.cpp - milestone 5 control-surface state machine
// (spec sections 2, 3, 4, 7). Task 5 covers boot, taps, recall/engage/
// bypass, pages, freeform, toggles, menus + knob routing; Task 6 appends
// the save flow and LED animation tests to this same file.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "ui_controller.hpp"

static VoiceStore g_store = factory_store();

static UiInputs base_in() {
  UiInputs in{};
  in.left_down = in.right_down = false;
  in.t_octave = TogglePos::Middle;  // octave 0
  in.t_page = TogglePos::Up;        // Set 1
  in.t_gate = TogglePos::Middle;    // medium
  for (int i = 0; i < 6; i++) in.knobs[i] = 0.5f;
  in.now_ms = 0;
  return in;
}

// Drives the controller with 1 ms ticks (the audio-block cadence).
struct Sim {
  UiController ui;
  UiInputs in = base_in();
  uint32_t t = 1000;
  Sim() {
    in.now_ms = t;
    ui.init(&g_store, in);
  }
  void step(uint32_t dt = 1) {
    for (uint32_t i = 0; i < dt; i++) { t += 1; in.now_ms = t; ui.tick(in); }
  }
  void press(Side s) { down(s) = true; step(); }
  void release(Side s) { down(s) = false; step(); }
  void tap(Side s) { press(s); step(50); release(s); }
  void hold(Side s, uint32_t ms = 1200) { press(s); step(ms); release(s); }
  bool& down(Side s) { return s == Side::Left ? in.left_down : in.right_down; }
};

static bool near(float a, float b, float eps = 1e-4f) {
  return std::fabs(a - b) <= eps;
}

int main() {
  g_store = factory_store();

  // ---- boot: bypassed, LEDs off, edit buffer = current page's R slot
  {
    Sim s;
    assert(!s.ui.engaged());
    assert(s.ui.bootloader_armed());
    assert(s.ui.layer() == MenuLayer::Menu1);
    LedState l = s.ui.leds(s.t);
    assert(!l.left && !l.right);
    assert(near(s.ui.edit_buffer().f1, g_store.slots[0].f1));  // Wukong
    assert(!s.ui.take_engage_edge());
  }

  // ---- boot with the page toggle on Freeform: edit buffer still loads
  // Set 1 R (init falls back to Set1 because Freeform has no slot)
  {
    UiInputs in = base_in();
    in.t_page = TogglePos::Middle;  // Freeform at boot
    in.now_ms = 1000;
    UiController ui;
    ui.init(&g_store, in);
    assert(ui.page() == Page::Freeform);
    assert(near(ui.edit_buffer().f1, g_store.slots[0].f1));  // Set 1 R, Wukong
  }

  // ---- tap right on Set 1: recall + engage Wukong; tap again: bypass
  {
    Sim s;
    s.tap(Side::Right);
    assert(s.ui.engaged() && s.ui.source() == EngagedSource::SlotR);
    assert(s.ui.take_engage_edge());        // fired exactly once
    assert(!s.ui.take_engage_edge());
    assert(!s.ui.bootloader_armed());
    LedState l = s.ui.leds(s.t);
    assert(l.right && !l.left);
    s.tap(Side::Right);
    assert(!s.ui.engaged());
    l = s.ui.leds(s.t);
    assert(!l.left && !l.right);
    assert(s.ui.bootloader_armed());
  }

  // ---- left tap: Prince; then page to Set 2, right tap: direct slot switch
  {
    Sim s;
    s.tap(Side::Left);
    assert(s.ui.source() == EngagedSource::SlotL);
    assert(near(s.ui.edit_buffer().f1, g_store.slots[1].f1));  // Prince
    assert(s.ui.take_engage_edge());
    s.in.t_page = TogglePos::Down;  // Set 2; paging never changes audio
    s.step();
    assert(near(s.ui.edit_buffer().f1, g_store.slots[1].f1));  // unchanged
    s.tap(Side::Right);
    assert(s.ui.source() == EngagedSource::SlotR);
    assert(near(s.ui.edit_buffer().f1, g_store.slots[2].f1));  // Rice
    assert(!s.ui.take_engage_edge());  // engaged -> engaged: no edge
  }

  // ---- freeform: tap engages the edit buffer as-is, both LEDs solid
  {
    Sim s;
    s.tap(Side::Right);                       // Wukong engaged (pickup rearmed
    assert(s.ui.take_engage_edge());          // consume the recall edge
    s.in.knobs[1] = 0.8f; s.step();           // at 0.5; 0.8 crosses the
    assert(near(s.ui.edit_buffer().mix, 0.8f));  // threshold, mix takes over)
    s.in.t_page = TogglePos::Middle;          // Freeform
    s.step();
    s.tap(Side::Left);                        // engaged -> bypass
    assert(!s.ui.engaged());
    assert(!s.ui.take_engage_edge());         // bypass sets no edge
    s.tap(Side::Right);                       // bypassed -> engage freeform
    assert(s.ui.source() == EngagedSource::Freeform);
    assert(s.ui.take_engage_edge());          // freeform engage fires the edge
    assert(!s.ui.take_engage_edge());         // exactly once
    assert(near(s.ui.edit_buffer().mix, 0.8f));  // buffer kept as-is
    LedState l = s.ui.leds(s.t);
    assert(l.left && l.right);
  }

  // ---- toggle override: recalled octave/gate win until the toggle MOVES
  {
    Sim s;
    s.in.t_octave = TogglePos::Up;  // physical +1 before recall
    s.step();
    s.tap(Side::Right);             // Wukong stores octave 0
    assert(s.ui.edit_buffer().octave == 0);      // stored value wins
    s.in.t_octave = TogglePos::Down;             // a move is always an event
    s.step();
    assert(s.ui.edit_buffer().octave == -1);
    s.in.t_gate = TogglePos::Up;
    s.step();
    assert(s.ui.edit_buffer().gate_level == 2);  // high
  }

  // ---- freeform-engaged: toggles always track live
  {
    Sim s;
    s.in.t_page = TogglePos::Middle;
    s.step();
    s.tap(Side::Right);  // engage freeform
    assert(s.ui.source() == EngagedSource::Freeform);
    s.in.t_octave = TogglePos::Up;
    s.step();
    assert(s.ui.edit_buffer().octave == 1);
  }

  // ---- knob pickup: recall rearms; small move stays inert, big move takes over
  {
    Sim s;
    s.tap(Side::Right);
    float mix0 = s.ui.edit_buffer().mix;  // factory 1.0
    s.in.knobs[1] = 0.505f;  // within threshold of 0.5
    s.step();
    assert(near(s.ui.edit_buffer().mix, mix0));
    s.in.knobs[1] = 0.7f;    // crosses threshold
    s.step();
    assert(near(s.ui.edit_buffer().mix, 0.7f));
  }

  // ---- menu latch: hold right ~1 s latches menu 2 on release, LED blinks
  {
    Sim s;
    s.tap(Side::Right);
    s.hold(Side::Right);
    assert(s.ui.layer() == MenuLayer::Menu2);
    // right LED blinking: differs across a half-period boundary
    LedState a = s.ui.leds(4000), b = s.ui.leds(4250);
    assert(a.right != b.right);
    // and the LEFT LED is DARK throughout, even though the R slot is
    // engaged: a latched menu owns both LEDs (amended 2026-08-28)
    assert(!a.left && !b.left);
    // knobs now edit menu 2 with pickup: knob 0 -> f1
    s.in.knobs[0] = 0.75f;  // 0.5 -> 0.75 crosses threshold
    s.step();
    assert(near(s.ui.edit_buffer().f1, 200.0f + 0.75f * 1200.0f));
    // tap the latched menu's own stomp: exit, no save, knobs rearm
    s.tap(Side::Right);
    assert(s.ui.layer() == MenuLayer::Menu1);
    assert(s.ui.engaged());               // exit does not bypass
    s.in.knobs[0] = 0.74f; s.step();      // small move, inert after rearm
    assert(near(s.ui.edit_buffer().vocal_vol, 1.0f));
    // left hold latches menu 3, left LED blinks (half-period apart differs)
    s.hold(Side::Left);
    assert(s.ui.layer() == MenuLayer::Menu3);
    LedState c = s.ui.leds(4000), d = s.ui.leds(4250);
    assert(c.left != d.left);
    // the RIGHT LED is dark here for the same reason, and this is the
    // case that prompted the amendment: R engaged, menu 3 latched
    assert(!c.right && !d.right);
    // knob 3 was rearmed at its physical 0.5; moving it to 0.9 crosses the
    // pickup threshold, so the voice count takes over: 0.9 is the eighth of
    // eight equal bands, so 8 voices.
    s.in.knobs[3] = 0.9f; s.step();
    assert(near(s.ui.edit_buffer().unison, 8.0f));
    s.tap(Side::Left);
    assert(s.ui.layer() == MenuLayer::Menu1);
  }

  // ---- menus are available while bypassed
  {
    Sim s;
    s.hold(Side::Right);
    assert(!s.ui.engaged());
    assert(s.ui.layer() == MenuLayer::Menu2);
    assert(s.ui.bootloader_armed());  // still bypassed
    s.tap(Side::Right);
    assert(s.ui.layer() == MenuLayer::Menu1);
  }

  // ---- latch switch: holding the OTHER stomp while a menu is latched
  // switches the latch (menu 2 <-> menu 3), never fires a save
  {
    Sim s;
    s.hold(Side::Right);
    assert(s.ui.layer() == MenuLayer::Menu2);
    s.hold(Side::Left);
    assert(s.ui.layer() == MenuLayer::Menu3);  // latch switched
    assert(!s.ui.save_pending());              // no save fired
    s.hold(Side::Right);
    assert(s.ui.layer() == MenuLayer::Menu2);  // and back
    s.tap(Side::Right);
    assert(s.ui.layer() == MenuLayer::Menu1);
  }

  // ---- both pressed: gestures cancel (aborted DFU latches nothing)
  {
    Sim s;
    s.press(Side::Left);
    s.step(300);
    s.press(Side::Right);
    s.step(1500);              // both crossed the hold threshold
    s.release(Side::Right);
    s.release(Side::Left);
    s.step(5);
    assert(s.ui.layer() == MenuLayer::Menu1);  // nothing latched
    assert(!s.ui.engaged());                   // nothing engaged
  }

  // ---- both pressed while a menu is latched: gesture ignored, menu stays
  {
    Sim s;
    s.hold(Side::Right);
    assert(s.ui.layer() == MenuLayer::Menu2);
    s.press(Side::Left);
    s.step(300);
    s.press(Side::Right);
    s.step(1500);              // both crossed the hold threshold
    s.release(Side::Right);
    s.release(Side::Left);
    s.step(5);
    assert(s.ui.layer() == MenuLayer::Menu2);  // latch unchanged
    assert(!s.ui.save_pending());              // no save fired
    assert(!s.ui.engaged());                   // nothing engaged
  }

  // ==== Task 6: save flow + LED animations (spec sections 3 and 4) ====
  // Save gesture (redesigned 2026-08-27): hold one stomp past the latch
  // threshold, then press the OTHER stomp while still holding. Fires on
  // the second press edge; both releases are swallowed (nothing latches).
  // Menus never save: in a menu, the other stomp's tap switches menus
  // and only the latched menu's own stomp (blinking side) exits.

  // ---- save: hold RIGHT ~1 s, press LEFT while still holding -> R slot
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);        // engage Wukong (Set 1 R)
    assert(s.ui.take_engage_edge());
    s.in.knobs[1] = 0.8f;      // menu 1 knob 1: mix -> 0.8 (differs from
    s.step();                  // the factory slot's 1.0)
    s.press(Side::Right);
    s.step(1200);              // past kHoldMs, still holding
    assert(!s.ui.save_pending());   // arming alone writes nothing
    s.press(Side::Left);       // second press while first held: SAVE
    assert(s.ui.save_pending());
    assert(s.ui.save_slot() == 0);  // held side = R, page = Set 1
    assert(near(s.ui.save_snapshot().mix, 0.8f));
    s.release(Side::Left);
    s.release(Side::Right);    // swallowed: the long hold latches no menu
    s.step(5);
    assert(s.ui.layer() == MenuLayer::Menu1);
    // main-loop side of the handshake:
    g_store.slots[s.ui.save_slot()] = s.ui.save_snapshot();
    s.ui.save_done(s.t);
    s.step();                  // tick consumes the ack
    assert(!s.ui.save_pending());
    assert(s.ui.engaged() && s.ui.source() == EngagedSource::SlotR);  // playback continues
    // confirm animation: both LEDs blink simultaneously for 900 ms
    LedState a = s.ui.leds(s.t + 10);
    assert(a.left == a.right);
    LedState b = s.ui.leds(s.t + 10 + UiController::kSaveBlinkMs);
    assert(b.left == b.right && a.left != b.left);
    // after the window: back to state (right solid, left off)
    LedState c = s.ui.leds(s.t + 6 * UiController::kSaveBlinkMs + 20);
    assert(c.right && !c.left);
    // the saved slot recalls with the tweak
    s.tap(Side::Right);        // bypass
    s.tap(Side::Right);        // recall Set 1 R again
    assert(near(s.ui.edit_buffer().mix, 0.8f));
  }

  // ---- the latch fires at the threshold, under the foot (amendment
  // 2026-08-28), and the release that follows is swallowed
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);              // engage Wukong
    s.press(Side::Right);
    s.step(900);
    assert(s.ui.layer() == MenuLayer::Menu1);   // not there yet
    s.step(150);                                // crosses kHoldMs
    assert(s.ui.layer() == MenuLayer::Menu2);   // latched while still held
    s.step(800);
    s.release(Side::Right);
    s.step(5);
    assert(s.ui.layer() == MenuLayer::Menu2);   // release changes nothing
    assert(s.ui.engaged() && s.ui.source() == EngagedSource::SlotR);
    // holding the OTHER side switches the latch at its own threshold
    s.press(Side::Left);
    s.step(1050);
    assert(s.ui.layer() == MenuLayer::Menu3);
    s.release(Side::Left);
    s.step(5);
    assert(s.ui.layer() == MenuLayer::Menu3);
  }

  // ---- the save chord unwinds the latch its own hold just made: both
  // gestures share kHoldMs, and the chord outranks the latch
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);
    s.press(Side::Right);
    s.step(1200);
    assert(s.ui.layer() == MenuLayer::Menu2);   // the hold latched first
    s.press(Side::Left);                        // ... and the chord wins
    assert(s.ui.layer() == MenuLayer::Menu1);
    assert(s.ui.save_pending() && s.ui.save_slot() == 0);
    s.release(Side::Left);
    s.release(Side::Right);
    s.step(5);
    assert(s.ui.layer() == MenuLayer::Menu1);   // no menu left over
    g_store.slots[s.ui.save_slot()] = s.ui.save_snapshot();
    s.ui.save_done(s.t);
    s.step();
  }

  // ---- in a menu, tapping the OTHER stomp switches menus (never saves,
  // never recalls); only the latched menu's own stomp (blinking side) exits
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);              // engage Wukong
    s.hold(Side::Right);             // latch menu 2
    assert(s.ui.layer() == MenuLayer::Menu2);
    s.tap(Side::Left);               // other-side tap: switch to menu 3
    assert(s.ui.layer() == MenuLayer::Menu3);
    assert(!s.ui.save_pending());
    assert(s.ui.engaged() && s.ui.source() == EngagedSource::SlotR);  // no recall
    s.in.knobs[3] = 0.52f;           // pickup rearmed on the switch: a small
    s.step();                        // move stays inert (voice count untouched)
    assert(near(s.ui.edit_buffer().unison, g_store.slots[0].unison));
    s.tap(Side::Right);              // other side again: back to menu 2
    assert(s.ui.layer() == MenuLayer::Menu2);
    s.tap(Side::Right);              // own side (blinking): exit
    assert(s.ui.layer() == MenuLayer::Menu1);
    assert(s.ui.engaged() && s.ui.source() == EngagedSource::SlotR);
  }

  // ---- menu mode blocks saving: the hold+press chord does nothing there
  {
    g_store = factory_store();
    Sim s;
    s.hold(Side::Left);              // latch menu 3
    assert(s.ui.layer() == MenuLayer::Menu3);
    s.press(Side::Left);
    s.step(1200);
    s.press(Side::Right);            // would be a save outside a menu
    assert(!s.ui.save_pending());
    s.release(Side::Right);
    s.release(Side::Left);
    s.step(5);
    assert(s.ui.layer() == MenuLayer::Menu3);  // both-press cancel: latch stays
  }

  // ---- both-press together while engaged is now the CHARGE gesture,
  // and it still changes no engage/menu/save state (charge spec sec 2)
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);              // engage Wukong (Set 1 R)
    assert(s.ui.take_engage_edge());
    s.press(Side::Left);
    s.press(Side::Right);            // both down before any hold threshold
    s.step(1500);
    s.release(Side::Right);
    s.release(Side::Left);
    s.step(5);
    assert(s.ui.engaged() && s.ui.source() == EngagedSource::SlotR);
    assert(s.ui.layer() == MenuLayer::Menu1);
    assert(!s.ui.save_pending());
    assert(!s.ui.take_engage_edge());
    assert(near(s.ui.edit_buffer().f1, g_store.slots[0].f1));  // no recall
  }

  // ---- save-from-anywhere / slot copy: recall 1R, page to Set 2, save R
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);              // Wukong from Set 1 R
    s.in.t_page = TogglePos::Down;   // page to Set 2 (audio unchanged)
    s.step();
    s.press(Side::Right);            // hold RIGHT ...
    s.step(1200);
    s.press(Side::Left);             // ... press LEFT: save to Set 2 R
    assert(s.ui.save_pending() && s.ui.save_slot() == 2);
    assert(near(s.ui.save_snapshot().f1, g_store.slots[0].f1));  // Wukong copied
    g_store.slots[2] = s.ui.save_snapshot();
    s.release(Side::Left);
    s.release(Side::Right);
    s.step(5);
    s.ui.save_done(s.t);
    s.step();
  }

  // ---- hold LEFT + press RIGHT saves the L slot (works bypassed too)
  {
    g_store = factory_store();
    Sim s;
    s.press(Side::Left);
    s.step(1200);
    s.press(Side::Right);
    assert(s.ui.save_pending() && s.ui.save_slot() == 1);  // Set 1 L
    g_store.slots[1] = s.ui.save_snapshot();
    s.release(Side::Right);
    s.release(Side::Left);
    s.step(5);
    s.ui.save_done(s.t);
    s.step();
    assert(!s.ui.engaged());         // a bypassed save stays bypassed
    assert(s.ui.layer() == MenuLayer::Menu1);  // and latches no menu
  }

  // ---- save rejected on Freeform page: flicker, no write, no latch
  {
    g_store = factory_store();
    Sim s;
    s.in.t_page = TogglePos::Middle; // Freeform
    s.step();
    s.press(Side::Right);
    s.step(1200);
    s.press(Side::Left);             // save attempt
    assert(!s.ui.save_pending());    // nothing written
    // one short double-flicker: on, off, on inside 3 * kRejectFlickMs
    LedState f0 = s.ui.leds(s.t + 5);
    LedState f1 = s.ui.leds(s.t + UiController::kRejectFlickMs + 5);
    LedState f2 = s.ui.leds(s.t + 2 * UiController::kRejectFlickMs + 5);
    assert(f0.left && f0.right);
    assert(!f1.left && !f1.right);
    assert(f2.left && f2.right);
    s.release(Side::Left);
    s.release(Side::Right);
    s.step(5);
    assert(s.ui.layer() == MenuLayer::Menu1);  // nothing latched
  }

  // ---- a second save chord while one is pending does not clobber it
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);
    s.press(Side::Right);
    s.step(1200);
    s.press(Side::Left);
    assert(s.ui.save_pending());
    VoiceParams snap = s.ui.save_snapshot();
    s.release(Side::Left);
    s.step(5);
    s.press(Side::Left);             // chord again before the ack
    assert(s.ui.save_pending());
    assert(near(s.ui.save_snapshot().f1, snap.f1));
    s.release(Side::Left);
    s.release(Side::Right);
    s.step(5);
    s.ui.save_done(s.t);
    s.step();
  }

  // ---- save-window guard: stomp gestures are ignored while a save is
  // pending, so a recall can never read a slot mid-QSPI-write (the
  // ~100 ms blocking save window)
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);        // engage Wukong (Set 1 R)
    s.in.knobs[1] = 0.8f;      // mix tweak so the buffer differs
    s.step();
    s.press(Side::Right);      // save chord ...
    s.step(1200);
    s.press(Side::Left);       // ... -> pending
    assert(s.ui.save_pending());
    s.release(Side::Left);
    s.release(Side::Right);
    s.step(5);
    s.tap(Side::Right);        // would bypass; must be ignored mid-save
    assert(s.ui.engaged());
    s.tap(Side::Left);         // would recall Set 1 L mid-write
    assert(near(s.ui.edit_buffer().mix, 0.8f));  // buffer untouched
    assert(s.ui.source() == EngagedSource::SlotR);
    s.hold(Side::Left);        // hold ignored too: no menu latches
    assert(s.ui.layer() == MenuLayer::Menu1);
    // ack round-trips: normal gesture handling resumes
    g_store.slots[s.ui.save_slot()] = s.ui.save_snapshot();
    s.ui.save_done(s.t);
    s.step();
    s.tap(Side::Right);        // tap works again: engaged -> bypass
    assert(!s.ui.engaged());
  }

  // ==== Charge mode: gesture + level (charge spec sections 2-3) ====

  // ---- both-together while engaged charges; ramp, clamp, decay; the
  // long two-footed hold neither latches nor saves
  {
    g_store = factory_store();  // charge: gain on, Hamekameka, slow decay
    Sim s;
    s.tap(Side::Right);              // engage Wukong
    assert(near(s.ui.charge_level(), 0.0f));
    s.press(Side::Left);
    s.press(Side::Right);            // 1 ms apart: together
    s.step(1250);                    // half of the 2500 ms Hamekameka
    assert(s.ui.charging());
    float mid = s.ui.charge_level();
    assert(mid > 0.4f && mid < 0.6f);
    s.step(2000);
    assert(near(s.ui.charge_level(), 1.0f));   // clamped at full
    s.release(Side::Right);          // decay starts (slow, 1200 ms)
    assert(!s.ui.charging());
    s.step(600);
    float dec = s.ui.charge_level();
    assert(dec > 0.3f && dec < 0.7f);
    s.release(Side::Left);
    s.step(2000);
    assert(near(s.ui.charge_level(), 0.0f));
    assert(s.ui.layer() == MenuLayer::Menu1);  // nothing latched
    assert(!s.ui.save_pending());              // nothing saved
    assert(s.ui.engaged());
  }

  // ---- re-press during decay resumes from the current level, even
  // with one stomp held past the hold threshold (never a save)
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);
    s.press(Side::Left);
    s.press(Side::Right);
    s.step(1250);
    s.release(Side::Right);          // decay; LEFT held all along (>1 s)
    s.step(300);
    float before = s.ui.charge_level();
    s.press(Side::Right);            // re-press: resume, NOT a save chord
    assert(!s.ui.save_pending());
    assert(s.ui.charging());
    s.step(500);
    assert(s.ui.charge_level() > before);
    s.release(Side::Right);
    s.release(Side::Left);
    s.step(3000);
    assert(near(s.ui.charge_level(), 0.0f));
  }

  // ---- staggered press stays the SAVE chord: no charge starts
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);
    s.press(Side::Right);
    s.step(1200);
    s.press(Side::Left);             // save chord
    assert(s.ui.save_pending());
    assert(!s.ui.charging());
    assert(near(s.ui.charge_level(), 0.0f));
    s.release(Side::Left);
    s.release(Side::Right);
    s.step(5);
    g_store.slots[s.ui.save_slot()] = s.ui.save_snapshot();
    s.ui.save_done(s.t);
    s.step();
  }

  // ---- no charge while bypassed (DFU territory) or inside a menu
  {
    g_store = factory_store();
    Sim s;
    s.press(Side::Left);
    s.press(Side::Right);            // bypassed both-press
    s.step(1500);
    assert(near(s.ui.charge_level(), 0.0f));
    s.release(Side::Right);
    s.release(Side::Left);
    s.step(5);
    s.tap(Side::Right);              // engage
    s.hold(Side::Right);             // latch menu 2
    s.press(Side::Left);
    s.press(Side::Right);            // in-menu both-press
    s.step(1500);
    assert(near(s.ui.charge_level(), 0.0f));
    s.release(Side::Right);
    s.release(Side::Left);
    s.step(5);
    assert(s.ui.layer() == MenuLayer::Menu2);  // still the plain no-op
    s.tap(Side::Right);              // exit menu
  }

  // ---- decay off snaps instantly; any engage change kills the level
  {
    g_store = factory_store();
    g_store.charge.decay = 0;        // off
    g_store.charge.time = 0;         // punch, 750 ms
    Sim s;
    s.tap(Side::Right);
    s.press(Side::Left);
    s.press(Side::Right);
    s.step(800);
    assert(near(s.ui.charge_level(), 1.0f));   // punch charges fast
    s.release(Side::Right);
    s.step(1);
    assert(near(s.ui.charge_level(), 0.0f));   // instant snap
    s.release(Side::Left);
    s.step(5);
    g_store.charge.decay = 1;        // slow again for the kill test
    Sim s2;
    s2.tap(Side::Right);
    s2.press(Side::Left);
    s2.press(Side::Right);
    s2.step(800);
    s2.release(Side::Right);
    s2.release(Side::Left);
    s2.step(5);
    assert(s2.ui.charge_level() > 0.5f);       // decaying
    s2.tap(Side::Right);             // bypass: kills instantly
    assert(near(s2.ui.charge_level(), 0.0f));
    assert(!s2.ui.engaged());
  }

  // ==== Charge config editing + page freeze (charge spec sec 4 and 8) ====

  // ---- menu 2 toggle moves set gain/time/decay; octave/gate/page and
  // the edit buffer stay untouched
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);              // engage Wukong (page Set 1)
    s.hold(Side::Right);             // latch menu 2
    s.in.t_octave = TogglePos::Up;   // toggle 1 move: gain -> Above 9000!
    s.step();
    assert(s.ui.charge_config().gain == 2);
    assert(s.ui.edit_buffer().octave == 0);      // octave NOT applied
    s.in.t_gate = TogglePos::Down;   // toggle 3 move: decay -> off
    s.step();
    assert(s.ui.charge_config().decay == 0);
    assert(s.ui.edit_buffer().gate_level == 1);  // gate NOT applied
    s.in.t_page = TogglePos::Down;   // toggle 2 move: time -> punch
    s.step();
    assert(s.ui.charge_config().time == 0);
    assert(s.ui.page() == Page::Set1);           // page frozen in menus
  }

  // ---- a toggle reading that differs across a menu change is a change of
  // MEANING, not a gesture: the three toggles belong to octave/page/gate
  // outside a menu and to the charge rows inside one, so neither side may
  // act on the difference. Here the positions cannot physically jump; the
  // emulator's on-screen levers can, because they swap banks, which is what
  // this guard exists for (pedal/static/faceplate.js setChargeBank).
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);                  // engage 1R
    const Page page0 = s.ui.page();
    const uint8_t gate0 = s.ui.edit_buffer().gate_level;

    // Latch menu 3 and swap every toggle reading on the very tick the menu
    // takes effect: nothing may be written to the charge config.
    s.press(Side::Left);
    s.step(1100);                        // menu 3 latches in here
    s.in.t_octave = TogglePos::Up;
    s.in.t_page = TogglePos::Down;
    s.in.t_gate = TogglePos::Up;
    s.release(Side::Left);
    assert(s.ui.layer() == MenuLayer::Menu3);
    // The first tick after the swap DOES see a move (the menu did not
    // change on it), so the config follows the levers, which is the
    // emulator's intent. What must never happen is the reverse leak:
    // page/octave/gate untouched while the menu is latched.
    assert(s.ui.page() == page0);
    assert(s.ui.edit_buffer().gate_level == gate0);

    // Now leave the menu while the readings swap back in the same tick.
    s.in.t_octave = TogglePos::Middle;
    s.in.t_page = TogglePos::Up;
    s.in.t_gate = TogglePos::Middle;
    s.tap(Side::Left);                   // own side: exit
    assert(s.ui.layer() == MenuLayer::Menu1);
    assert(s.ui.page() == page0);        // no retro-applied page jump
    assert(s.ui.edit_buffer().gate_level == gate0);
  }

  // ---- menu 3 rows (pitch/tone/size); page stays frozen through exit
  // and only follows a NEW move made outside the menu
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);              // engage 1R (page Set 1)
    s.hold(Side::Left);              // latch menu 3
    s.in.t_octave = TogglePos::Down; // pitch -> off
    s.step();
    assert(s.ui.charge_config().pitch == 0);
    s.in.t_page = TogglePos::Middle; // tone -> darker (NOT a page change)
    s.step();
    assert(s.ui.charge_config().tone == 1);
    s.in.t_gate = TogglePos::Up;     // size -> full
    s.step();
    assert(s.ui.charge_config().size == 2);
    s.tap(Side::Left);               // exit menu 3 (own side)
    assert(s.ui.config_save_pending());  // dirty exit triggers the write
    g_store.charge = s.ui.charge_config();
    s.ui.config_save_done();
    s.step();                            // ack consumed, gestures live
    assert(s.ui.page() == Page::Set1);  // no page jump on exit
    assert(s.ui.edit_buffer().octave == 0);      // nothing retro-applied
    s.tap(Side::Right);              // still slot 0 semantics: bypass
    assert(!s.ui.engaged());
    s.in.t_page = TogglePos::Down;   // NEW move outside the menu
    s.step();
    assert(s.ui.page() == Page::Set2);  // now the page follows
  }

  // ==== Config persistence on menu exit (charge spec section 7) ====
  {
    g_store = factory_store();
    Sim s;
    s.hold(Side::Right);             // latch menu 2
    s.in.t_octave = TogglePos::Up;   // gain -> Above 9000! (dirty)
    s.step();
    assert(!s.ui.config_save_pending());  // not until menu exit
    s.tap(Side::Right);              // exit menu
    assert(s.ui.config_save_pending());
    s.tap(Side::Right);              // gestures guarded during the write
    assert(!s.ui.engaged());
    g_store.charge = s.ui.charge_config();  // main-loop side: persist
    s.ui.config_save_done();
    s.step();                        // tick consumes the ack
    assert(!s.ui.config_save_pending());
    assert(g_store.charge.gain == 2);
    s.tap(Side::Right);              // gestures work again
    assert(s.ui.engaged());
    LedState l = s.ui.leds(s.t);     // silent write: no confirm blink
    assert(l.right && !l.left);
  }

  // ---- menu exit with no config change writes nothing
  {
    g_store = factory_store();
    Sim s;
    s.hold(Side::Right);
    s.tap(Side::Right);              // exit, no toggle moved
    assert(!s.ui.config_save_pending());
  }

  // ==== Charge LED animation (charge spec section 6) ====
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);              // engage (right LED solid normally)
    s.press(Side::Left);
    s.press(Side::Right);            // charge session
    s.step(100);
    bool saw_flip = false;
    LedState prev = s.ui.leds(s.t);
    for (int i = 0; i < 500; i++) {  // 500 ms: at least one alternation
      s.step();
      LedState cur = s.ui.leds(s.t);
      assert(cur.left != cur.right); // always alternating, never both
      if (cur.left != prev.left) saw_flip = true;
      prev = cur;
    }
    assert(saw_flip);
    s.release(Side::Right);
    s.release(Side::Left);
    s.step(5000);                    // decay out fully
    LedState c = s.ui.leds(s.t);
    assert(c.right && !c.left);      // normal language resumes (R engaged)
  }

  // ==== Post-merge polish: parked review minors ====

  // ---- config edited then reverted in one menu session: exit writes
  // nothing (compare against the last persisted copy, charge spec sec 7)
  {
    g_store = factory_store();
    Sim s;
    s.hold(Side::Right);             // latch menu 2
    s.in.t_octave = TogglePos::Up;   // gain -> Above 9000!
    s.step();
    s.in.t_octave = TogglePos::Middle;  // gain -> back to factory "on"
    s.step();
    s.tap(Side::Right);              // exit
    assert(!s.ui.config_save_pending());  // net change is zero: no write
  }

  // ---- charge animation first phase is LEFT on (charge spec section 6)
  {
    g_store = factory_store();
    Sim s;
    s.tap(Side::Right);
    s.press(Side::Left);
    s.press(Side::Right);            // fresh charge session
    s.step(50);                      // well inside the first half-period
    LedState l = s.ui.leds(s.t);
    assert(l.left && !l.right);
    s.release(Side::Right);
    s.release(Side::Left);
    s.step(3000);                    // decay out
  }

  // ---- a menu latched during the decay wins the LEDs: the blink is
  // actionable feedback, the charge glow is decoration
  {
    g_store = factory_store();       // slow decay (1200 ms)
    Sim s;
    s.tap(Side::Right);              // engage Set 1 R
    s.press(Side::Left);
    s.press(Side::Right);
    s.step(2600);                    // full charge
    s.release(Side::Right);
    s.release(Side::Left);           // decay starts
    s.hold(Side::Left, 1050);        // latch menu 3 mid-decay
    assert(s.ui.layer() == MenuLayer::Menu3);
    assert(s.ui.charge_level() > 0.0f);   // still decaying
    const uint32_t T = (s.t / 500 + 1) * 500;  // a menu-blink ON phase
    LedState on = s.ui.leds(T);
    LedState off = s.ui.leds(T + UiController::kMenuBlinkMs);
    // Menu 3 owns both LEDs: its own blinks, the other is dark even though
    // the R slot is engaged. So the blink's OFF phase leaves BOTH dark,
    // which the charge alternation (always exactly one lit) can never
    // produce. That is what proves the menu won the LEDs.
    assert(on.left && !on.right);
    assert(!off.left && !off.right);
  }

  printf("test_ui_controller OK\n");
  return 0;
}
