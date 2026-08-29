// audio.js - the signal path.
//
//   source ─┬─────────────────────────► post-processor input 0 (dry)
//           └─► fof-processor (voice) ─► post-processor input 1
//                                            │
//                                            ▼  destination
//
// The pedal always sees a live signal, so every source here (built-in clip,
// uploaded file, microphone) feeds the engine in live mode. The lab's
// precomputed-contour path is deliberately not used: it is a lab artifact
// and the firmware has no equivalent.

import { kGateLevels } from './voice-params.js';

// The lab's digital x4 stands in for the analog input gain the pedal's
// hardware provides, and the gate thresholds were tuned against it.
const INPUT_GAIN = 4.0;
const SAMPLE_RATE = 48000;   // the pedal's rate

export function toFofParams(v) {
  return {
    f1: v.f1, f2: v.f2, f3: v.f3,
    bw1: v.bw1, bw2: v.bw2, bw3: v.bw3,
    a1: v.a1, a2: v.a2, a3: v.a3,
    vibRate: v.vib_rate, vibDepth: v.vib_depth, vibJitter: v.vib_jitter,
    glideMs: v.glide_ms,
    octaveShift: v.octave,
    gate: kGateLevels[v.gate_level],
    followPitch: 1,
    inputGain: INPUT_GAIN,
    gain: 1.0,        // output levels live in the post chain
  };
}

export class AudioEngine {
  constructor() {
    this.ctx = null;
    this.fof = null;
    this.post = null;
    this.source = null;       // the currently connected source node
    this.stream = null;       // live input MediaStream, if any
    this.buffer = null;       // decoded clip or upload
    this.startedAt = 0;
    this.offsetAt = 0;
    this.playing = false;
    this.onLiveState = () => {};
  }

  get ready() { return this.ctx !== null; }
  get duration() { return this.buffer ? this.buffer.duration : 0; }

  async start() {
    if (this.ctx) {
      if (this.ctx.state === 'suspended') await this.ctx.resume();
      return;
    }
    const ctx = new (window.AudioContext || window.webkitAudioContext)({
      sampleRate: SAMPLE_RATE, latencyHint: 'interactive',
    });
    await ctx.audioWorklet.addModule('fof-processor.js');
    await ctx.audioWorklet.addModule('post-processor.js');

    this.fof = new AudioWorkletNode(ctx, 'fof-processor', {
      numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1],
    });
    this.post = new AudioWorkletNode(ctx, 'post-processor', {
      numberOfInputs: 2, numberOfOutputs: 1, outputChannelCount: [2],
    });
    this.fof.connect(this.post, 0, 1);
    this.post.connect(ctx.destination);

