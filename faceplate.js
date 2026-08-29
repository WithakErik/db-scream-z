// faceplate.js - the pedal itself, drawn as SVG.
//
// Geometry follows the enclosure layout the booklet cards use
// (manual/cards/*.svg): two rows of three knobs, three toggle slots, two
// LEDs, two footswitches. The knob and toggle positions here are PHYSICAL
// positions: like real pots and real switches they stay where they were
// left, and never jump when a slot is recalled. That is the whole reason
// knob pickup exists, so modelling it any other way would hide the
// behaviour this page is meant to teach.

import { TogglePos } from './voice-params.js';

const NS = 'http://www.w3.org/2000/svg';

// Layout, in the 60 x 112 viewBox.
const KNOB_X = [15, 30, 45];
const KNOB_CY = [25, 45];
const KNOB_R = 4;
const TOG_X = [13, 28, 43];
const TOG_Y = 58, TOG_W = 4, TOG_H = 12;
const LED_CY = 84, LED_R = 1.6;
const FS_CY = 98, FS_R = 6.5;
const SIDE_X = [15, 45];

const LED_BLUE = '#2f7fe0';
const LED_ORANGE = '#ff7a18';

function el(name, attrs, parent) {
  const e = document.createElementNS(NS, name);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (parent) parent.appendChild(e);
  return e;
}

export class Faceplate {
  constructor(container) {
    // Physical control state.
    this.knobPos = [0.5, 0.5, 0.5, 0.5, 0.5, 0.5];
    this.toggles = [TogglePos.Middle, TogglePos.Up, TogglePos.Middle];
    this.leftDown = false;
    this.rightDown = false;
    this.onStompChange = () => {};

    const svg = el('svg', {
      viewBox: '0 0 60 112', class: 'pedal',
      xmlns: NS, 'aria-label': 'DBscreamZ pedal',
    }, container);
    this.svg = svg;

    // ---- enclosure ----
    // Four layers, because the panel is a photograph of painted artwork and
    // the controls have to stay readable on top of it:
    //   base   a flat fill, so a failed image load still looks like a pedal
    //   art    the faceplate photo, clipped to the rounded enclosure
    //   scrim  a wash that knocks the ink back under the labels
    //   frame  the outline, stroked last so the art cannot bleed over it
    const ENC = { x: 0.6, y: 0.6, width: 58.8, height: 110.8 };
    const CORNER = { rx: 3.5 };
    const defs = el('defs', {}, svg);
    const clip = el('clipPath', { id: 'enclosure-clip' }, defs);
    el('rect', { ...ENC, ...CORNER }, clip);
    el('rect', { ...ENC, ...CORNER, class: 'enclosure-base' }, svg);
    // The photo is 1536x2752 against a 58.8x110.8 panel, so `slice` covers
    // the panel and crops a sliver off the sides rather than letterboxing.
    el('image', {
      href: 'images/faceplate.jpg', ...ENC,
      preserveAspectRatio: 'xMidYMid slice',
      'clip-path': 'url(#enclosure-clip)', class: 'enclosure-art',
    }, svg);
    el('rect', { ...ENC, ...CORNER, class: 'enclosure-scrim' }, svg);
    el('rect', { ...ENC, ...CORNER, class: 'enclosure' }, svg);

    // Wordmark (600x206 -> aspect 2.913), spanning the knob row exactly:
    // from the left edge of the Mix knob to the right edge of Master, so
    // the panel reads as one column instead of a small badge over a wide
    // block of controls.
    const LOGO_X = KNOB_X[0] - KNOB_R;
    const LOGO_W = KNOB_X[2] + KNOB_R - LOGO_X;
    el('image', {
      href: 'images/logo.png',
      x: LOGO_X, y: 3.6, width: LOGO_W, height: LOGO_W / 2.913,
      preserveAspectRatio: 'xMidYMid meet',
    }, svg);

    // ---- knobs ----
    this.knobs = [];
    for (let i = 0; i < 6; i++) {
      const cx = KNOB_X[i % 3];
      const cy = KNOB_CY[Math.floor(i / 3)];
      const g = el('g', { class: 'knob', tabindex: '0',
                          role: 'slider', 'aria-label': `Knob ${i + 1}` }, svg);
      el('circle', { cx, cy, r: KNOB_R, class: 'knob-body' }, g);
      const ptr = el('line', {
        x1: cx, y1: cy, x2: cx, y2: cy - KNOB_R, class: 'knob-ptr',
      }, g);
      const dot = el('circle', { cx, cy, r: 0.55, class: 'knob-dot' }, g);
      const label = el('text', {
        x: cx, y: cy + KNOB_R + 2.6, class: 'knob-label',
      }, svg);
      const value = el('text', {
        x: cx, y: cy + KNOB_R + 5.2, class: 'knob-value',
      }, svg);
      this.knobs.push({ g, ptr, dot, label, value, cx, cy, i });
      this.wireKnob(this.knobs[i]);
    }

    // ---- toggles ----
    this.toggleEls = [];
    for (let t = 0; t < 3; t++) {
      const x = TOG_X[t];
      const g = el('g', { class: 'toggle', tabindex: '0',
                          role: 'radiogroup',
                          'aria-label': `Toggle ${t + 1}` }, svg);
      el('rect', { x, y: TOG_Y, width: TOG_W, height: TOG_H, rx: 1,
                   class: 'toggle-slot' }, g);
      for (let d = 0; d < 3; d++)
        el('line', { x1: x + TOG_W, y1: TOG_Y + 2 + d * 4,
                     x2: x + TOG_W + 1, y2: TOG_Y + 2 + d * 4,
                     class: 'detent' }, g);
      const lever = el('circle', {
        cx: x + TOG_W / 2, cy: TOG_Y + 6, r: 1.5, class: 'toggle-lever',
      }, g);
      const label = el('text', {
        x: x + TOG_W / 2, y: TOG_Y + TOG_H + 3.4, class: 'toggle-label',
      }, svg);
      const value = el('text', {
        x: x + TOG_W / 2, y: TOG_Y + TOG_H + 6.0, class: 'toggle-value',
      }, svg);
      this.toggleEls.push({ g, lever, label, value, x, t });
      this.wireToggle(this.toggleEls[t]);
    }

    // ---- LEDs ----
    this.leds = [];
    for (let s = 0; s < 2; s++) {
      const cx = SIDE_X[s];
      el('circle', { cx, cy: LED_CY, r: LED_R + 0.5, class: 'led-bezel' }, svg);
      this.leds.push(el('circle', {
        cx, cy: LED_CY, r: LED_R, class: 'led',
      }, svg));
    }

    // ---- footswitches ----
    this.stomps = [];
    for (let s = 0; s < 2; s++) {
      const cx = SIDE_X[s];
      const g = el('g', { class: 'stomp', tabindex: '0', role: 'button',
                          'aria-label': s === 0 ? 'Left footswitch'
                                                : 'Right footswitch' }, svg);
      el('circle', { cx, cy: FS_CY, r: FS_R, class: 'stomp-ring' }, g);
      el('circle', { cx, cy: FS_CY, r: FS_R - 1.6, class: 'stomp-cap' }, g);
      el('text', { x: cx, y: FS_CY + 1.4, class: 'stomp-label' }, g)
        .textContent = s === 0 ? 'L' : 'R';
      this.stomps.push(g);
      this.wireStomp(g, s);
    }
  }

