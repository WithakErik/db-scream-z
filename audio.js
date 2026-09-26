// audio.js - the signal path.
//
//   source ─┬─────────────────────────► post-processor input 0 (dry)
//           ├─► fof-processor (voice)   ─┐
//           └─► chord-processor (chord) ─┴► post-processor input 1
//                                             │
//                                             ▼  destination
//
// Only one of fof/chord is ever connected to a source: which one is fixed
// for the page's lifetime by chordMode (the ?chord URL flag, decided once
// in app.js), the same way the pedal's firmware runs one engine or the
// other depending on how it booted.
//
// The pedal always sees a live signal, so every source here (built-in clip,
// uploaded file, microphone) feeds the engine in live mode. The lab's
// precomputed-contour path is deliberately not used: it is a lab artifact
// and the firmware has no equivalent.

import { kGateLevels, formantScaleFrom } from './voice-params.js';
import { toChordEngineParams } from './chord-map.js';

// The lab's digital x4 stands in for the analog input gain the pedal's
// hardware provides, and the gate thresholds were tuned against it.
const INPUT_GAIN = 4.0;
const SAMPLE_RATE = 48000;   // the pedal's rate

export function toFofParams(v) {
  return {
    f1: v.f1, f2: v.f2, f3: v.f3,
    bw1: v.bw1, bw2: v.bw2, bw3: v.bw3,
    a1: v.a1, a2: v.a2, a3: v.a3,
    // Unison pinned for the same reason aspiration is pinned to 0:
    // fof-processor.js keeps its unison support, the pedal just stops
    // driving it.
    //
    // Stacks above 3 were the ringmod-like artifact heard on hardware
    // 2026-09-01. Briefly dropped to 1 on 2026-09-02 chasing a high-note
    // crackle and put straight back: the host renders ruled the voice path
    // out (peak never exceeds 0.25 from 100 Hz to 2 kHz and does not rise
    // with pitch), and 1 also silently killed the detune knob, since
    // detuneCents multiplies `spread` and a single voice sits at spread 0.
    unison: 3, detuneCents: v.detune_cents,
    // Vibrato. The store and the knobs are in CENTS, this file's vibDepth is
    // in SEMITONES: this division is the only place that boundary is
    // crossed anywhere in this page.
    vibRate: v.vib_rate_hz, vibDepth: v.vib_depth_cents / 100,
    // Grain length is pinned for the same reason unison and aspiration are:
    // the engine keeps its support and the pedal stops driving it. 20 ms is
    // the engine's own default (grainMs default in fof-processor.js), and
    // every character already stored exactly 20, so no voice
    // changed when the knob was retired on 2026-09-02. Knob 6 is detune
    // now, and the knob detune left is vibrato depth.
    grainMs: 20,
    formantScale: formantScaleFrom(v.vocal_size),
    // No aspiration: fof-processor.js still carries the breath branch, so
    // the pedal switches it off here instead. Its two 4-5 kHz sinusoids,
    // retriggered once per pitch period, were the "static" heard on
    // hardware on 2026-09-01.
    aspiration: 0,
    glideMs: v.glide_ms,
    octaveShift: v.octave,
    gate: kGateLevels[v.gate_level],
    followPitch: 1,
    inputGain: INPUT_GAIN,
    gain: 1.0,        // output levels live in the post chain
  };
}

export class AudioEngine {
  // chordMode fixes which engine the page drives for its whole lifetime,
  // mirroring the firmware: one engine runs at a time, decided by how the
  // pedal booted, never switched mid-session.
  constructor(chordMode = false) {
    this.chordMode = chordMode;
    this.ctx = null;
    this.fof = null;
    this.chord = null;
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

  // The engine that is actually wired into the graph, by mode.
  get activeEngine() { return this.chordMode ? this.chord : this.fof; }

  async start() {
    if (this.ctx) {
      if (this.ctx.state === 'suspended') await this.ctx.resume();
      return;
    }
    const ctx = new (window.AudioContext || window.webkitAudioContext)({
      sampleRate: SAMPLE_RATE, latencyHint: 'interactive',
    });
    await ctx.audioWorklet.addModule('fof-processor.js');
    await ctx.audioWorklet.addModule('chord-processor.js');
    await ctx.audioWorklet.addModule('post-processor.js');

    this.fof = new AudioWorkletNode(ctx, 'fof-processor', {
      numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1],
    });
    this.chord = new AudioWorkletNode(ctx, 'chord-processor', {
      numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1],
    });
    this.post = new AudioWorkletNode(ctx, 'post-processor', {
      numberOfInputs: 2, numberOfOutputs: 1, outputChannelCount: [2],
    });
    // Only the active engine feeds post input 1: wiring both permanently
    // would sum the idle engine's output into it, and an idle fof/chord
    // node is not guaranteed to render exact silence with no source
    // connected upstream (residual internal state, e.g. a glide target).
    this.activeEngine.connect(this.post, 0, 1);
    this.post.connect(ctx.destination);

