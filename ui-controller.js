// ui-controller.js - port of firmware/hothouse/ui_controller.hpp.
//
// The control-surface state machine, unchanged in behaviour from the pedal:
// gesture timing, menu latching, knob pickup, the save chord, charge mode
// and the LED language all follow the C++ line for line. ../tests asserts
// the same cases firmware/host/tests/test_ui_controller.cpp does.
//
// Physical mapping: FOOTSWITCH_1 / LED_1 = LEFT, FOOTSWITCH_2 / LED_2 =
// RIGHT. So menu 2 = hold RIGHT, menu 3 = hold LEFT.
//
// Taps fire on release; holds fire the moment the press crosses kHoldMs,
// so the menu comes up under your foot. The save chord shares that
// threshold, so it arrives after its own hold has already latched: it
// unwinds that latch before saving. See unwindHoldLatch().
//
// The firmware splits this across an audio ISR and the main loop, which is
// why the save handshake is a pair of flags rather than a call. Nothing in
// the browser needs that, but the flags are kept: they are what produces
// the save guard window and the confirm blink, both of which are visible
// behaviour.

import {
  Side, Page, TogglePos, EngagedSource, MenuLayer,
  kChargeTimesMs, kDecayTimesMs,
  cloneVoice, chargeConfigsEqual, slotIndex,
} from './voice-params.js';
import { applyKnob } from './param-map.js';
import { KnobPickup } from './knob-pickup.js';

const Menu = { None: 'None', Menu2: 'Menu2', Menu3: 'Menu3' };

export class UiController {
  static kHoldMs = 1000;        // latch threshold
  static kMenuBlinkMs = 250;    // menu blink half-period
  static kSaveBlinkMs = 150;    // save confirm half-period
  static kRejectFlickMs = 60;   // reject flicker phase

  init(store, input) {
    this.store = store;
    this.page = UiController.pageFrom(input.t_page);
    const load = this.page === Page.Freeform ? Page.Set1 : this.page;
    this.edit = cloneVoice(store.slots[slotIndex(load, Side.Right)]);
    this.src = EngagedSource.None;
    this.engagedSlot = -1;
    this.menu = Menu.None;
    this.engageEdge = false;
    this.savePending = false;
    this.saveAck = false;
    this.saveAckMs = 0;
    this.saveSlotIdx = -1;
    this.saveSnap = null;
    this.configSavePending = false;
    this.configSaveAck = false;
    this.confirmStart = 0;
    this.rejectStart = 0;
    this.lastOctave = input.t_octave;
    this.lastPage = input.t_page;
    this.lastGate = input.t_gate;
    this.lastMenu = this.menu;
    this.persistedConfig = { ...store.charge };
    this.leftDown = input.left_down;
    this.rightDown = input.right_down;
    this.leftStart = this.rightStart = input.now_ms;
    this.bothCancel = input.left_down && input.right_down;
    this.holdLatched = false;
    this.holdLatchedSide = Side.Left;
    this.menuBeforeHold = Menu.None;
    this.config = { ...store.charge };
    this.chargeLevel = 0.0;
    this.charging = false;
    this.chargeSession = false;
    this.lastNow = input.now_ms;
    this.chargeLedFlip = false;
    this.chargeLedMs = 0;
    this.pickup = new KnobPickup();
    this.pickup.rearm(input.knobs);
  }