  // ---- input wiring ----

  wireKnob(k) {
    let dragging = false, startY = 0, startPos = 0;
    const rect = () => this.svg.getBoundingClientRect();
    k.g.addEventListener('pointerdown', (e) => {
      dragging = true;
      startY = e.clientY;
      startPos = this.knobPos[k.i];
      k.g.setPointerCapture(e.pointerId);
      e.preventDefault();
    });
    k.g.addEventListener('pointermove', (e) => {
      if (!dragging) return;
      // A full sweep is about half the pedal's on-screen height, which
      // makes fine adjustment possible without a modifier key.
      const span = rect().height * 0.5;
      const dy = startY - e.clientY;
      this.knobPos[k.i] = Math.min(1, Math.max(0, startPos + dy / span));
    });
    const stop = (e) => {
      if (!dragging) return;
      dragging = false;
      try { k.g.releasePointerCapture(e.pointerId); } catch { /* already gone */ }
    };
    k.g.addEventListener('pointerup', stop);
    k.g.addEventListener('pointercancel', stop);
    k.g.addEventListener('wheel', (e) => {
      e.preventDefault();
      const step = e.shiftKey ? 0.002 : 0.02;
      this.knobPos[k.i] = Math.min(1, Math.max(0,
        this.knobPos[k.i] - Math.sign(e.deltaY) * step));
    }, { passive: false });
    k.g.addEventListener('keydown', (e) => {
      const step = e.shiftKey ? 0.002 : 0.02;
      if (e.key === 'ArrowUp' || e.key === 'ArrowRight')
        this.knobPos[k.i] = Math.min(1, this.knobPos[k.i] + step);
      else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft')
        this.knobPos[k.i] = Math.max(0, this.knobPos[k.i] - step);
      else return;
      e.preventDefault();
    });
  }

