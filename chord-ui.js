// chord-ui.js - port of firmware/hothouse/chord_ui.hpp.
//
// Chord mode's control surface (chord mode design spec sections 3-4). A
// separate class from UiController on purpose: chord mode has no slots, no
// pages, no save chord and a momentary right stomp.
//
//   left tap          engage / bypass (in the menu: leave the menu)
//   left hold ~1 s    latch the chord menu, under the foot
//   right, held       open mouth (momentary); never a menu, never a save
//   both together     charge, engaged and outside the menu only
//   toggle 3          gate level; toggles 1 and 2 do nothing
//
// Storage is automatic: the one chord setting is saved kAutoSaveMs after the
// last change, or at once on leaving the menu, and only when it differs
// from what was last saved.
//
// The firmware splits tick()/takeEngageEdge() (audio ISR) from
// savePending/saveSnapshot()/saveDone()/leds()/bootloaderArmed() (main
// loop); nothing in the browser needs that split, but the same handshake
// flags are kept because they are what produces the save guard window.

import { ChordLayer, applyChordKnob } from './chord-map.js';
import { KnobPickup } from './knob-pickup.js';
import { TogglePos, chordsEqual, kChargeTimesMs, kDecayTimesMs } from './voice-params.js';

// now_ms and every timestamp it is compared against are uint32_t in the
// firmware: a real millis() counter that wraps every ~49.7 days. `>>> 0`
// reproduces that wraparound so a delta computed from an out-of-order
// now_ms behaves the same as on hardware instead of going negative.
// test_chord_ui.cpp's leds(0) call (ported below) deliberately passes a
// now_ms earlier than boot_ms_ to read the menu blink phase, relying on
// exactly this wraparound to skip the boot-flash branch.
const udelta = (a, b) => (a - b) >>> 0;

export class ChordUiController {
  static kHoldMs = 1000;       // same latch as normal mode
  static kMenuBlinkMs = 250;
  static kBootFlashMs = 150;   // 3 flashes on entry
  static kAutoSaveMs = 3000;
  // A live knob is re-applied only when it has moved more than this from
  // where it was last applied: the hardware ADC wanders by a few LSB every
  // block, and without a deadband that wander restarts the 3 s settle on
  // every tick so main-layer edits never auto-save (final review,
  // 2026-09-25). Mirrors chord_ui.hpp.
  static kChordKnobDeadband = 1.0 / 256.0;

  init(saved, charge, inputs) {
    this._chord = { ...saved };
    this.persisted = { ...saved };  // last saved
    this.config = { ...charge };
    this._engaged = false;
    this.menu = false;
    this.engageEdge = false;
    this.leftDown = inputs.left_down;
    this.rightDown = inputs.right_down;
    this.leftStart = inputs.now_ms;
    // The boot grip (both stomps held through power-up) is not a gesture:
    // treat it like any other both-down so its releases are swallowed.
    this.bothCancel = inputs.left_down && inputs.right_down;
    this.holdLatched = false;
    this._mouthOpen = false;
    this.lastGate = inputs.t_gate;
    this.bootMs = inputs.now_ms;
    this.lastChangeMs = inputs.now_ms;
    this.exitSave = false;
    this.savePending = false;
    this.saveAck = false;
    this.saveSnap = { ...saved };
    this.chargeLevel = 0.0;
    this.charging = false;
    this.chargeSession = false;
    this.lastNow = inputs.now_ms;
    this.chargeLedFlip = false;
    this.chargeLedMs = 0;
    this.pickup = new KnobPickup();
    this.pickup.rearm(inputs.knobs);
    this.applied = [0, 0, 0, 0, 0, 0];  // position each knob last applied
  }