    // Only the active engine's port feeds onLiveState: the idle node still
    // runs and posts its own livestate, which would otherwise overwrite
    // the status line with the other engine's readings.
    this.activeEngine.port.onmessage = (e) => {
      if (e.data.type === 'livestate') this.onLiveState(e.data);
    };
    // Live mode, permanently: everything upstream is a real-time signal.
    // fof-processor.js is the only engine that has a live/transport
    // concept; chord-processor.js has no pitch tracker to reset.
    this.fof.port.postMessage({ type: 'live', on: 1 });
    this.fof.port.postMessage({ type: 'transport', playing: true, reset: true });
    this.ctx = ctx;
  }

  setVoice(v, engaged) {
    if (!this.ctx) return;
    this.fof.port.postMessage({ type: 'params', values: toFofParams(v) });
    this.post.port.postMessage({
      type: 'params', tone: v.tone, vocal: v.vocal_vol,
      mix: v.mix, master: v.master_vol, engaged,
    });
  }

  // c is a ChordParams object (chord-map.js), already run through
  // applyChargeChord by the caller; mouthOpen is UI state (the right stomp)
  // that lives outside the saved setting, so it is threaded through here
  // rather than folded into c.
  setChord(c, mouthOpen, engaged) {
    if (!this.ctx) return;
    this.chord.port.postMessage({
      type: 'params', values: toChordEngineParams(c, mouthOpen, INPUT_GAIN),
    });
    this.post.port.postMessage({
      type: 'params', tone: c.tone, vocal: c.vocal_vol,
      mix: c.mix, master: c.master_vol, engaged,
    });
  }

  // The burp fix: on a bypass -> engage edge, clear the overlap buffer and
  // the post chain's filter state, never the pitch tracker.
  engageEdge() {
    if (!this.ctx) return;
    if (this.chordMode) this.chord.port.postMessage({ type: 'reset' });
    else this.fof.port.postMessage({ type: 'transport', playing: true, reset: true });
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
    node.connect(this.activeEngine);
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
  // frozen as they are now, faster than real time. Same mode branch as the
  // live graph: "download" in chord mode renders chord mode.
  async render(payload, onProgress) {
    if (!this.buffer) throw new Error('nothing loaded to render');
    const len = Math.ceil(this.buffer.duration * SAMPLE_RATE);
    const off = new OfflineAudioContext(2, len, SAMPLE_RATE);
    await off.audioWorklet.addModule(this.chordMode ? 'chord-processor.js' : 'fof-processor.js');
    await off.audioWorklet.addModule('post-processor.js');

    const engine = new AudioWorkletNode(off, this.chordMode ? 'chord-processor' : 'fof-processor', {
      numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1],
    });
    const post = new AudioWorkletNode(off, 'post-processor', {
      numberOfInputs: 2, numberOfOutputs: 1, outputChannelCount: [2],
    });
    engine.connect(post, 0, 1);
    post.connect(off.destination);
    if (this.chordMode) {
      // No forced-open mouth in a download: the envelope follows the
      // rendered guitar's own dynamics, same as a normal right-stomp-up
      // playthrough.
      engine.port.postMessage({
        type: 'params', values: toChordEngineParams(payload, false, INPUT_GAIN),
      });
      post.port.postMessage({
        type: 'params', tone: payload.tone,
        vocal: payload.vocal_vol, mix: payload.mix, master: payload.master_vol,
        engaged: true,
      });
    } else {
      engine.port.postMessage({ type: 'live', on: 1 });
      engine.port.postMessage({ type: 'transport', playing: true, reset: true });
      engine.port.postMessage({ type: 'params', values: toFofParams(payload) });
      post.port.postMessage({
        type: 'params', tone: payload.tone,
        vocal: payload.vocal_vol, mix: payload.mix, master: payload.master_vol,
        engaged: true,
      });
    }

    const src = off.createBufferSource();
    src.buffer = this.buffer;
    src.connect(engine);
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