  tick(input) {
    // Save handshake ack from the host: all menus exit, both LEDs blink 3
    // times, playback continues.
    if (this.saveAck) {
      this.saveAck = false;
      this.savePending = false;
      this.confirmStart = this.saveAckMs;
    }
    if (this.configSaveAck) {
      this.configSaveAck = false;
      this.configSavePending = false;
      this.persistedConfig = { ...this.config };   // the write landed
    }

    // ---- stomp gestures: tap = release < kHoldMs, hold = longer ----
    // Save-window guard: while a save is pending, ignore taps and holds
    // entirely so a recall can never read a slot mid-write.
    const saveGuard = this.savePending || this.configSavePending;
    const leftEdge = input.left_down && !this.leftDown;
    const rightEdge = input.right_down && !this.rightDown;
    if (leftEdge) this.leftStart = input.now_ms;
    if (rightEdge) this.rightStart = input.now_ms;
    // Second-press dispatch: save chord vs charge session vs nothing. Runs
    // BEFORE bothCancel is raised so a fresh together-press is
    // distinguishable from an ongoing both-down state.
    if (leftEdge && input.right_down) this.onSecondPress(Side.Right, input);
    else if (rightEdge && input.left_down) this.onSecondPress(Side.Left, input);
    if (input.left_down && input.right_down) this.bothCancel = true;
    // A release while a charge session holds both stomps ends the ramp and
    // starts the decay; the release itself is swallowed below.
    if (this.chargeSession && ((!input.left_down && this.leftDown) ||
                              (!input.right_down && this.rightDown)))
      this.charging = false;
    // The latch fires here, under the foot, the tick the press crosses
    // kHoldMs. Only a single-footed hold can latch: both down is charge
    // (or the firmware-update gesture), and the save window ignores
    // gestures outright.
    if (!this.holdLatched && !this.bothCancel && !saveGuard) {
      if (input.left_down && input.now_ms - this.leftStart >= UiController.kHoldMs)
        this.armHold(Side.Left, input);
      else if (input.right_down && input.now_ms - this.rightStart >= UiController.kHoldMs)
        this.armHold(Side.Right, input);
    }
    // Releases: a latched hold's release is swallowed, and so is one that
    // crossed kHoldMs while a guard was up (the gesture was ignored, it
    // does not become a tap on the way out). Everything shorter is a tap.
    if (!input.left_down && this.leftDown) {
      if (this.holdLatched && this.holdLatchedSide === Side.Left) {
        this.holdLatched = false;
      } else if (!this.bothCancel && !saveGuard &&
                 input.now_ms - this.leftStart < UiController.kHoldMs) {
        this.onTap(Side.Left, input);
      }
    }
    if (!input.right_down && this.rightDown) {
      if (this.holdLatched && this.holdLatchedSide === Side.Right) {
        this.holdLatched = false;
      } else if (!this.bothCancel && !saveGuard &&
                 input.now_ms - this.rightStart < UiController.kHoldMs) {
        this.onTap(Side.Right, input);
      }
    }
    if (!input.left_down && !input.right_down) {
      this.bothCancel = false;
      this.chargeSession = false;
      this.holdLatched = false;
    }
    this.leftDown = input.left_down;
    this.rightDown = input.right_down;

    // ---- toggles ----
    // Outside menus: a page move selects the page; octave/gate moves (or
    // freeform live-tracking) write the edit buffer. Inside a latched menu:
    // toggle MOVES edit the global charge config instead. The page is
    // move-event-driven so a config edit can never page-switch on exit.
    // A toggle only counts as MOVED when the menu did not change on this
    // same tick. Latching or leaving a menu hands the three toggles to a
    // different set of parameters, so a reading that differs across that
    // boundary is a change of meaning, not a gesture, and must not be
    // applied to either side. On hardware the positions cannot jump and
    // this only costs the one tick the menu changes in; the emulator needs
    // it because its levers swap banks (faceplate.js setChargeBank).
    const menuChanged = this.menu !== this.lastMenu;
    if (menuChanged) {
      // fall through to the re-baseline below: no move this tick
    } else if (this.menu === Menu.None) {
      if (input.t_page !== this.lastPage) this.page = UiController.pageFrom(input.t_page);
      const freeformLive = this.src === EngagedSource.Freeform;
      if (input.t_octave !== this.lastOctave || freeformLive)
        this.edit.octave = UiController.octaveFrom(input.t_octave);
      if (input.t_gate !== this.lastGate || freeformLive)
        this.edit.gate_level = UiController.gateFrom(input.t_gate);
    } else {
      if (input.t_octave !== this.lastOctave) this.setChargeField(0, input.t_octave);
      if (input.t_page !== this.lastPage) this.setChargeField(1, input.t_page);
      if (input.t_gate !== this.lastGate) this.setChargeField(2, input.t_gate);
    }
    this.lastMenu = this.menu;
    this.lastOctave = input.t_octave;
    this.lastPage = input.t_page;
    this.lastGate = input.t_gate;

    // ---- knobs: pickup, then route into the active layer ----
    const lay = this.layer();
    for (let i = 0; i < 6; i++)
      if (this.pickup.update(i, input.knobs[i]))
        applyKnob(lay, i, input.knobs[i], this.edit);

    // ---- charge level ----
    const dt = input.now_ms - this.lastNow;
    this.lastNow = input.now_ms;
    if (this.charging) {
      this.chargeLevel += dt / kChargeTimesMs[this.config.time];
      if (this.chargeLevel > 1.0) this.chargeLevel = 1.0;
    } else if (this.chargeLevel > 0.0) {
      const d = kDecayTimesMs[this.config.decay];
      if (d === 0) {
        this.chargeLevel = 0.0;
      } else {
        this.chargeLevel -= dt / d;
        if (this.chargeLevel < 0.0) this.chargeLevel = 0.0;
      }
    }
    if (this.chargeLevel > 0.0) {
      // LED alternation clock: the half-period shrinks from ~400 ms at
      // level 0 to ~80 ms at full charge, and runs backwards on the decay.
      this.chargeLedMs += dt;
      const half = Math.trunc(400.0 - 320.0 * this.chargeLevel);
      if (this.chargeLedMs >= half) {
        this.chargeLedMs = 0;
        this.chargeLedFlip = !this.chargeLedFlip;
      }
    }
  }

