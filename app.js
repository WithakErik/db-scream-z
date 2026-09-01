// app.js - wiring. Ticks the ported control surface against the faceplate,
// pushes the resulting voice at the audio graph, and draws the result.
//
// The tick runs on requestAnimationFrame rather than per audio block. The
// firmware ticks in the audio callback (about 1 ms), but every threshold
// the state machine cares about is a gesture threshold: a 1000 ms hold, a
// 250 ms blink, a 60 ms flicker phase. A 16 ms frame resolves all of them,
// and the charge ramp integrates real elapsed time rather than counting
// ticks, so its speed does not depend on the frame rate.

import {
  Side, TogglePos, EngagedSource,
  factoryStore, kVoiceStoreVersion, voiceName, cloneVoice,
} from './voice-params.js';
import { kKnobLabels, knobValueText, knobPositions } from './param-map.js';
import { applyCharge, kChargeLabels } from './charge.js';
import { UiController, Menu } from './ui-controller.js';
import { Faceplate } from './faceplate.js';
import { AudioEngine, encodeWav } from './audio.js';

const STORE_KEY = 'dbscreamz.voicestore.v3';

// ---- the QSPI stand-in -----------------------------------------------
// The pedal keeps four slots and the charge config in QSPI flash, written
// only on save. localStorage is the same contract: survives a reload,
// never touched except by a save.
function loadStore() {
  try {
    const raw = localStorage.getItem(STORE_KEY);
    if (raw) {
      const s = JSON.parse(raw);
      if (s && s.version === kVoiceStoreVersion) return s;
    }
  } catch { /* private mode, cleared data, corrupt JSON: fall through */ }
  return factoryStore();
}

function persist(store) {
  try { localStorage.setItem(STORE_KEY, JSON.stringify(store)); }
  catch { /* nothing we can do, and the session still works */ }
}

// ---- setup ------------------------------------------------------------
const $ = (id) => document.getElementById(id);
const store = loadStore();
const face = new Faceplate($('stage'));
const ui = new UiController();
const audio = new AudioEngine();

const now = () => performance.now();
ui.init(store, face.inputs(now()));

// ---- motorised pots ---------------------------------------------------
// The pedal cannot move its own knobs, so after a layer change or a slot
// recall its six pots point at whatever they pointed at before, and knob
// pickup keeps them inert until nudged. On screen there is nothing
// stopping us from moving them, so we do: enter a menu and the knobs swing
// to the values that layer actually holds.
//
// Pickup is still re-armed at the new positions, so nothing takes over
// until you move it. It just has nothing left to protect you from, because
// the knob and the parameter now agree.
let lastLayer = ui.layer();
let lastEditRef = ui.editBuffer();   // identity changes on a recall

function syncKnobsToLayer() {
  face.knobPos = knobPositions(ui.layer(), ui.editBuffer());
  ui.pickup.rearm(face.knobPos);
}

syncKnobsToLayer();   // and at boot, so the panel reads true on arrival

let note = '';
let noteWarn = false;
let noteUntil = 0;
function say(text, warn = false, ms = 4000) {
  note = text; noteWarn = warn; noteUntil = now() + ms;
}

let live = { f0: 0, gateOpen: false };
audio.onLiveState = (s) => { live = s; };

// ---- charge and save, the two gestures a pointer cannot make ----------
const chargeBtn = $('charge');
chargeBtn.addEventListener('pointerdown', (e) => {
  chargeBtn.setPointerCapture(e.pointerId);
  chargeBtn.classList.add('held');
  if (!ui.engaged()) {
    say('Charge needs a voice engaged. Bypassed, both stomps together is ' +
        'the firmware update gesture instead.', true);
    return;
  }
  face.setBothDown(true);
  e.preventDefault();
});
const chargeUp = (e) => {
  chargeBtn.classList.remove('held');
  face.setBothDown(false);
  try { chargeBtn.releasePointerCapture(e.pointerId); } catch { /* ok */ }
};
chargeBtn.addEventListener('pointerup', chargeUp);
chargeBtn.addEventListener('pointercancel', chargeUp);

$('saveL').addEventListener('click', () => ui.requestSave(Side.Left, now()));
$('saveR').addEventListener('click', () => ui.requestSave(Side.Right, now()));

$('reset').addEventListener('click', () => {
  const f = factoryStore();
  store.version = f.version;
  store.slots = f.slots;
  store.charge = f.charge;
  persist(store);
  ui.init(store, face.inputs(now()));
  say('Factory voices and charge settings restored.');
});

// ---- audio source -----------------------------------------------------
const sourceSel = $('source'), deviceSel = $('device');
const playBtn = $('play'), renderBtn = $('render'), fileInput = $('file');
let sourceKind = 'clip';

$('start').addEventListener('click', async () => {
  try {
    await audio.start();
    $('start').disabled = true;
    $('start').textContent = 'Audio running';
    sourceSel.disabled = false;
    await selectSource(sourceSel.value);
  } catch (err) {
    say(`Could not start audio: ${err.message}`, true, 8000);
  }
});

