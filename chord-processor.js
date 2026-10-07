// chord-processor.js - mirror of firmware/engine/chord_engine.hpp; change
// both together; pedal/tests/chord.test.mjs parses the C++ to hold them in
// step.
//
// The guitar itself, driven, through three parallel formant bandpasses
// whose vowel moves with pick dynamics. No pitch tracking anywhere, so it
// plays chords, adds no latency and cannot make an octave error.
//
//   in -> LiveFrontEnd (gate, peak-normalized amp)
//   in -> soft clip -> 3 x TPT bandpass, summed -> leveler -> x amp x 0.5
//   mouth follower (amp x sensitivity, or forced open) sweeps the formants
//   from the closed vowel to the open vowel, in log frequency.
//
// No exports: like fof-processor.js and post-processor.js this file is
// loaded by the browser as a plain AudioWorklet script (addModule()), not
// an ES module, so everything a test needs rides as statics on the
// registered processor class (the shim pattern pitch-tracker.test.mjs and
// post-chain.test.mjs use).

// Peterson and Barney (1952), adult male averages (spec section 2).
const kVowelCount = 5;
const kVowelHz = [
  [300, 870, 2240],   // oo (boot)
  [570, 840, 2410],   // oh (bought)
  [730, 1090, 2440],  // ah (father)
  [530, 1840, 2480],  // eh (bet)
  [270, 2290, 3010],  // ee (beet)
];
const kChordBaseBw = [90.0, 110.0, 150.0];  // first-pass
const kChordAmp = [1.0, 0.63, 0.40];        // 0/-4/-8 dB
const kChordCoefEvery = 16;  // samples per coefficient update
// Leveler target. Calibrated against a factory Wukong render on
// guitar_long.wav (firmware/engine/chord_engine.hpp has the measurements).
const kChordLevelTarget = 0.0897;

// Pade tanh: exact 0 and +/-1 at +/-3, odd, monotone.
function softClip(x) {
  if (x >= 3.0) return 1.0;
  if (x <= -3.0) return -1.0;
  const x2 = x * x;
  return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}

// tan(pi fc / sr) for the TPT prewarp, from sin/cos.
function prewarpG(fc, sr) {
  const w = Math.PI * fc / sr;
  return Math.sin(w) / Math.cos(w);
}

// Vowel knob position (0..4, clamped) to a formant triple in Hz,
// interpolated between neighboring rows in log frequency. Writes into
// `out` (length 3), matching the C++ out-param signature.
function vowelFormants(v, out) {
  v = Math.min(Math.max(v, 0.0), kVowelCount - 1);
  const i = Math.min(Math.trunc(v), kVowelCount - 2);
  const t = v - i;
  for (let k = 0; k < 3; k++)
    out[k] = kVowelHz[i][k] * Math.pow(kVowelHz[i + 1][k] / kVowelHz[i][k], t);
}

// Zavalishin/Simper topology-preserving SVF, bandpass output normalized to
// unity peak gain (k * v1).
class TptBandpass {
  constructor() {
    this.a1 = 1; this.a2 = 0; this.a3 = 0; this.k = 1; this.ic1 = 0; this.ic2 = 0;
  }
  set(fc, bw, sr) {
    const g = prewarpG(fc, sr);
    this.k = bw / fc;
    this.a1 = 1.0 / (1.0 + g * (g + this.k));
    this.a2 = g * this.a1;
    this.a3 = g * this.a2;
  }
  process(v0) {
    const v3 = v0 - this.ic2;
    const v1 = this.a1 * this.ic1 + this.a2 * v3;
    const v2 = this.ic2 + this.a2 * this.ic1 + this.a3 * v3;
    this.ic1 = 2 * v1 - this.ic1;
    this.ic2 = 2 * v2 - this.ic2;
    return this.k * v1;
  }
  reset() { this.ic1 = 0; this.ic2 = 0; }
}