  editBuffer() { return this.edit; }
  engaged() { return this.src !== EngagedSource.None; }
  source() { return this.src; }
  currentPage() { return this.page; }
  chargeConfig() { return this.config; }
  menuLatched() { return this.menu; }

  layer() {
    if (this.menu === Menu.Menu2) return MenuLayer.Menu2;
    if (this.menu === Menu.Menu3) return MenuLayer.Menu3;
    return MenuLayer.Menu1;
  }

  // True exactly once per bypass -> engage transition. The host must then
  // clear the engine's output state (burp fix) and ramp in.
  takeEngageEdge() {
    const e = this.engageEdge;
    this.engageEdge = false;
    return e;
  }

  // ---- save handshake (host side) ----
  saveSlot() { return this.saveSlotIdx; }
  saveSnapshot() { return this.saveSnap; }
  saveDone(nowMs) { this.saveAckMs = nowMs; this.saveAck = true; }
  configSaveDone() { this.configSaveAck = true; }

  // Bootloader gesture armed ONLY while bypassed. In the browser there is
  // nothing to reboot into, so this only drives the refusal message.
  bootloaderArmed() { return this.src === EngagedSource.None; }

  // The emulator's stand-in for the save chord. On the pedal you hold one
  // stomp past kHoldMs and press the other while still holding; one mouse
  // pointer cannot do that, so the page offers a button per side instead.
  // It enters at exactly the same place the chord does, so the menu guard,
  // the save-window guard and the Freeform rejection all still apply.
  requestSave(side, nowMs) {
    this.onSaveChord(side, { now_ms: nowMs });
  }

  // LED language. Pure function of state + time.
  leds(nowMs) {
    if (this.confirmStart !== 0 &&
        nowMs - this.confirmStart < 6 * UiController.kSaveBlinkMs) {
      const on = Math.trunc((nowMs - this.confirmStart) / UiController.kSaveBlinkMs) % 2 === 0;
      return { left: on, right: on };   // 3 simultaneous blinks
    }
    if (this.rejectStart !== 0 &&
        nowMs - this.rejectStart < 3 * UiController.kRejectFlickMs) {
      const ph = Math.trunc((nowMs - this.rejectStart) / UiController.kRejectFlickMs);
      const on = ph !== 1;   // on-off-on: one short double-flicker
      return { left: on, right: on };
    }
    if (this.chargeLevel > 0.0 && this.menu === Menu.None) {
      // The charge animation overrides the normal language while active,
      // except when a menu is latched: the blink is actionable feedback,
      // the charge glow is decoration.
      return { left: this.chargeLedFlip, right: !this.chargeLedFlip };
    }
    const l = {
      left: this.src === EngagedSource.SlotL || this.src === EngagedSource.Freeform,
      right: this.src === EngagedSource.SlotR || this.src === EngagedSource.Freeform,
    };
    const blink = Math.trunc(nowMs / UiController.kMenuBlinkMs) % 2 === 0;
    // A latched menu owns BOTH LEDs (amended 2026-08-28): the menu's side
    // blinks and the other goes dark, even when that side is the engaged
    // slot. One blinking plus one solid read as "both lit" at a glance.
    if (this.menu === Menu.Menu2) { l.right = blink; l.left = false; }
    if (this.menu === Menu.Menu3) { l.left = blink; l.right = false; }
    return l;
  }

  // ---- internals ----

  static pageFrom(t) {
    if (t === TogglePos.Up) return Page.Set1;
    if (t === TogglePos.Down) return Page.Set2;
    return Page.Freeform;
  }
  static octaveFrom(t) {
    if (t === TogglePos.Up) return 1;
    if (t === TogglePos.Down) return -1;
    return 0;
  }
  static gateFrom(t) {
    if (t === TogglePos.Up) return 2;    // high
    if (t === TogglePos.Down) return 0;  // low
    return 1;                            // medium
  }
  // Uniform charge-config value mapping: Up = 2, Middle = 1, Down = 0.
  static chargeVal(t) {
    if (t === TogglePos.Up) return 2;
    if (t === TogglePos.Down) return 0;
    return 1;
  }
  static menuOf(side) { return side === Side.Right ? Menu.Menu2 : Menu.Menu3; }

  // Toggle move while a menu is latched edits the global charge config.
  // Menu 2: gain/time/decay. Menu 3: pitch/tone/size.
  setChargeField(toggle, pos) {
    const v = UiController.chargeVal(pos);
    const keys = this.menu === Menu.Menu2
      ? ['gain', 'time', 'decay']
      : ['pitch', 'tone', 'size'];
    // Dirtiness is judged at menu exit against persistedConfig.
    this.config[keys[toggle]] = v;
  }

