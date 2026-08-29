#!/usr/bin/env node
'use strict';
const fs = require('fs'), path = require('path');

const LAB = path.join(__dirname, '..', 'dbscreamz_lab', 'static');

// ---- minimal 16-bit PCM WAV ----
function readWav(p) {
  const b = fs.readFileSync(p);
  if (b.toString('ascii', 0, 4) !== 'RIFF') throw new Error('not RIFF');
  let off = 12, fmt = null, data = null;
  while (off + 8 <= b.length) {
    const id = b.toString('ascii', off, off + 4), sz = b.readUInt32LE(off + 4);
    if (id === 'fmt ') fmt = { ch: b.readUInt16LE(off + 10), sr: b.readUInt32LE(off + 12), bits: b.readUInt16LE(off + 22) };
    if (id === 'data') data = b.subarray(off + 8, off + 8 + sz);
    off += 8 + sz + (sz & 1);
  }
  if (!fmt || !data || fmt.bits !== 16) throw new Error('need 16-bit PCM');
  const n = data.length / 2 / fmt.ch, out = new Float32Array(n);
  for (let i = 0; i < n; i++) {           // average all channels (mono downmix)
    let s = 0;
    for (let c = 0; c < fmt.ch; c++) s += data.readInt16LE((i * fmt.ch + c) * 2);
    out[i] = s / fmt.ch / 32768.0;
  }
  return { sr: fmt.sr, samples: out };
}
function writeWav(p, sr, x) {
  const n = x.length, b = Buffer.alloc(44 + n * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2, 4); b.write('WAVE', 8);
  b.write('fmt ', 12); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20);
  b.writeUInt16LE(1, 22); b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * 2, 28);
  b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34);
  b.write('data', 36); b.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++)
    b.writeInt16LE(Math.round(Math.max(-1, Math.min(1, x[i])) * 32767), 44 + i * 2);
  fs.writeFileSync(p, b);
}

// ---- CLI usage ----
if (process.argv.length < 4) {
  console.error('usage: node tools/ref_render.js <in.wav> <out.wav|-> --character <Name> ' +
    '[--trace f.csv] [--dump-grains g.f32] [--dump-frontend fe.csv] [--set key=value ...]');
  process.exit(1);
}

// ---- load the worklet headlessly (do NOT modify the lab file) ----
const input = readWav(process.argv[2]);
globalThis.sampleRate = input.sr;
globalThis.AudioWorkletProcessor = class {
  constructor() { this.port = { onmessage: null, postMessage() {} }; }
};
let CLS;
globalThis.registerProcessor = (n, c) => { CLS = c; };
eval(fs.readFileSync(path.join(LAB, 'fof-processor.js'), 'utf8'));

// ---- args ----
const args = process.argv.slice(2);
const outPath = args[1];
function opt(name) { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : null; }
const character = opt('--character') || 'Wukong';

// ---- v12 param set: app.js P defaults + applyPreset() baking ----
const presets = JSON.parse(fs.readFileSync(path.join(LAB, 'presets.json'), 'utf8'));
const pr = presets[character];
if (!pr) {
  console.error(`unknown --character '${character}'; available: ${Object.keys(presets).join(', ')}`);
  process.exit(1);
}
const params = {
  f1: pr.formants_hz[0], f2: pr.formants_hz[1], f3: pr.formants_hz[2],
  bw1: 32.5, bw2: 47.5, bw3: 62.5, a1: 1.0, a2: 1.0, a3: 1.0,
  formantScale: 1.0, grainMs: 20, unison: 3, detuneCents: 11, aspiration: 0.0,
  targetF0: pr.f0_hz, loF0: Math.max(120, pr.f0_lo), hiF0: pr.f0_hi,
  octaveShift: 0, followPitch: 1, quantize: 1, ampComp: 1, glideMs: 0,
  registerMode: 0, taperEnd: 1, leveler: 1, inputGain: 4.0, gate: 0.02,
  fastTrack: 0, useBacf: 1, vibRate: pr.vib_rate_hz, vibDepth: pr.vib_depth_semi,
  vibJitter: 0.10, gain: 1.0, breathiness: 0,
};
for (let i = 0; i < args.length; i++)          // --set key=value overrides
  if (args[i] === '--set') {
    const [k, v] = args[i + 1].split('=');
    params[k] = Number(v);
  }

const proc = new CLS();
proc.onMsg({ type: 'params', values: params });
proc.onMsg({ type: 'live', on: 1 });
proc.onMsg({ type: 'transport', playing: 1 });

// grain dump: force build, write all 8 voices
const grainsPath = opt('--dump-grains');
if (grainsPath) {
  proc.buildGrain();
  const n = proc.grainLen;
  const flat = new Float32Array(8 * n);
  for (let u = 0; u < 8; u++) flat.set(proc.grains[u], u * n);
  fs.writeFileSync(grainsPath, Buffer.from(flat.buffer));
  fs.writeFileSync(grainsPath.replace(/\.f32$/, '') + '_meta.txt', `grainLen=${n}\n`);
}

// ---- render in 128-sample blocks ----
const N = input.samples.length, out = new Float32Array(N);
const tracePath = opt('--trace'), fePath = opt('--dump-frontend');
const traceRows = ['block,f0,amp'], feRows = ['sample,envFast,liveAmp,gateGain'];
const blk = new Float32Array(128), oblk = new Float32Array(128);
for (let b = 0; b * 128 < N; b++) {
  const s0 = b * 128, n = Math.min(128, N - s0);
  blk.fill(0); oblk.fill(0);
  blk.set(input.samples.subarray(s0, s0 + n));
  proc.process([[blk]], [[oblk]]);
  out.set(oblk.subarray(0, n), s0);
  if (tracePath) traceRows.push(`${b},${proc.liveF0},${proc.liveAmp}`);
  if (fePath) feRows.push(`${s0 + n - 1},${proc.envFast},${proc.liveAmp},${proc.gateGain}`);
}
if (outPath && outPath !== '-') writeWav(outPath, input.sr, out);
if (tracePath) fs.writeFileSync(tracePath, traceRows.join('\n') + '\n');
if (fePath) fs.writeFileSync(fePath, feRows.join('\n') + '\n');

// sanity summary
let peak = 0; for (let i = 0; i < N; i++) peak = Math.max(peak, Math.abs(out[i]));
if (!isFinite(peak)) { console.error('NON-FINITE OUTPUT'); process.exit(1); }
console.log(`rendered ${character}: ${N} samples @ ${input.sr} Hz, peak ${peak.toFixed(3)}`);