async function selectSource(value) {
  audio.disconnectSource();
  playBtn.disabled = true; renderBtn.disabled = true;
  deviceSel.hidden = true;
  try {
    if (value === 'live') {
      sourceKind = 'live';
      await audio.useLiveInput(deviceSel.value || undefined);
      const devs = await audio.inputDevices();
      deviceSel.innerHTML = '';
      for (const d of devs) {
        const o = document.createElement('option');
        o.value = d.deviceId;
        o.textContent = d.label || 'Input device';
        deviceSel.appendChild(o);
      }
      deviceSel.hidden = devs.length < 2;
      say('Live input running. Play something.');
    } else if (value === 'file') {
      fileInput.click();
    } else {
      sourceKind = 'clip';
      await audio.loadClip(value.slice('clip:'.length));
      audio.playBuffer(0, true);
      playBtn.disabled = false; renderBtn.disabled = false;
      playBtn.textContent = 'Pause';
    }
  } catch (err) {
    say(`Source failed: ${err.message}`, true, 8000);
  }
}

sourceSel.addEventListener('change', () => selectSource(sourceSel.value));
deviceSel.addEventListener('change', () => selectSource('live'));

fileInput.addEventListener('change', async () => {
  const f = fileInput.files && fileInput.files[0];
  if (!f) return;
  try {
    sourceKind = 'file';
    await audio.loadFile(f);
    audio.playBuffer(0, true);
    playBtn.disabled = false; renderBtn.disabled = false;
    playBtn.textContent = 'Pause';
    say(`${f.name} loaded. The pitch tracker is monophonic, so a single ` +
        'instrument tracks best.');
  } catch (err) {
    say(`Could not decode that file: ${err.message}`, true, 8000);
  }
});

playBtn.addEventListener('click', () => {
  if (audio.playing) {
    audio.stopBuffer();
    playBtn.textContent = 'Play';
  } else {
    audio.playBuffer(audio.offsetAt, true);
    playBtn.textContent = 'Pause';
  }
});

renderBtn.addEventListener('click', async () => {
  renderBtn.disabled = true;
  const was = renderBtn.textContent;
  renderBtn.textContent = 'Rendering…';
  try {
    // Render what you are hearing: the charge overlay included, frozen at
    // whatever the charge level is right now.
    const voice = applyCharge(ui.editBuffer(), ui.chargeConfig(),
                              ui.chargeLevel, ui.charging);
    const buf = await audio.render(voice);
    const url = URL.createObjectURL(encodeWav(buf));
    const a = document.createElement('a');
    a.href = url;
    a.download = `dbscreamz-${voiceName(ui.editBuffer()).toLowerCase()}.wav`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
    say('Rendered.');
  } catch (err) {
    say(`Render failed: ${err.message}`, true, 8000);
  } finally {
    renderBtn.textContent = was;
    renderBtn.disabled = false;
  }
});

// ---- labels -----------------------------------------------------------
const OCT = { [TogglePos.Up]: '+1', [TogglePos.Middle]: '0', [TogglePos.Down]: '-1' };
const PAGE = { [TogglePos.Up]: 'Set 1', [TogglePos.Middle]: 'Freeform', [TogglePos.Down]: 'Set 2' };
const GATE = { [TogglePos.Up]: 'high', [TogglePos.Middle]: 'med', [TogglePos.Down]: 'low' };

function toggleView() {
  const m = ui.menuLatched();
  if (m === Menu.Menu2) {
    const c = ui.chargeConfig();
    return {
      labels: ['Gain', 'Time', 'Decay'],
      values: [kChargeLabels.gain[c.gain], kChargeLabels.time[c.time],
               kChargeLabels.decay[c.decay]],
      charge: 'right',   // menu 2 is the RIGHT stomp: orange, like LED 2
    };
  }
  if (m === Menu.Menu3) {
    const c = ui.chargeConfig();
    return {
      labels: ['Pitch', 'Tone', 'Aspir'],
      values: [kChargeLabels.pitch[c.pitch], kChargeLabels.tone[c.tone],
               kChargeLabels.aspir[c.aspir]],
      charge: 'left',    // menu 3 is the LEFT stomp: blue, like LED 1
    };
  }
  return {
    labels: ['Octave', 'Page', 'Gate'],
    values: [OCT[face.toggles[0]], PAGE[face.toggles[1]], GATE[face.toggles[2]]],
    charge: null,
  };
}

function statusText() {
  const src = ui.source();
  const where = src === EngagedSource.None ? 'bypassed'
    : src === EngagedSource.Freeform ? 'freeform'
    : `${voiceName(ui.editBuffer())} ${src === EngagedSource.SlotL ? 'L' : 'R'}`;
  const menu = ui.menuLatched() === Menu.Menu2 ? ' · menu 2'
    : ui.menuLatched() === Menu.Menu3 ? ' · menu 3' : '';
  const chg = ui.chargeLevel > 0
    ? ` · charge ${Math.round(ui.chargeLevel * 100)}%` : '';
  const f0 = live.f0 > 20 ? ` · ${live.f0.toFixed(1)} Hz` : '';
  const gate = live.gateOpen ? '' : ' · gated';
  return `<b>${where}</b>${menu}${chg}${f0}${gate}`;
}

