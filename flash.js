// flash.js - the flash page's DOM layer. Every decision is in
// flash-core.js, which pedal/tests/flash-core.test.mjs and
// flash-page.test.mjs cover; this file only wires it to the page.
//
// dfu.js and dfuse.js (vendor/webdfu/) are classic scripts loaded by
// flash.html before this module; they define the globals dfu and dfuse.

import {
  FlashError, loadRelease, findDfuInterface, flash, controlsFor, explain,
} from './flash-core.js';

const REPO = 'https://github.com/WithakErik/db-scream-z';
const lib = { dfu: globalThis.dfu, dfuse: globalThis.dfuse };
const $ = (id) => document.getElementById(id);

let release = null;
let device = null;

function setState(state) {
  const c = controlsFor(state);
  $('connect').disabled = !c.connect;
  $('flash').disabled = !c.flash;
}

function say(text) { $('status').textContent = text; }

function clearError() {
  $('error').hidden = true;
  for (const el of document.querySelectorAll('[data-help]')) el.hidden = true;
}

function showError(err) {
  const x = explain(err);
  if (!x) return;
  $('error-text').textContent = x.message;
  $('error').hidden = false;
  for (const el of document.querySelectorAll('[data-help]')) {
    el.hidden = !x.help.includes(el.dataset.help);
  }
}

function onProgress({ phase, done, total }) {
  const bar = $('bar');
  bar.hidden = false;
  bar.max = total || 1;
  bar.value = done;
  if (phase === 'erase') say('Erasing the old firmware...');
  else if (phase === 'write') say(`Writing ${done} of ${total} bytes...`);
}

async function init() {
  setState('loading');
  try {
    release = await loadRelease();
  } catch (e) {
    setState('no-release');
    say('');
    // Nothing has touched USB yet, so never show the USB help here.
    showError(e instanceof FlashError ? e : new FlashError('no-release',
      'The published firmware could not be loaded. Reload the page to try again.'));
    return;
  }
  const v = release.version;
  $('version').textContent =
    `${v.commit.slice(0, 7)}, built ${v.date.slice(0, 10)}, ${v.size} bytes`;
  $('source').href = `${REPO}/tree/${v.commit}`;
  $('source').hidden = false;
  if (!navigator.usb) {
    setState('unsupported');
    $('unsupported').hidden = false;
    say('');
    return;
  }
  setState('ready');
  say('Put the pedal in DFU mode, then press Connect.');
}

$('connect').addEventListener('click', async () => {
  clearError();
  let usb;
  try {
    usb = await navigator.usb.requestDevice({ filters: [{ vendorId: 0x0483 }] });
  } catch (e) {
    if (explain(e)) { showError(e); return; }
    // The picker was closed with nothing chosen: say why that happens, and
    // reveal the Windows driver note the line points at.
    say('Pedal not in the list? It must be in DFU mode (bypassed, both ' +
      'stomps held about 2 seconds) and on a data cable, not a ' +
      'charge-only one. On Windows, see the driver note below.');
    $('error-text').textContent = '';
    $('error').hidden = false;
    for (const el of document.querySelectorAll('[data-help]')) {
      el.hidden = el.dataset.help !== 'windows';
    }
    return;
  }
  try {
    device = await findDfuInterface(usb, lib);
    setState('connected');
    say('Pedal connected in DFU mode. Press Flash.');
  } catch (e) {
    device = null;
    setState('ready');
    showError(e);
  }
});

$('flash').addEventListener('click', async () => {
  if (!device || !release) return;
  clearError();
  setState('flashing');
  try {
    await flash(device, release.image, onProgress);
    setState('done');
    say(`Flashed ${release.version.commit.slice(0, 7)}. The pedal restarts ` +
      'on its own and disappears from USB, which is expected: its firmware ' +
      'does not use USB. Your saved settings are kept. If it has not restarted after a few seconds, power cycle it.');
  } catch (e) {
    setState('ready');
    say('');
    showError(e instanceof FlashError ? e : new FlashError('flash-failed', String(e)));
  }
  device = null;
});

init();
