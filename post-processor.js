// post-processor.js - port of firmware/hothouse/post_chain.hpp plus the
// engage/bypass crossfade from firmware/hothouse/main.cpp.
//
// This is a worklet rather than a graph of Web Audio nodes because the
// chain is per-sample DSP with state (the tone tilt's one-pole) and the
// point of this page is that it behaves like the pedal. A WaveShaper plus
// a BiquadFilter would be an approximation of a thing we already have
// exactly.
//
// input 0 = dry guitar, input 1 = the voice from fof-processor.
// The dry path is never touched: voice -> TONE -> VOCAL VOL, then a
// dry/wet crossfade and MASTER on the sum.

const kRampSeconds = 0.010;   // engage/bypass crossfade, click-free

class PostProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.sr = sampleRate;
    this.cLp = 1.0 - Math.exp(-2.0 * 3.14159265 * 1500.0 / this.sr);
    this.lp = 0.0;
    this.tone = 0.0;
    this.vocal = 1.0;
    this.mix = 1.0;
    this.master = 1.0;
    this.engaged = false;
    this.ramp = 0.0;          // 0 = bypass output, 1 = processed output
    this.rampStep = 1.0 / (kRampSeconds * this.sr);
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.type === 'params') {
        this.tone = m.tone;
        this.vocal = m.vocal;
        this.mix = m.mix;
        this.master = m.master;
        this.engaged = !!m.engaged;
      } else if (m.type === 'reset') {
        this.lp = 0.0;        // PostChain::reset(), on the engage edge
      }
    };
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const dryIn = inputs[0] && inputs[0].length ? inputs[0] : null;
    const voiceIn = inputs[1] && inputs[1].length ? inputs[1][0] : null;
    const n = out[0].length;
    const target = this.engaged ? 1.0 : 0.0;

    for (let i = 0; i < n; i++) {
      // Mix all dry channels, the same reason fof-processor does: a guitar
      // on the interface's channel 2 arrives on the right channel only.
      let dry = 0;
      if (dryIn) {
        for (let c = 0; c < dryIn.length; c++) dry += dryIn[c][i];
        if (dryIn.length > 1) dry /= dryIn.length;
      }
      let v = voiceIn ? voiceIn[i] : 0;

      // TONE: one-pole tilt around a 1.5 kHz lowpass. tone 0 leaves the
      // sample untouched; -1 is the pure lowpass, +1 adds the residual
      // highs back on top. The lowpass state always runs so sweeping
      // through center does not step.
      this.lp += this.cLp * (v - this.lp);
      if (this.tone < 0.0) v = v + (-this.tone) * (this.lp - v);
      else if (this.tone > 0.0) v = v + this.tone * (v - this.lp);
      v *= this.vocal;

      const wet = this.master * (dry * (1.0 - this.mix) + v * this.mix);

      if (this.ramp < target) {
        this.ramp += this.rampStep;
        if (this.ramp > 1.0) this.ramp = 1.0;
      } else if (this.ramp > target) {
        this.ramp -= this.rampStep;
        if (this.ramp < 0.0) this.ramp = 0.0;
      }
      const y = dry + this.ramp * (wet - dry);
      for (let c = 0; c < out.length; c++) out[c][i] = y;
    }
    return true;
  }
}

registerProcessor('post-processor', PostProcessor);