  // Remembers which side is holding and what the menu was before it
  // latched, so onSecondPress can unwind the latch for a save chord.
  armHold(side, input) {
    this.holdLatched = true;
    this.holdLatchedSide = side;
    this.menuBeforeHold = this.menu;
    this.onHold(side, input);
  }

  onHold(side, input) {
    // Hold latches that side's menu at the threshold, not on release.
    // Holding the OTHER side while a menu is latched switches the latch.
    const m = UiController.menuOf(side);
    if (this.menu !== m) {
      this.menu = m;
      this.pickup.rearm(input.knobs);   // layer change
    }
  }

  // Undo the latch the still-held `held` side just made, back to whatever
  // menu was up before it. The save chord's own hold now always trips the
  // latch first, and "menus never save" would otherwise eat every save.
  unwindHoldLatch(held, input) {
    if (!this.holdLatched || this.holdLatchedSide !== held) return;
    this.holdLatched = false;
    if (this.menu !== this.menuBeforeHold) {
      this.menu = this.menuBeforeHold;
      this.pickup.rearm(input.knobs);   // layer change
    }
  }

  // Save chord: second press edge while `held` has been down past kHoldMs.
  // Menus never save; the target slot side is the held side.
  onSaveChord(held, input) {
    if (this.menu !== Menu.None || this.savePending || this.configSavePending) return;
    if (this.page === Page.Freeform) {
      this.rejectStart = input.now_ms;   // "pick a page first"
      return;
    }
    this.saveSlotIdx = slotIndex(this.page, held);
    this.saveSnap = cloneVoice(this.edit);
    this.savePending = true;   // host persists, then saveDone()
  }

  // Called on a press edge while the other side (`held`) is also down.
  // Resume an ongoing charge session, fire the save chord, or start a
  // fresh charge session.
  onSecondPress(held, input) {
    if (this.chargeSession) {   // re-press before both released: resume
      this.charging = true;
      return;
    }
    if (this.bothCancel) return;   // ongoing non-charge both-down: no-op
    const start = held === Side.Left ? this.leftStart : this.rightStart;
    if (input.now_ms - start >= UiController.kHoldMs) {
      this.unwindHoldLatch(held, input);   // the chord outranks its own latch
      this.onSaveChord(held, input);
      return;
    }
    // Both down together: charge, armed only engaged, outside menus, with
    // no save window open.
    if (this.src !== EngagedSource.None && this.menu === Menu.None &&
        !this.savePending && !this.configSavePending) {
      this.chargeSession = true;
      this.charging = true;
      this.chargeLedFlip = true;   // first animation phase: LEFT on
      this.chargeLedMs = 0;        // a resume keeps its phase
    }
  }

  onTap(side, input) {
    // Menu mode: tapping the OTHER stomp switches to that side's menu;
    // only the latched menu's own stomp (the blinking side) exits. Menus
    // never save; saving is only ever the hold+press chord, outside menus.
    if (this.menu !== Menu.None) {
      const m = UiController.menuOf(side);
      this.menu = this.menu === m ? Menu.None : m;
      // On exit, persist only a NET config change: an edit reverted in the
      // same session is not a change.
      if (this.menu === Menu.None &&
          !chargeConfigsEqual(this.config, this.persistedConfig))
        this.configSavePending = true;   // host persists, silently
      this.pickup.rearm(input.knobs);    // layer change either way
      return;
    }
    // Any engage-state change kills the charge instantly: every path below
    // bypasses, engages, or recalls.
    this.charging = false;
    this.chargeLevel = 0.0;
    // No menu latched: engage / bypass / recall.
    if (this.page === Page.Freeform) {
      if (this.src !== EngagedSource.None) {
        this.src = EngagedSource.None;   // engaged -> bypass
        this.engagedSlot = -1;
      } else {
        this.src = EngagedSource.Freeform;   // edit buffer as-is, no load
        this.engageEdge = true;
      }
      return;
    }
    const target = slotIndex(this.page, side);
    if (this.src !== EngagedSource.None && this.engagedSlot === target) {
      this.src = EngagedSource.None;   // tap again = bypass
      this.engagedSlot = -1;
      return;
    }
    // Recall + engage: copy the slot into the edit buffer (discards unsaved
    // tweaks, by design).
    const wasBypassed = this.src === EngagedSource.None;
    this.edit = cloneVoice(this.store.slots[target]);
    this.engagedSlot = target;
    this.src = side === Side.Right ? EngagedSource.SlotR : EngagedSource.SlotL;
    if (wasBypassed) this.engageEdge = true;
    this.pickup.rearm(input.knobs);   // source change
  }
}

export { Menu };