// Transcription of the envelope/gate half of firmware/engine/front_end.hpp
// (itself a transcription of fof-processor.js's liveSample()). Input x is
// POST-inputGain.
class LiveFrontEnd {
  constructor(sr) {
    this.cEnvA = 1 - Math.exp(-1 / (0.006 * sr));
    this.cEnvR = 1 - Math.exp(-1 / (0.080 * sr));
    this.cPkDecay = Math.exp(-1 / (4.0 * sr));
    this.cGateA = 1 - Math.exp(-1 / (0.005 * sr));
    this.cGateR = 1 - Math.exp(-1 / (0.060 * sr));
    this.reset();
  }
  reset() {
    this.envFast = 0; this.envPeak = 0.05; this.gateOpen = false;
    this.gateGain = 0; this.liveAmp = 0;
  }
  process(x, gateThreshold) {
    const a = Math.abs(x);
    this.envFast += (a - this.envFast) * (a > this.envFast ? this.cEnvA : this.cEnvR);
    this.envPeak = Math.max(this.envPeak * this.cPkDecay, this.envFast, 0.05);
    const rawAmp = Math.min(1.0, this.envFast / this.envPeak);
    if (this.gateOpen) {
      if (this.envFast < gateThreshold * 0.5) this.gateOpen = false;
    } else if (this.envFast > gateThreshold) this.gateOpen = true;
    const tgt = this.gateOpen ? 1.0 : 0.0;
    this.gateGain += (tgt - this.gateGain) * (tgt > this.gateGain ? this.cGateA : this.cGateR);
    this.liveAmp = rawAmp * this.gateGain;
  }
}

// ChordEngineParams defaults, camelCase mirror of the C++ struct's
// in-class initializers.
function defaultChordEngineParams() {
  return {
    inputGain: 1.0, gate: 0.02, drive: 10.0, sensitivity: 3.0,
    closedVowel: 0.0, openVowel: 2.0, formantScale: 1.0, bwScale: 1.0,
    attackMs: 10.0, releaseMs: 150.0, mouthOpen: false, gain: 1.0,
  };
}

class ChordEngine {
  constructor(sr) {
    this.sr = sr;
    this.fe = new LiveFrontEnd(sr);
    // the grain engine's leveler constants (fof-processor.js): 8 ms
    // tracker, 10 ms reduction slew, 60 ms increase slew
    this.cFast = 1 - Math.exp(-1 / (0.008 * sr));
    this.cGlvDn = 1 - Math.exp(-1 / (0.010 * sr));
    this.cGlvUp = 1 - Math.exp(-1 / (0.060 * sr));
    this.closed = [0, 0, 0];
    this.open = [0, 0, 0];
    this.cAtt = 0; this.cRel = 0;
    this.mouth_ = 0.0;
    this.coefCount = 0;
    this.bp = [new TptBandpass(), new TptBandpass(), new TptBandpass()];
    this.lvFast = 0.0; this.lvG = 1.0;
    this.setParams(defaultChordEngineParams());
  }

  // Called once per block. A handful of pow/exp per block, nothing per
  // sample.
  setParams(p) {
    this.p = p;
    vowelFormants(p.closedVowel, this.closed);
    vowelFormants(p.openVowel, this.open);
    this.cAtt = 1 - Math.exp(-1 / (Math.max(0.1, p.attackMs) * 0.001 * this.sr));
    this.cRel = 1 - Math.exp(-1 / (Math.max(0.1, p.releaseMs) * 0.001 * this.sr));
  }

  // Engage path: same contract as FofProcessor's grain state reset.
  clearOutputState() {
    this.fe.reset();
    for (const b of this.bp) b.reset();
    this.mouth_ = 0.0;
    this.coefCount = 0;
  }