  tick(inputs) {
    if (this.saveAck) {
      this.saveAck = false;
      this.savePending = false;
      this.persisted = { ...this.saveSnap };  // the write landed: new baseline
    }

    // ---- stomps ----
    const leftEdge = inputs.left_down && !this.leftDown;
    const rightEdge = inputs.right_down && !this.rightDown;
    if (leftEdge) this.leftStart = inputs.now_ms;
    if ((leftEdge && inputs.right_down) || (rightEdge && inputs.left_down)) {
      if (this.chargeSession) {
        this.charging = true;  // re-press before both released: resume
      } else if (!this.bothCancel && this._engaged && !this.menu) {
        this.chargeSession = true;
        this.charging = true;
        this.chargeLedFlip = true;
        this.chargeLedMs = 0;
      }
    }
    if (inputs.left_down && inputs.right_down) this.bothCancel = true;
    if (this.chargeSession && ((!inputs.left_down && this.leftDown) ||
                               (!inputs.right_down && this.rightDown)))
      this.charging = false;
    // Only the LEFT stomp latches, at the threshold, under the foot. The
    // right stomp is the mouth and is routinely held past kHoldMs.
    if (!this.holdLatched && !this.bothCancel && inputs.left_down &&
        udelta(inputs.now_ms, this.leftStart) >= ChordUiController.kHoldMs) {
      this.holdLatched = true;
      if (!this.menu) {
        this.menu = true;
        this.pickup.rearm(inputs.knobs);  // layer change
      }
    }
    if (!inputs.left_down && this.leftDown) {
      if (this.holdLatched) {
        this.holdLatched = false;  // the latching hold's release is swallowed
      } else if (!this.bothCancel &&
                 udelta(inputs.now_ms, this.leftStart) < ChordUiController.kHoldMs) {
        this.onLeftTap(inputs);
      }
    }
    if (!inputs.left_down && !inputs.right_down) {
      this.bothCancel = false;
      this.chargeSession = false;
      this.holdLatched = false;
    }
    // A both-press closes the mouth until both stomps are up, except after
    // a charge: once left is let go with right still held, the mouth opens
    // again (final review, 2026-09-25). bothCancel itself stays set, so the
    // menu-exit swallow and the hold latch are unchanged.
    this._mouthOpen = inputs.right_down &&
                      (!this.bothCancel || (this.chargeSession && !inputs.left_down));
    this.leftDown = inputs.left_down;
    this.rightDown = inputs.right_down;

    // ---- toggle 3 and knobs; note whether anything actually changed ----
    const before = { ...this._chord };
    if (inputs.t_gate !== this.lastGate)
      this._chord.gate_level = ChordUiController.gateFrom(inputs.t_gate);
    this.lastGate = inputs.t_gate;
    const lay = this.menu ? ChordLayer.Menu : ChordLayer.Main;
    for (let i = 0; i < 6; i++) {
      const wasLive = this.pickup.live(i);
      if (!this.pickup.update(i, inputs.knobs[i])) continue;
      const d = inputs.knobs[i] - this.applied[i];
      // The pickup itself always applies (the rearm made the knob inert,
      // so this is the first application since the layer change).
      const db = ChordUiController.kChordKnobDeadband;
      if (!wasLive || d > db || d < -db) {
        this.applied[i] = inputs.knobs[i];
        applyChordKnob(lay, i, inputs.knobs[i], this._chord);
      }
    }
    if (!chordsEqual(before, this._chord)) this.lastChangeMs = inputs.now_ms;

    // ---- auto-save (spec section 4) ----
    if (!this.savePending) {
      const dirty = !chordsEqual(this._chord, this.persisted);
      if (!dirty) {
        this.exitSave = false;
      } else if (this.exitSave ||
                 udelta(inputs.now_ms, this.lastChangeMs) >= ChordUiController.kAutoSaveMs) {
        this.saveSnap = { ...this._chord };
        this.exitSave = false;
        this.savePending = true;  // host persists, then saveDone()
      }
    }

    // ---- charge level: same ramp as UiController (charge spec sec 3) ----
    const dt = udelta(inputs.now_ms, this.lastNow);
    this.lastNow = inputs.now_ms;
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
      this.chargeLedMs += dt;
      const half = Math.trunc(400.0 - 320.0 * this.chargeLevel);
      if (this.chargeLedMs >= half) {
        this.chargeLedMs = 0;
        this.chargeLedFlip = !this.chargeLedFlip;
      }
    }
  }

  chord() { return this._chord; }
  engaged() { return this._engaged; }
  mouthOpen() { return this._mouthOpen; }
  inMenu() { return this.menu; }
  takeEngageEdge() {
    const e = this.engageEdge;
    this.engageEdge = false;
    return e;
  }
  chargeConfig() { return this.config; }
  bootloaderArmed() { return !this._engaged; }  // ONLY while bypassed

  saveSnapshot() { return this.saveSnap; }
  saveDone() { this.saveAck = true; }

  layer() { return this.menu ? ChordLayer.Menu : ChordLayer.Main; }

  // LED language. Pure function of state + time.
  leds(nowMs) {
    if (udelta(nowMs, this.bootMs) < 6 * ChordUiController.kBootFlashMs) {
      const on = Math.trunc(udelta(nowMs, this.bootMs) / ChordUiController.kBootFlashMs) % 2 === 0;
      return { left: on, right: on };  // "you are in chord mode"
    }
    if (this.chargeLevel > 0.0 && !this.menu)
      return { left: this.chargeLedFlip, right: !this.chargeLedFlip };
    if (this.menu)
      return { left: Math.trunc(nowMs / ChordUiController.kMenuBlinkMs) % 2 === 0, right: false };
    return { left: this._engaged, right: this._mouthOpen };
  }

  // ---- internals ----

  static gateFrom(t) {
    if (t === TogglePos.Up) return 2;    // high
    if (t === TogglePos.Down) return 0;  // low
    return 1;                            // medium
  }

  onLeftTap(inputs) {
    if (this.menu) {
      this.menu = false;
      this.exitSave = true;
      this.pickup.rearm(inputs.knobs);  // layer change
      return;
    }
    this.charging = false;  // any engage-state change kills the charge
    this.chargeLevel = 0.0;
    if (this._engaged) {
      this._engaged = false;
    } else {
      this._engaged = true;
      this.engageEdge = true;
    }
  }
}