// ---- the loop ---------------------------------------------------------
// Charge config value (0/1/2) to the lever position that means it. The
// mapping is uniform across every row: Up = 2, Middle = 1, Down = 0.
const CHARGE_POS = [TogglePos.Down, TogglePos.Middle, TogglePos.Up];

// The three charge rows of the latched menu, as lever positions.
function chargeTogglePositions() {
  const c = ui.chargeConfig();
  const keys = ui.menuLatched() === Menu.Menu2
    ? ['gain', 'time', 'decay']
    : ['pitch', 'tone', 'aspir'];
  return keys.map((k) => CHARGE_POS[c[k]]);
}

function frame() {
  const t = now();
  // Hand the levers to the charge rows while a menu is latched, so they
  // show and edit the setting under them rather than sitting wherever
  // octave/page/gate left them. Must run before inputs().
  const latched = ui.menuLatched() !== Menu.None;
  face.setChargeBank(latched, latched ? chargeTogglePositions() : []);
  ui.tick(face.inputs(t));

  // Save handshakes: on hardware these cross to the main loop because the
  // QSPI write blocks for ~100 ms. Here they complete immediately, but the
  // handshake shape is kept so the guard window and confirm blink behave.
  if (ui.savePending) {
    store.slots[ui.saveSlot()] = cloneVoice(ui.saveSnapshot());
    persist(store);
    ui.saveDone(t);
    say(`Saved to ${ui.saveSlot() % 2 === 0 ? 'R' : 'L'} of ` +
        `${ui.saveSlot() < 2 ? 'Set 1' : 'Set 2'}.`);
  }
  if (ui.configSavePending) {
    store.charge = { ...ui.chargeConfig() };
    persist(store);
    ui.configSaveDone();
  }

  if (ui.takeEngageEdge()) audio.engageEdge();

  // A latch, a menu switch, a menu exit or a recall: swing the knobs to
  // match whatever the active layer now holds.
  if (ui.layer() !== lastLayer || ui.editBuffer() !== lastEditRef) {
    lastLayer = ui.layer();
    lastEditRef = ui.editBuffer();
    syncKnobsToLayer();
  }

  // The charge overlay is a pure copy; the edit buffer is never written.
  const voice = applyCharge(ui.editBuffer(), ui.chargeConfig(),
                            ui.chargeLevel, ui.charging);
  audio.setVoice(voice, ui.engaged());

  const layer = ui.layer();
  const tv = toggleView();
  // Grey a knob's readout only when the knob is genuinely not pointing at
  // the value shown. Since the knobs are synced on every layer change that
  // is now rare, but it is still the truthful test: pickup has not taken
  // over AND the pointer disagrees with the parameter.
  const truePos = knobPositions(layer, ui.editBuffer());
  const agrees = (i) => Math.abs(face.knobPos[i] - truePos[i]) < 0.005;
  // While charging, sweep the pointers with the overlay so the panel shows
  // the sound: the readouts already track the charged voice, and a pointer
  // frozen at the resting value next to a climbing number reads as a bug.
  // Display only, and the pointers land back on the knobs as it decays.
  // The real pedal cannot do this either; same call as the toggle levers.
  const charging = ui.chargeLevel > 0;
  const shownPos = charging ? knobPositions(layer, voice) : null;
  face.render({
    leds: ui.leds(t),
    knobLabels: kKnobLabels[layer],
    knobValues: [0, 1, 2, 3, 4, 5].map((i) => knobValueText(layer, i, voice)),
    knobPos: shownPos,
    // Charging, the pointer is drawn FROM the value shown, so the two agree
    // by construction and greying them would be a lie.
    knobLive: [0, 1, 2, 3, 4, 5].map((i) => charging || ui.pickup.live(i) || agrees(i)),
    toggleLabels: tv.labels,
    toggleValues: tv.values,
    chargeMenu: tv.charge,
  });

  chargeBtn.classList.toggle('armed', ui.engaged());
  $('status').innerHTML = statusText();
  if (note && t > noteUntil) { note = ''; noteWarn = false; }
  const noteEl = $('note');
  if (noteEl.dataset.shown !== note) {
    noteEl.dataset.shown = note;
    noteEl.textContent = note || defaultNote();
    noteEl.classList.toggle('warn', noteWarn);
  }
  requestAnimationFrame(frame);
}

function defaultNote() {
  if (ui.menuLatched() !== Menu.None)
    return 'Menu latched: the knobs edit formants, and the toggles now set ' +
           'charge mode. Tap the blinking side to exit.';
  if (!ui.engaged())
    return 'Bypassed. Set the middle toggle to Set 1 or Set 2, then tap a stomp.';
  return 'Hold a stomp about a second to latch its menu. Both at once is ' +
         'CHARGE, which is the button below because one pointer cannot ' +
         'press two switches.';
}

requestAnimationFrame(frame);