  processBlock(input, output, n) {
    for (let i = 0; i < n; i++) {
      const x = input[i] * this.p.inputGain;
      this.fe.process(x, this.p.gate);
      const amp = this.fe.liveAmp;

      const target = this.p.mouthOpen ? 1.0 : Math.min(1.0, amp * this.p.sensitivity);
      this.mouth_ += (target - this.mouth_) * (target > this.mouth_ ? this.cAtt : this.cRel);

      if (this.coefCount === 0) this.updateCoefs();
      if (++this.coefCount === kChordCoefEvery) this.coefCount = 0;

      const d = softClip(this.p.drive * x);
      let s = 0;
      for (let k = 0; k < 3; k++) s += kChordAmp[k] * this.bp[k].process(d);

      // Target leveler BEFORE the envelope multiply, as the grain engine
      // does, so vowel and resonance changes do not jump in loudness.
      this.lvFast += (Math.abs(s) - this.lvFast) * this.cFast;
      let g = kChordLevelTarget / (this.lvFast + 1e-3);
      g = Math.min(4.0, Math.max(0.25, g));
      this.lvG += (g - this.lvG) * (g < this.lvG ? this.cGlvDn : this.cGlvUp);

      output[i] = s * this.lvG * amp * 0.5 * this.p.gain;
    }
  }

  mouth() { return this.mouth_; }
  liveAmp() { return this.fe.liveAmp; }

  updateCoefs() {
    for (let k = 0; k < 3; k++) {
      let f = this.closed[k] * Math.pow(this.open[k] / this.closed[k], this.mouth_) *
              this.p.formantScale;
      f = Math.min(f, 0.45 * this.sr);
      const bw = Math.max(1.0, kChordBaseBw[k] * this.p.bwScale);
      this.bp[k].set(f, bw, this.sr);
    }
  }
}

ChordEngine.kVowelHz = kVowelHz;
ChordEngine.kChordBaseBw = kChordBaseBw;
ChordEngine.kChordAmp = kChordAmp;
ChordEngine.kChordCoefEvery = kChordCoefEvery;
ChordEngine.kChordLevelTarget = kChordLevelTarget;

class ChordProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.sr = sampleRate;
    this.engine = new ChordEngine(this.sr);
    this.monoIn = new Float32Array(128);
    this.monoOut = new Float32Array(128);
    this.stateCtr = 0;
    this.port.onmessage = (e) => this.onMsg(e.data);
  }

  onMsg(m) {
    if (m.type === 'params') {
      this.engine.setParams(m.values);
    } else if (m.type === 'reset') {
      this.engine.clearOutputState();
    }
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const n = out[0].length;
    if (this.monoIn.length !== n) {
      this.monoIn = new Float32Array(n);
      this.monoOut = new Float32Array(n);
    }
    // Mix ALL input channels (FIRMWARE.md invariant: same as
    // fof-processor.js), so a guitar on the mixer's other channel is never
    // silently dropped.
    const inChs = inputs[0] && inputs[0].length ? inputs[0] : null;
    const nInCh = inChs ? inChs.length : 0;
    this.monoIn.fill(0);
    if (nInCh > 0) {
      for (let i = 0; i < n; i++) {
        let x = 0;
        for (let c = 0; c < nInCh; c++) x += inChs[c][i];
        this.monoIn[i] = x / nInCh;
      }
    }

    this.engine.processBlock(this.monoIn, this.monoOut, n);
    for (let c = 0; c < out.length; c++) out[c].set(this.monoOut);

    if (++this.stateCtr >= 4) {
      this.stateCtr = 0;
      this.port.postMessage({
        type: 'livestate', amp: this.engine.liveAmp(), mouth: this.engine.mouth(),
      });
    }
    return true;
  }
}

ChordProcessor.ChordEngine = ChordEngine;
ChordProcessor.TptBandpass = TptBandpass;
ChordProcessor.softClip = softClip;
ChordProcessor.prewarpG = prewarpG;
ChordProcessor.vowelFormants = vowelFormants;
ChordProcessor.kVowelHz = kVowelHz;

registerProcessor('chord-processor', ChordProcessor);