    this.fof.port.onmessage = (e) => {
      if (e.data.type === 'livestate') this.onLiveState(e.data);
    };
    // Live mode, permanently: everything upstream is a real-time signal.
    this.fof.port.postMessage({ type: 'live', on: 1 });
    this.fof.port.postMessage({ type: 'transport', playing: true, reset: true });
    this.ctx = ctx;
  }

  setVoice(v, engaged) {
    if (!this.ctx) return;
    this.fof.port.postMessage({ type: 'params', values: toFofParams(v) });
    this.post.port.postMessage({
      type: 'params', drive: v.drive, tone: v.tone, vocal: v.vocal_vol,
      mix: v.mix, master: v.master_vol, engaged,
    });
  }

  // The burp fix: on a bypass -> engage edge, clear the overlap buffer and
  // the post chain's filter state, never the pitch tracker.
  engageEdge() {
    if (!this.ctx) return;
    this.fof.port.postMessage({ type: 'transport', playing: true, reset: true });
    this.post.port.postMessage({ type: 'reset' });
  }

  // ---- sources ----

  disconnectSource() {
    if (this.source) {
      try { this.source.disconnect(); } catch { /* already gone */ }
      if (this.source.stop) { try { this.source.stop(); } catch { /* not started */ } }
      this.source = null;
    }
    if (this.stream) {
      for (const t of this.stream.getTracks()) t.stop();
      this.stream = null;
    }
    this.playing = false;
  }

  connect(node) {
    node.connect(this.fof);
    node.connect(this.post, 0, 0);
    this.source = node;
  }

  async loadClip(url) {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`${url}: ${res.status}`);
    this.buffer = await this.ctx.decodeAudioData(await res.arrayBuffer());
    return this.buffer;
  }

  async loadFile(file) {
    this.buffer = await this.ctx.decodeAudioData(await file.arrayBuffer());
    return this.buffer;
  }

  playBuffer(offset = 0, loop = true) {
    if (!this.buffer) return;
    this.disconnectSource();
    const src = this.ctx.createBufferSource();
    src.buffer = this.buffer;
    src.loop = loop;
    this.connect(src);
    src.start(0, offset % this.buffer.duration);
    this.startedAt = this.ctx.currentTime;
    this.offsetAt = offset;
    this.playing = true;
  }

  stopBuffer() {
    this.offsetAt = this.position();
    this.disconnectSource();
  }

  position() {
    if (!this.playing || !this.buffer) return this.offsetAt;
    const t = this.offsetAt + (this.ctx.currentTime - this.startedAt);
    return this.buffer.loop === false ? t : t % this.buffer.duration;
  }

  async useLiveInput(deviceId) {
    this.disconnectSource();
    this.stream = await navigator.mediaDevices.getUserMedia({
      audio: {
        deviceId: deviceId ? { exact: deviceId } : undefined,
        // The guitar must arrive unprocessed: every one of these mangles it.
        echoCancellation: false, noiseSuppression: false,
        autoGainControl: false,
      },
    });
    this.connect(this.ctx.createMediaStreamSource(this.stream));
    this.playing = true;
  }

  async inputDevices() {
    const all = await navigator.mediaDevices.enumerateDevices();
    return all.filter((d) => d.kind === 'audioinput');
  }

  // ---- offline render ----
  // Re-runs the whole file through the same two worklets with the settings
  // frozen as they are now, faster than real time.
  async render(voice, onProgress) {
    if (!this.buffer) throw new Error('nothing loaded to render');
    const len = Math.ceil(this.buffer.duration * SAMPLE_RATE);
    const off = new OfflineAudioContext(2, len, SAMPLE_RATE);
    await off.audioWorklet.addModule('fof-processor.js');
    await off.audioWorklet.addModule('post-processor.js');

    const fof = new AudioWorkletNode(off, 'fof-processor', {
      numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1],
    });
    const post = new AudioWorkletNode(off, 'post-processor', {
      numberOfInputs: 2, numberOfOutputs: 1, outputChannelCount: [2],
    });
    fof.connect(post, 0, 1);
    post.connect(off.destination);
    fof.port.postMessage({ type: 'live', on: 1 });
    fof.port.postMessage({ type: 'transport', playing: true, reset: true });
    fof.port.postMessage({ type: 'params', values: toFofParams(voice) });
    post.port.postMessage({
      type: 'params', drive: voice.drive, tone: voice.tone,
      vocal: voice.vocal_vol, mix: voice.mix, master: voice.master_vol,
      engaged: true,
    });

    const src = off.createBufferSource();
    src.buffer = this.buffer;
    src.connect(fof);
    src.connect(post, 0, 0);
    src.start();
    if (onProgress) onProgress(0);
    const out = await off.startRendering();
    if (onProgress) onProgress(1);
    return out;
  }
}

// Minimal 16-bit PCM WAV writer, so a render can be handed back as a file.
export function encodeWav(buffer) {
  const nCh = buffer.numberOfChannels;
  const nFrames = buffer.length;
  const bytes = 44 + nFrames * nCh * 2;
  const buf = new ArrayBuffer(bytes);
  const dv = new DataView(buf);
  const str = (off, s) => { for (let i = 0; i < s.length; i++) dv.setUint8(off + i, s.charCodeAt(i)); };
  str(0, 'RIFF'); dv.setUint32(4, bytes - 8, true); str(8, 'WAVE');
  str(12, 'fmt '); dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true); dv.setUint16(22, nCh, true);
  dv.setUint32(24, buffer.sampleRate, true);
  dv.setUint32(28, buffer.sampleRate * nCh * 2, true);
  dv.setUint16(32, nCh * 2, true); dv.setUint16(34, 16, true);
  str(36, 'data'); dv.setUint32(40, nFrames * nCh * 2, true);

  const chans = [];
  for (let c = 0; c < nCh; c++) chans.push(buffer.getChannelData(c));
  let off = 44;
  for (let i = 0; i < nFrames; i++) {
    for (let c = 0; c < nCh; c++) {
      let s = chans[c][i];
      s = s < -1 ? -1 : s > 1 ? 1 : s;
      dv.setInt16(off, s < 0 ? s * 0x8000 : s * 0x7fff, true);
      off += 2;
    }
  }
  return new Blob([buf], { type: 'audio/wav' });
}
