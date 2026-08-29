#!/usr/bin/env node
'use strict';
const fs = require('fs'), path = require('path');

const LAB = path.join(__dirname, '..', 'dbscreamz_lab', 'static');

// ---- load the worklet headlessly (do NOT modify the lab file) ----
globalThis.sampleRate = 48000;
globalThis.AudioWorkletProcessor = class {
  constructor() { this.port = { onmessage: null, postMessage() {} }; }
};
let CLS;
globalThis.registerProcessor = (n, c) => { CLS = c; };
eval(fs.readFileSync(path.join(LAB, 'fof-processor.js'), 'utf8'));

// ---- create processor instance ----
const proc = new CLS();
proc.p.quantize = 1;

// ---- frequency sweep: f = 16 * 2^(k/96) for k = 0..640 ----
// This gives 8 steps/semitone, sampling quantizer rounding edges well
for (let k = 0; k <= 640; k++) {
  const f = 16 * Math.pow(2, k / 96);
  const quantized = proc.quantizeHz(f);

  // amp_comp formula from fof-processor.js lines 1023-1027
  const note = 12 * Math.log2(f / 440) + 69;
  const ampcomp = Math.min(3, Math.max(0.1, (note - 12) * (-1 / 72) + 2));

  console.log(`${f},${quantized},${ampcomp}`);
}