  wireToggle(t) {
    const set = (pos) => { this.toggles[t.t] = pos; };
    t.g.addEventListener('pointerdown', (e) => {
      const box = t.g.getBoundingClientRect();
      const frac = (e.clientY - box.top) / box.height;
      set(frac < 0.34 ? TogglePos.Up
        : frac < 0.67 ? TogglePos.Middle : TogglePos.Down);
      e.preventDefault();
    });
    t.g.addEventListener('keydown', (e) => {
      const order = [TogglePos.Up, TogglePos.Middle, TogglePos.Down];
      const at = order.indexOf(this.toggles[t.t]);
      if (e.key === 'ArrowUp') set(order[Math.max(0, at - 1)]);
      else if (e.key === 'ArrowDown') set(order[Math.min(2, at + 1)]);
      else return;
      e.preventDefault();
    });
  }

  wireStomp(g, side) {
    const down = (v) => (e) => {
      if (side === 0) this.leftDown = v; else this.rightDown = v;
      if (v) { try { g.setPointerCapture(e.pointerId); } catch { /* ok */ } }
      this.onStompChange();
      e.preventDefault();
    };
    g.addEventListener('pointerdown', down(true));
    g.addEventListener('pointerup', down(false));
    g.addEventListener('pointercancel', down(false));
    g.addEventListener('keydown', (e) => {
      if (e.key !== ' ' && e.key !== 'Enter') return;
      if (e.repeat) return;
      if (side === 0) this.leftDown = true; else this.rightDown = true;
      e.preventDefault();
    });
    g.addEventListener('keyup', (e) => {
      if (e.key !== ' ' && e.key !== 'Enter') return;
      if (side === 0) this.leftDown = false; else this.rightDown = false;
      e.preventDefault();
    });
  }

  // Force both stomps down, for the CHARGE button (one pointer cannot press
  // two switches). Returns to normal on release.
  setBothDown(v) { this.leftDown = v; this.rightDown = v; }

  // ---- what the controller reads each tick ----
  inputs(nowMs) {
    return {
      left_down: this.leftDown,
      right_down: this.rightDown,
      t_octave: this.toggles[0],
      t_page: this.toggles[1],
      t_gate: this.toggles[2],
      knobs: this.knobPos.slice(),
      now_ms: nowMs,
    };
  }

  // ---- drawing ----
  render(view) {
    for (let i = 0; i < 6; i++) {
      const k = this.knobs[i];
      // 270 degrees of travel: 7 o'clock at 0, 5 o'clock at 1.
      const deg = this.knobPos[i] * 270 - 135;
      const rad = (deg * Math.PI) / 180;
      k.ptr.setAttribute('x2', k.cx + KNOB_R * 0.82 * Math.sin(rad));
      k.ptr.setAttribute('y2', k.cy - KNOB_R * 0.82 * Math.cos(rad));
      k.label.textContent = view.knobLabels[i];
      // An inert knob shows its parameter's value greyed: the number is
      // what the voice is doing, not what the knob is pointing at.
      k.value.textContent = view.knobValues[i];
      k.g.classList.toggle('inert', !view.knobLive[i]);
      k.value.classList.toggle('inert', !view.knobLive[i]);
    }
    for (let t = 0; t < 3; t++) {
      const e = this.toggleEls[t];
      const pos = this.toggles[t];
      e.lever.setAttribute('cy',
        TOG_Y + (pos === TogglePos.Up ? 2 : pos === TogglePos.Down ? 10 : 6));
      e.label.textContent = view.toggleLabels[t];
      e.value.textContent = view.toggleValues[t];
      // Levers take the latched menu's LED colour, so the panel and the
      // lit LED agree about which menu you are in: left = blue, right =
      // orange, and the default near-black outside a menu.
      e.g.classList.toggle('charge-left', view.chargeMenu === 'left');
      e.g.classList.toggle('charge-right', view.chargeMenu === 'right');
    }
    // color as well as fill: the lit-LED glow is a drop-shadow in
    // currentColor, which reads `color`, not `fill`.
    this.leds[0].style.fill = view.leds.left ? LED_BLUE : '';
    this.leds[0].style.color = view.leds.left ? LED_BLUE : '';
    this.leds[0].classList.toggle('on', view.leds.left);
    this.leds[1].style.fill = view.leds.right ? LED_ORANGE : '';
    this.leds[1].style.color = view.leds.right ? LED_ORANGE : '';
    this.leds[1].classList.toggle('on', view.leds.right);
    this.stomps[0].classList.toggle('down', this.leftDown);
    this.stomps[1].classList.toggle('down', this.rightDown);
  }
}
