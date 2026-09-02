/*
 * FOF (Formant-Wave-Function) vocal synthesis AudioWorklet.
 *
 * Port of MonkSynth's dsp/voice.c grain engine (Rodet/IRCAM FOF):
 *   - a grain is a sum of N damped sinusoids at formant frequencies,
 *     shaped by a cosine window
 *   - grains are overlap-added into a circular buffer at the pitch period
 *   - the grain itself is pitch-independent, so it is only recomputed when
 *     formant parameters change (this is what makes FOF cheap)
 *
 * Pitch comes from a precomputed guitar contour (f0 + amplitude at ctrlHz),
 * register-mapped into the character's scream range at audio rate so the
 * register controls stay live.
 *
 * No noise sources. Aspiration is MonkSynth's two inharmonic sinusoids and
 * defaults to zero amplitude.
 */

/* ================= Cycfi Q BACF pitch detector - JS port ==================
 * Port of the cycfi/q pitch stack (Boost Software License 1.0):
 * bitset, bitstream_acf, zero_crossing_collector, bacf_period_detector,
 * pitch_detector. Line-faithful except 32-bit words instead of 64 (JS
 * bitwise ops are 32-bit), which only changes window-size rounding.
 * Upstream: github.com/cycfi/q (Joel de Guzman). */

const QVS = 32;
const Q_UNDEF_EDGE = -0x80000000;

function qPopcount(v) {
  v = v - ((v >>> 1) & 0x55555555);
  v = (v & 0x33333333) + ((v >>> 2) & 0x33333333);
  v = (v + (v >>> 4)) & 0x0f0f0f0f;
  return (v * 0x01010101) >>> 24;
}
function qRelWithin(a, b, eps) {
  return Math.abs(a - b) <= eps * Math.max(Math.abs(a), Math.abs(b));
}

class QBitset {
  constructor(numBits) { this.a = new Uint32Array(Math.ceil(numBits / QVS)); }
  size() { return this.a.length * QVS; }
  clear() { this.a.fill(0); }
  setRange(i, n, val) {
    const size = this.size();
    if (i > size) return;
    if (i + n > size) n = size - i;
    let p = (i / QVS) | 0;
    let mod = i & (QVS - 1);
    const A = this.a;
    if (mod) {
      mod = QVS - mod;
      let mask = (~(0xffffffff >>> mod)) >>> 0;
      if (n < mod) mask = (mask & (0xffffffff >>> (mod - n))) >>> 0;
      if (val) A[p] |= mask; else A[p] &= ~mask;
      if (n < mod) return;
      n -= mod; ++p;
    }
    if (n >= QVS) {
      const v = val ? 0xffffffff : 0;
      do { A[p++] = v; n -= QVS; } while (n >= QVS);
    }
    if (n) {
      const mask = ((1 << (n & (QVS - 1))) - 1) >>> 0;
      if (val) A[p] |= mask; else A[p] &= ~mask;
    }
  }
}

class QBitstreamACF {
  constructor(bits) {
    this.b = bits.a;
    this.midArray = Math.max((((bits.size() / QVS) | 0) >> 1) - 1, 1);
  }
  count(pos) {
    const index = (pos / QVS) | 0;
    const shift = pos % QVS;
    const a = this.b, m = this.midArray;
    let count = 0;
    if (shift === 0) {
      for (let i = 0; i < m; ++i)
        count += qPopcount((a[i] ^ a[index + i]) >>> 0);
    } else {
      const shift2 = QVS - shift;
      for (let i = 0; i < m; ++i) {
        let v = a[index + i] >>> shift;
        v = (v | (a[index + i + 1] << shift2)) >>> 0;
        count += qPopcount((a[i] ^ v) >>> 0);
      }
    }
    return count;
  }
}

class QZCInfo {
  constructor() {
    this.cf = 0; this.cs = 0; this.peak = 0;
    this.leading = Q_UNDEF_EDGE; this.trailing = Q_UNDEF_EDGE; this.width = 0;
  }
  updatePeak(s, frame) {
    this.peak = Math.max(s, this.peak);
    if (this.width === 0 && s < this.peak * 0.3)
      this.width = frame - this.leading;
  }
  period(next) { return next.leading - this.leading; }
  fractionalPeriod(next) {
    const dx1 = -this.cf / (this.cs - this.cf);
    const dx2 = -next.cf / (next.cs - next.cf);
    return (next.leading - this.leading) + (dx2 - dx1);
  }
  similar(next) {
    return qRelWithin(this.peak, next.peak, 1 - 0.8) &&
           qRelWithin(this.width, next.width, 1 - 0.85);
  }
}

class QZeroCrossingCollector {
  constructor(hysteresisDb, windowBits) {
    this.hys = -Math.pow(10, hysteresisDb / 20);
    const words = Math.max(2, Math.ceil(windowBits / QVS));
    this.windowSize = words * QVS;
    let cap = 1; while (cap < this.windowSize / 2) cap <<= 1;
    this.cap = cap; this.mask = cap - 1;
    this.data = new Array(cap);
    for (let i = 0; i < cap; i++) this.data[i] = new QZCInfo();
    this.pos = 0;
    this.prev = 0; this.state = false; this.numEdges = 0;
    this.frame = 0; this.ready = false; this.peakUpdate = 0; this.peak = 0;
  }
  // C++ zc[i]: index 0 = OLDEST edge (reversed ring access)
  get(i) { return this.data[(this.pos + (this.numEdges - 1 - i)) & this.mask]; }
  newest() { return this.data[this.pos]; }
  isReset() { return this.frame === 0; }
  isReady() { return this.ready; }
  peakPulse() { return Math.max(this.peak, this.peakUpdate); }
  reset() { this.numEdges = 0; this.state = false; this.frame = 0; }
  shift(n) {
    const first = this.newest();
    first.leading -= n;
    if (!this.state) first.trailing -= n;
    let i = 1;
    for (; i !== this.numEdges; ++i) {
      const o = this.data[(this.pos + i) & this.mask];
      o.leading -= n;
      o.trailing -= n;
      if (o.trailing < 0) break;
    }
    this.numEdges = i;
  }
  updateState(s) {
    if (this.ready) {
      this.shift(this.windowSize / 2);
      this.ready = false;
      this.peak = this.peakUpdate;
      this.peakUpdate = 0;
    }
    if (this.numEdges >= this.cap) this.reset();
    if (s > 0) {
      if (!this.state) {
        this.pos = (this.pos - 1) & this.mask;
        const o = this.data[this.pos];
        o.cf = this.prev; o.cs = s; o.peak = s; o.leading = this.frame;
        o.trailing = Q_UNDEF_EDGE; o.width = 0;
        ++this.numEdges;
        this.state = true;
      } else {
        this.newest().updatePeak(s, this.frame);
      }
      if (s > this.peakUpdate) this.peakUpdate = s;
    } else if (this.state && s < this.hys) {
      this.state = false;
      this.newest().trailing = this.frame;
      if (this.peak === 0) this.peak = this.peakUpdate;
    }
    this.prev = s;
  }
  process(s) {
    s += this.hys / 2;
    if (this.numEdges >= this.cap) this.reset();
    if (this.frame === this.windowSize / 2 && this.numEdges === 0) this.reset();
    this.updateState(s);
    if (++this.frame >= this.windowSize && !this.state) {
      this.frame -= this.windowSize / 2;
      if (this.numEdges > 1) this.ready = true;
      else this.reset();
    }
    return this.state;
  }
}

class QBacfPeriodDetector {
  constructor(lowestHz, highestHz, sps, hysDb) {
    this.zc = new QZeroCrossingCollector(hysDb, Math.floor((2 / lowestHz) * sps));
    this.minPeriod = Math.floor((1 / highestHz) * sps);
    this.range = Math.trunc(highestHz / lowestHz);
    this.bits = new QBitset(this.zc.windowSize);
    this.weight = 2 / this.zc.windowSize;
    this.midPoint = this.zc.windowSize / 2;
    this.periodDiffThreshold = this.midPoint * 0.008;
    this.fundPeriod = -1; this.fundPeriodicity = 0;
    this.predictedPeriod = -1; this.edgeMark = 0; this.predictEdge = 0;
  }
  setBitstream() {
    const threshold = this.zc.peakPulse() * 0.6;
    this.bits.clear();
    for (let i = 0; i !== this.zc.numEdges; ++i) {
      const info = this.zc.get(i);
      if (info.peak >= threshold) {
        const pos = Math.max(info.leading, 0);
        this.bits.setRange(pos, info.trailing - pos, 1);
      }
    }
  }
  autocorrelateAt(ac, period, first) {
    let count = ac.count(period);
    const mid = ac.midArray * QVS;
    const start = period;
    if (first && count === 0) {
      if (ac.count((period / 2) | 0) === 0) return [-1, period];
    } else if (period < 32) {
      for (let p = start + 1; p < mid; ++p) {
        const c = ac.count(p);
        if (c > count) break;
        count = c; period = p;
      }
      for (let p = start - 1; p > this.minPeriod; --p) {
        const c = ac.count(p);
        if (c > count) break;
        count = c; period = p;
      }
    }
    return [count, period];
  }
  autocorrelate() {
    const threshold = this.zc.peakPulse() * 0.6;
    const ac = new QBitstreamACF(this.bits);
    const harmonicThreshold = (16 * 2) / this.zc.windowSize;
    const fund = { i1: -1, i2: -1, period: -1, periodicity: 0, harmonic: 1 };
    let firstPeriod = 0;
    const periodOf = (x) => this.zc.get(x.i1).fractionalPeriod(this.zc.get(x.i2));
    const save = (inc) => {
      fund.i1 = inc.i1; fund.i2 = inc.i2; fund.period = inc.period;
      fund.periodicity = inc.periodicity; fund.harmonic = 1;
      firstPeriod = periodOf(fund);
    };
    const trySubHarmonic = (harmonic, inc) => {
      const incomingPeriod = (inc.period / harmonic) | 0;
      if (Math.abs(incomingPeriod - fund.period) < this.periodDiffThreshold) {
        if (inc.periodicity > fund.periodicity && harmonic !== fund.harmonic) {
          const pd = Math.abs(inc.periodicity - fund.periodicity);
          if (pd <= harmonicThreshold) {
            fund.i1 = inc.i1; fund.i2 = inc.i2;
            fund.periodicity = inc.periodicity;
            fund.harmonic = harmonic;
          } else save(inc);
        }
        return true;
      }
      return false;
    };
    const processHarmonics = (inc) => {
      if (inc.period < firstPeriod) return false;
      const multiple = Math.max(1, Math.round(periodOf(inc) / firstPeriod));
      return trySubHarmonic(Math.min(this.range, multiple), inc);
    };
    const collect = (inc) => {
      if (fund.period === -1) save(inc);
      else if (processHarmonics(inc)) return;
      else if (inc.periodicity > fund.periodicity) save(inc);
    };
    outer:
    for (let i = 0; i !== this.zc.numEdges - 1; ++i) {
      const first = this.zc.get(i);
      if (first.peak >= threshold) {
        for (let j = i + 1; j !== this.zc.numEdges; ++j) {
          const next = this.zc.get(j);
          if (next.peak >= threshold) {
            const period = first.period(next);
            if (period > this.midPoint) break;
            if (period >= this.minPeriod) {
              const [count, p2] =
                this.autocorrelateAt(ac, period, fund.period === -1);
              if (count === -1) break outer;
              collect({ i1: i, i2: j, period: p2,
                        periodicity: 1 - count * this.weight, harmonic: 1 });
              if (count === 0) break outer;
            }
          }
        }
      }
    }
    if (fund.period !== -1) {
      this.fundPeriod = periodOf(fund) / fund.harmonic;
      this.fundPeriodicity = fund.periodicity;
    } else {
      this.fundPeriod = -1;
      this.fundPeriodicity = 0;
    }
  }
  process(s) {
    const prev = this.zc.state;
    const zcs = this.zc.process(s);
    if (!zcs && prev !== zcs) { ++this.edgeMark; this.predictedPeriod = -1; }
    if (this.zc.isReset()) { this.fundPeriod = -1; this.fundPeriodicity = 0; }
    if (this.zc.isReady()) {
      this.setBitstream();
      this.autocorrelate();
      return true;
    }
    return false;
  }
  predictPeriod() {
    if (this.predictedPeriod === -1 && this.edgeMark !== this.predictEdge) {
      this.predictEdge = this.edgeMark;
      if (this.zc.numEdges > 1) {
        const threshold = this.zc.peakPulse() * 0.6;
        for (let i = this.zc.numEdges - 1; i > 0; --i) {
          const edge2 = this.zc.get(i);
          if (edge2.peak >= threshold) {
            for (let j = i - 1; j >= 0; --j) {
              const edge1 = this.zc.get(j);
              if (edge1.similar(edge2)) {
                this.predictedPeriod = edge1.fractionalPeriod(edge2);
                return this.predictedPeriod;
              }
            }
          }
        }
      }
    }
    return this.predictedPeriod;
  }
}

class QMedian3 {
  constructor(m = 0) { this.m = m; this.b = m; this.c = m; }
  f(a) {
    this.m = Math.max(Math.min(a, this.b),
                      Math.min(Math.max(a, this.b), this.c));
    this.c = this.b; this.b = a;
    return this.m;
  }
}

class QPitchDetector {
  static maxDeviation = 0.90;
  static minPeriodicity = 0.8;
  constructor(lowestHz, highestHz, sps, hysDb) {
    this.pd = new QBacfPeriodDetector(lowestHz, highestHz, sps, hysDb);
    this.frequency = 0;
    this.sps = sps;
    this.median = new QMedian3();
    this.predictMedian = new QMedian3();
    this.framesAfterShift = 0;
  }
  calcFrequency() {
    return this.pd.fundPeriod !== -1 ? this.sps / this.pd.fundPeriod : 0;
  }
  periodicity() { return this.pd.fundPeriodicity; }
  biasPair(current, incoming) {          // returns [f, shift]
    const error = current / 32;          // approx 1/2 semitone
    const diff = Math.abs(current - incoming);
    if (diff < error) return [incoming, false];
    if (this.framesAfterShift > 2) {
      if (current > incoming) {
        const multiple = Math.round(current / incoming);
        if (multiple > 1) {
          const f = incoming * multiple;
          if (Math.abs(current - f) < error) return [f, false];
        }
      } else {
        const multiple = Math.round(incoming / current);
        if (multiple > 1) {
          const f = incoming / multiple;
          if (Math.abs(current - f) < error) return [f, false];
        }
      }
    }
    if (this.pd.fundPeriodicity > QPitchDetector.minPeriodicity)
      return [incoming, true];
    return [current, false];
  }
  biasIn(incoming) {
    const current = this.frequency;
    ++this.framesAfterShift;
    const [f, shift] = this.biasPair(current, incoming);
    if (shift) {
      if (this.pd.fundPeriodicity < QPitchDetector.maxDeviation) {
        const predicted = this.predictFrequency(false);
        if (predicted > 0) {
          const [f2, shift2] = this.biasPair(current, predicted);
          if (!shift2) {
            this.frequency = this.median.f(f2);
          } else {
            const usePredicted =
              Math.abs(current - f) >= Math.abs(current - f2);
            this.frequency = this.median.f(usePredicted ? f2 : f);
          }
        } else {
          this.frequency = this.median.f(f);
        }
      } else {
        const fm = this.median.f(incoming);
        if (fm === incoming) this.framesAfterShift = 0;
        this.frequency = fm;
      }
    } else {
      this.frequency = this.median.f(f);
    }
  }
  process(s) {
    this.pd.process(s);
    if (this.pd.zc.isReady()) {
      if (this.frequency === 0) {
        if (this.pd.fundPeriodicity >= QPitchDetector.maxDeviation) {
          const f = this.calcFrequency();
          if (f > 0) {
            this.median.f(f);
            this.frequency = f;
            this.framesAfterShift = 0;
          }
        }
      } else {
        if (this.pd.fundPeriodicity < QPitchDetector.minPeriodicity)
          this.framesAfterShift = 0;
        const f = this.calcFrequency();
        if (f > 0) this.biasIn(f);
      }
      return true;
    }
    return false;
  }
  predictFrequency(init = false) {
    const period = this.pd.predictPeriod();
    if (period < this.pd.minPeriod) return 0;
    let f = this.sps / period;
    if (this.frequency !== f) {
      f = this.predictMedian.f(f);
      if (init) this.frequency = this.median.f(f);
    }
    return f;
  }
}
/* =============== end Cycfi Q BACF port =============== */

const MAX_UNISON = 8;

class FofProcessor extends AudioWorkletProcessor {
  constructor() {
    super();

    this.sr = sampleRate;
    this.playing = false;
    this.pos = 0;            // playhead in samples

    // contour
    this.f0Arr = new Float32Array(0);
    this.ampArr = new Float32Array(0);
    this.ctrlHz = 200;
    this.clipSamples = 1;

    // parameters (defaults overwritten by the UI immediately)
    this.p = {
      f1: 858, f2: 1234, f3: 3112,
      bw1: 32.5, bw2: 47.5, bw3: 62.5,
      a1: 1.0, a2: 1.0, a3: 1.0,
      formantScale: 1.0,
      grainMs: 20,
      unison: 3,
      detuneCents: 11,
      aspiration: 0.0,
      quantize: 1,
      ampComp: 1,   // MonkSynth's low-note boost: the human prefers it ON
                    // (with the target leveler active its static tilt is
                    // mostly flattened; what remains is dynamic flavour)
      glideMs: 0,
      targetF0: 452, loF0: 300, hiF0: 620,
      octaveShift: 0,
      followPitch: 1,
      registerMode: 0,   // 0 = fixed octave (keeps melody), 1 = per-note snap
      taperEnd: 1,       // force the grain to end at zero (kills grain-rate hiss)
      leveler: 1,        // flatten unison beat wobble (see process())
      inputGain: 4.0,    // live mode: digital gain before tracker + envelope
      gate: 0.02,        // live noise gate threshold on envFast (post-inputGain)
      fastTrack: 0,      // YIN only: trade tracker latency for stability
      useBacf: 1,        // Cycfi Q BACF port: faster AND more stable than YIN
      breathiness: 0,
      gain: 1.0,
    };

    this.fixedOctave = 0;   // derived from the contour + target f0

    // One grain PER UNISON VOICE, with per-voice sinusoid phases.
    // Identical grains across voices beat coherently as trigger phases drift
    // (measured: +9 dB intermittent spikes at unison=3, zero at unison=1).
    // Decorrelated phases keep the unison sum incoherent and steady.
    this.grains = [];
    for (let u = 0; u < MAX_UNISON; u++) this.grains.push(new Float32Array(1));
    this.grainLen = 1;
    this.grainDirty = true;

    // overlap-add buffer (2x max grain length, circular)
    this.obufLen = Math.ceil(this.sr * 0.2);
    this.obuf = new Float32Array(this.obufLen);
    this.owrite = 0;
    this.oread = 0;

    // per-unison-voice state
    this.vphase = new Float64Array(MAX_UNISON);   // grain trigger phase
    this.curF0 = 200;

    // leveler state: fast/slow rectified-average trackers
    this.lvFast = 0;
    this.lvSlow = 0;

    // ---- LIVE INPUT mode: real-time pitch tracking ----
    // Mirrors causal_pitch() + yin() from build_assets.py, sample for sample.
    // Everything causal, O(1) state: this is the firmware algorithm.
    this.live = 0;
    // 48k -> 8k decimator: 32-tap windowed-sinc lowpass at ~3.4 kHz
    this.dcTaps = new Float32Array(32);
    {
      const fc = 3400 / this.sr;
      let sum = 0;
      for (let i = 0; i < 32; i++) {
        const m = i - 15.5;
        const sinc = Math.sin(2 * Math.PI * fc * m) / (Math.PI * m);
        const w = 0.54 - 0.46 * Math.cos(2 * Math.PI * i / 31);
        this.dcTaps[i] = sinc * w;
        sum += this.dcTaps[i];
      }
      for (let i = 0; i < 32; i++) this.dcTaps[i] /= sum;   // unity DC gain
    }
    this.dcBuf = new Float32Array(32);
    this.dcIdx = 0;
    this.dcPhase = 0;
    // 8 kHz ring for the 40 ms YIN window (320 samples), 5 ms hop (40)
    this.p8 = new Float32Array(512);
    this.p8w = 0; this.p8n = 0; this.sinceHop = 0;
    // actual decimated rate: sr/6. At a 44.1 kHz context this is 7350 Hz,
    // and converting YIN lags with a hard-coded 8000 would read every pitch
    // ~1.4 semitones flat.
    this.sr8 = this.sr / 6;
    // per-channel input level meters (block mean |x|), for diagnostics
    this.inLvl = [0, 0];
    this.win = new Float32Array(320);
    this.yinD = new Float32Array(115);
    this.yinC = new Float32Array(115);
    // live envelope follower + slow peak for normalisation
    this.envFast = 0; this.envPeak = 0.05;
    this.cEnvA = 1 - Math.exp(-1 / (0.006 * this.sr));   // 6 ms attack
    this.cEnvR = 1 - Math.exp(-1 / (0.080 * this.sr));   // 80 ms release
    this.cPkDecay = Math.exp(-1 / (4.0 * this.sr));      // ~4 s peak decay
    // noise gate: hysteresis (open at gate, close at gate/2) + smoothed gain,
    // so mixer hiss between phrases cannot drive the voice
    this.gateOpen = false; this.gateGain = 0;
    this.cGateA = 1 - Math.exp(-1 / (0.005 * this.sr));  // 5 ms open
    this.cGateR = 1 - Math.exp(-1 / (0.060 * this.sr));  // 60 ms close
    // causal tracker state (attack blank / vote / anchor / hysteresis / median)
    this.trackerReset();
    this.liveF0 = 200; this.liveAmp = 0; this.liveConf = 0;
    this.stateCtr = 0;

    this.port.onmessage = (e) => this.onMsg(e.data);
  }

  onMsg(m) {
    if (m.type === 'contour') {
      this.f0Arr = new Float32Array(m.f0);
      this.ampArr = new Float32Array(m.amp);
      this.ctrlHz = m.ctrlHz;
      this.clipSamples = Math.max(1, Math.round(m.duration * this.sr));
      this.pos = 0;
      // median f0 of the loud part of the clip, used to pick a fixed
      // transposition that preserves the melody
      const loud = [];
      for (let i = 0; i < this.f0Arr.length; i++) {
        if (this.ampArr[i] > 0.12 && this.f0Arr[i] > 40) loud.push(this.f0Arr[i]);
      }
      loud.sort((a, b) => a - b);
      this.medianF0 = loud.length ? loud[Math.floor(loud.length / 2)] : 150;
    } else if (m.type === 'params') {
      let dirty = false;
      for (const k in m.values) {
        const v = m.values[k];
        if (this.p[k] !== v) {
          if (['f1','f2','f3','bw1','bw2','bw3','a1','a2','a3',
               'formantScale','grainMs','aspiration','breathiness','taperEnd'].includes(k)) {
            dirty = true;
          }
          this.p[k] = v;
        }
      }
      if (dirty) this.grainDirty = true;
    } else if (m.type === 'transport') {
      this.playing = !!m.playing;
      if (m.reset) { this.pos = 0; this.obuf.fill(0); this.owrite = 0; this.oread = 0; }
    } else if (m.type === 'live') {
      this.live = m.on ? 1 : 0;
      // BACF detector: full audio rate, 70-1300 Hz, -45 dB hysteresis
      // (the constructor values Cycfi's own guitar test suite uses)
      this.qpd = new QPitchDetector(70, 1300, this.sr, -45);
      this.trackerReset();
      this.envFast = 0; this.envPeak = 0.05;
      this.gateOpen = false; this.gateGain = 0;
      this.dcBuf.fill(0); this.p8.fill(0); this.p8n = 0; this.sinceHop = 0;
      this.obuf.fill(0); this.owrite = 0; this.oread = 0;
    } else if (m.type === 'seek') {
      this.pos = Math.max(0, Math.round(m.seconds * this.sr)) % this.clipSamples;
    }
  }

  buildGrain() {
    const p = this.p;
    const n = Math.max(16, Math.round(this.sr * p.grainMs * 0.001));

    // cosine window, matching voice.c build_window_and_decay()
    const aLen = Math.max(1, Math.round(this.sr * 0.0018));
    const rStart = Math.min(n - 1, Math.round(this.sr * 0.013 * (p.grainMs / 20)));
    const rLen = Math.max(1, Math.round(this.sr * 0.007 * (p.grainMs / 20)));

    const F = [p.f1 * p.formantScale, p.f2 * p.formantScale, p.f3 * p.formantScale];
    const BW = [p.bw1, p.bw2, p.bw3];
    const A = [p.a1, p.a2, p.a3];

    const TAU = 2 * Math.PI;
    for (let u = 0; u < MAX_UNISON; u++) {
      if (this.grains[u].length !== n) this.grains[u] = new Float32Array(n);
      const g = this.grains[u];
      for (let i = 0; i < n; i++) {
        const t = i / this.sr;
        let s = 0;
        for (let k = 0; k < 3; k++) {
          // Deterministic per-voice, per-formant phase scatter (golden-ratio
          // sequence). Voice 0 keeps zero phase so unison=1 is the exact
          // MonkSynth grain. No runtime randomness, no noise.
          const ph = u === 0 ? 0 : TAU * (((u * 0.61803398875 + k * 0.3819660113) * (k + 1)) % 1);
          // damped sinusoid: exp(-pi*BW*i/sr) * sin(2*pi*F*t + phase)
          s += A[k] * Math.sin(TAU * F[k] * t + ph) * Math.exp(-Math.PI * BW[k] * i / this.sr);
        }
        // MonkSynth aspiration: two INHARMONIC sinusoids, not white noise.
        if (p.aspiration > 0) {
          const p1 = this.sr * 0.000202, p2 = this.sr * 0.000263;
          const d1 = Math.exp(-Math.PI * 50 * (i * 3.0) / this.sr);
          const d2 = Math.exp(-Math.PI * 50 * (i * 3.5) / this.sr);
          s += p.aspiration * (Math.sin(TAU * i / p1) * d1 + Math.sin(TAU * i / p2) * d2);
        }
        // window
        let w = 1.0;
        if (i < aLen) w = 0.5 * (1 - Math.cos(Math.PI * i / aLen));
        if (i >= rStart) w = Math.max(0, 0.5 * (1 - Math.cos(Math.PI * (rLen + i) / rLen)));
        // MonkSynth's release deliberately stops short of zero ("open tail"),
        // so every grain ends on a step. Optional final taper to zero.
        if (p.taperEnd) {
          const tail = Math.max(1, Math.round(n * 0.12));
          if (i >= n - tail) w *= 0.5 * (1 + Math.cos(Math.PI * (i - (n - tail)) / tail));
        }
        g[i] = s * w;
      }
    }
    this.grainLen = n;
    this.grainDirty = false;
  }

  /* ---------------- live pitch tracking ----------------
   * Direct transcription of build_assets.py yin() + causal_pitch().
   * Runs at 200 Hz control rate on the decimated 8 kHz stream. */

  trackerReset() {
    this.tkAnchor = null;      // log2 anchor of the current note
    this.tkLast = 200.0;       // last accepted pitch (held through gaps)
    this.tkPendK = 0; this.tkPendN = 0;   // octave-change hysteresis
    this.tkBlank = 0;          // attack-blank countdown, frames
    this.tkVotes = [];         // re-anchor votes
    this.tkRing = [];          // causal median of 5
    this.tkPrevAmp = 0;
  }

  foldRange(f) {               // full guitar range 75-1250 Hz, fold by octaves
    // (was capped at 700 Hz: F5 at the 13th fret of high e is 698.5 Hz, so
    // everything from there up got folded/detected an octave low)
    if (f <= 0) return 75;
    while (f < 75) f *= 2;
    while (f > 1250) f *= 0.5;
    return f;
  }

  yin8k(win, n) {              // fmin 70, fmax ~1200, thr 0.15, at sr/6
    // tmin 6 = ~1333 Hz ceiling; parabolic refinement recovers precision at
    // small lags. tmin 11 capped detection at ~727 Hz, one semitone above F5.
    const tmax = 114, tmin = 6, thr = 0.15;
    let mean = 0;
    for (let i = 0; i < n; i++) mean += win[i];
    mean /= n;
    let sd = 0;
    for (let i = 0; i < n; i++) { const v = win[i] - mean; sd += v * v; }
    if (Math.sqrt(sd / n) < 1e-5) return [0, 0];
    const d = this.yinD, cum = this.yinC;
    const L = n - tmax;
    d[0] = 0;
    for (let t = 1; t < tmax; t++) {
      let s = 0;
      for (let i = 0; i < L; i++) {
        const df = win[i] - win[i + t];
        s += df * df;
      }
      d[t] = s;
    }
    cum[0] = 1;
    let run = 0;
    for (let t = 1; t < tmax; t++) {
      run += d[t];
      cum[t] = d[t] * t / (run + 1e-12);
    }
    let cand = -1;
    for (let t = tmin; t < tmax; t++) {
      if (cum[t] < thr) {
        let tt = t;
        while (tt + 1 < tmax && cum[tt + 1] < cum[tt]) tt++;
        cand = tt; break;
      }
    }
    if (cand < 0) {
      let best = tmin;
      for (let t = tmin; t < tmax; t++) if (cum[t] < cum[best]) best = t;
      cand = best;
    }
    const conf = Math.min(1, Math.max(0, 1 - cum[cand]));
    let lag = cand;
    if (cand >= 1 && cand < tmax - 1) {   // parabolic refine
      const a = cum[cand - 1], b = cum[cand], c = cum[cand + 1];
      lag = cand + (a - c) / (2 * (a - 2 * b + c) + 1e-12);
    }
    return [this.sr8 / lag, conf];
  }

  trackerFrame() {
    // fastTrack shrinks the YIN window 40 -> 30 ms (saves 10 ms of the
    // analysis latency; slightly noisier on the lowest notes)
    const wn = this.p.fastTrack ? 240 : 320;
    let j = (this.p8w - wn + 512) % 512;
    const w = this.win;
    for (let k = 0; k < wn; k++) { w[k] = this.p8[j]; j = (j + 1) % 512; }

    // onset = fast rise of the normalised envelope (attack blanking:
    // a pick transient has no stable period, YIN is worst exactly there)
    // fastTrack: blank 6->3 frames, votes 3->2, median 5->3. Saves ~30 ms of
    // note-onset latency at the cost of octave stability at note starts.
    const fast = this.p.fastTrack;
    const dAmp = this.liveAmp - this.tkPrevAmp;
    this.tkPrevAmp = this.liveAmp;
    // onset threshold scales with the frame rate (per-frame envelope rise)
    if (dAmp > (fast ? 0.006 : 0.012)) {
      this.tkBlank = fast ? 3 : 6;
      this.tkAnchor = null;
      this.tkVotes.length = 0;
      this.tkPendK = 0; this.tkPendN = 0;
    }

    let outF = this.tkLast;
    if (this.tkBlank > 0) {
      this.tkBlank--;
    } else if (this.liveAmp > 0.08) {
      const [f0, conf] = this.yin8k(w, wn);
      this.liveConf = conf;
      const voiced = conf > 0.5 && f0 > 60 && f0 < 1300;
      if (voiced) {
        if (this.tkAnchor === null) {
          // voted re-anchor: median of first 3 confident frames, so one
          // octave-errored frame cannot poison the whole note
          if (conf >= 0.6) this.tkVotes.push(this.foldRange(f0));
          const nv = fast ? 2 : 3;
          if (this.tkVotes.length >= nv) {
            const v = [...this.tkVotes].sort((a, b) => a - b);
            // 3 votes: median. 2 votes: take the LOWER (guitar octave
            // errors are overwhelmingly upward).
            const cand = nv === 3 ? v[1] : v[0];
            this.tkAnchor = Math.log2(cand);
            this.tkVotes.length = 0;
            this.tkLast = cand;
            outF = cand;
          }
        } else {
          // octave anchor: snap to the octave nearest the anchor; a
          // mid-note octave change needs 60 frames (300 ms) of persistent
          // disagreement, because a real leap requires a re-pick
          const lf = Math.log2(f0);
          let bestK = 0, bestD = 1e18;
          for (let k = -1; k <= 1; k++) {
            const d = Math.abs(lf + k - this.tkAnchor);
            if (d < bestD) { bestD = d; bestK = k; }
          }
          if (bestK !== 0) {
            if (bestK === this.tkPendK) this.tkPendN++;
            else { this.tkPendK = bestK; this.tkPendN = 1; }
            if (this.tkPendN >= (fast ? 120 : 60)) {   // 300 ms at either rate
              this.tkAnchor += bestK;
              this.tkPendK = 0; this.tkPendN = 0;
            }
          } else { this.tkPendK = 0; this.tkPendN = 0; }
          const cand = this.foldRange(f0 * Math.pow(2, bestK));
          this.tkAnchor = 0.94 * this.tkAnchor + 0.06 * Math.log2(cand);
          this.tkLast = cand;
          outF = cand;
        }
      }
    }
    // causal median (5 frames normally, 3 in fastTrack)
    const ml = fast ? 3 : 5;
    this.tkRing.push(outF);
    while (this.tkRing.length > ml) this.tkRing.shift();
    const r = [...this.tkRing].sort((a, b) => a - b);
    this.liveF0 = r[Math.floor(r.length / 2)];
  }

  // per-sample live front end: envelope, decimation, frame hops
  liveSample(x) {
    const a = Math.abs(x);
    this.envFast += (a - this.envFast) * (a > this.envFast ? this.cEnvA : this.cEnvR);
    // 0.05 FLOOR on the peak reference. Without it this normaliser is an
    // accidental AGC: over ~20 s of silence envPeak decayed to the noise
    // floor and liveAmp returned to 1.0, producing a constant full-volume
    // voice from mixer hiss (measured, and heard by the human).
    this.envPeak = Math.max(this.envPeak * this.cPkDecay, this.envFast, 0.05);
    const rawAmp = Math.min(1, this.envFast / this.envPeak);
    // gate with hysteresis + smoothed gain (no clicks, no chatter)
    if (this.gateOpen) {
      if (this.envFast < this.p.gate * 0.5) this.gateOpen = false;
    } else if (this.envFast > this.p.gate) this.gateOpen = true;
    const tgt = this.gateOpen ? 1 : 0;
    this.gateGain += (tgt - this.gateGain) * (tgt > this.gateGain ? this.cGateA : this.cGateR);
    this.liveAmp = rawAmp * this.gateGain;

    // BACF path: full-rate, no decimation, no blanking/votes/median needed.
    // The detector's own median-3 + bias logic is the whole stabiliser.
    if (this.p.useBacf && this.qpd) {
      this.qpd.process(x);
      const f = this.qpd.frequency;
      if (f > 0) this.liveF0 = f;      // hold last on unvoiced
      this.liveConf = this.qpd.periodicity();
      return;
    }

    this.dcBuf[this.dcIdx] = x;
    this.dcIdx = (this.dcIdx + 1) & 31;
    if (++this.dcPhase >= 6) {
      this.dcPhase = 0;
      let s = 0, j = (this.dcIdx - 1) & 31;
      for (let k = 0; k < 32; k++) { s += this.dcTaps[k] * this.dcBuf[j]; j = (j + 31) & 31; }
      this.p8[this.p8w] = s;
      this.p8w = (this.p8w + 1) % 512;
      if (this.p8n < 512) this.p8n++;
      // fastTrack doubles the control rate (2.5 ms hop): blanking, voting and
      // the median are counted in FRAMES, so their latency halves outright
      if (++this.sinceHop >= (this.p.fastTrack ? 20 : 40) &&
          this.p8n >= (this.p.fastTrack ? 240 : 320)) {
        this.sinceHop = 0;
        this.trackerFrame();
      }
    }
  }

  quantizeHz(hz) {
    if (!this.p.quantize) return hz;
    // MonkSynth quantises pitch to 32 steps per semitone, which is what gives
    // the voice its stepped character. Its grain_period() does
    //   internal = midi_note - 12;  idx = int(internal*32)
    // but `midi_note` there is an INTERNAL representation already offset by
    // +12; the -12 converts into table space. Porting that literally against a
    // real frequency halved every pitch (measured ratio exactly 0.5000).
    // Keep the 32-steps-per-semitone grid, drop the octave offset.
    const note = 12 * Math.log2(hz / 440) + 69;
    const idx = Math.round(note * 32);
    return 440 * Math.pow(2, (idx / 32 - 69) / 12);
  }

  registerMap(f0) {
    const p = this.p;
    if (!p.followPitch || f0 < 40) return p.targetF0;

    if (p.registerMode === 0) {
      // FIXED TRANSPOSE. Plain octave shift, nothing auto-computed.
      // Deriving the transposition from the character's measured f0 gave +2
      // octaves on this player's material and was rejected by ear; the
      // character preset now controls formants only, and octaveShift
      // (default +1) is the single, explicit pitch-register control.
      return f0 * Math.pow(2, p.octaveShift);
    }

    // PER-NOTE SNAP. Pulls every note toward the target f0 individually.
    // Keeps everything inside the scream register but flattens melodic
    // contour, because neighbouring notes can pick different octaves.
    const lo = p.loF0 * 0.75, hi = p.hiF0 * 1.25, tgt = p.targetF0;
    let best = tgt, bf = 1e18;
    for (let o = -2; o <= 5; o++) {
      const c = f0 * Math.pow(2, o);
      const pen = (c >= lo && c <= hi) ? 0 : Math.min(Math.abs(c - lo), Math.abs(c - hi)) * 2;
      const cost = Math.abs(Math.log2(c / tgt)) * 100 + pen;
      if (cost < bf) { bf = cost; best = c; }
    }
    return best * Math.pow(2, p.octaveShift);
  }

  process(inputs, outputs) {
    const out = outputs[0][0];
    const n = out.length;

    // leveler coefficients: 8 ms level tracker, 30 ms gain slew
    if (this.cFast === undefined) {
      this.cFast = 1 - Math.exp(-1 / (0.008 * this.sr));
      this.cGlvDn = 1 - Math.exp(-1 / (0.010 * this.sr));
      this.cGlvUp = 1 - Math.exp(-1 / (0.060 * this.sr));
      this.lvG = 1;
    }

    if (this.grainDirty) this.buildGrain();

    if (!this.playing || (!this.live && this.f0Arr.length === 0)) {
      out.fill(0);
      return true;
    }
    // Mix ALL input channels. The channelCount:1 getUserMedia constraint is
    // advisory; browsers often deliver the device's native stereo, and a
    // guitar on the mixer's channel 2 arrives on the RIGHT channel, which a
    // channel-0-only read silently drops (dry path stereo-passes, so the dry
    // sound working while the voice is dead is the fingerprint of this bug).
    const inChs = this.live && inputs[0] && inputs[0].length ? inputs[0] : null;
    const nInCh = inChs ? inChs.length : 0;

    const p = this.p;
    const gl = this.grainLen;
    const nUni = Math.max(1, Math.min(MAX_UNISON, Math.round(p.unison)));
    let sumG = 0;
    for (let u = 0; u < nUni; u++) {
      const sp = nUni === 1 ? 0 : (u / (nUni - 1)) * 2 - 1;
      sumG += 1 - 0.45 * Math.abs(sp);
    }
    const glideCoef = Math.exp(-1 / (Math.max(1, p.glideMs) * 0.001 * this.sr));

    for (let i = 0; i < n; i++) {
      let rawF0, amp;
      if (this.live) {
        // ---- LIVE: envelope + decimator + tracker, per sample ----
        let x = 0;
        for (let c = 0; c < nInCh; c++) {
          const v = inChs[c][i];
          x += v;
          if (c < 2) this.inLvl[c] += Math.abs(v);
        }
        if (nInCh > 1) x /= nInCh;
        this.liveSample(x * this.p.inputGain);
        rawF0 = this.liveF0;
        // 0.5 headroom scale: the live envelope reaches 1.0 at every pick
        // attack (the offline savgol contour never did), and unscaled output
        // peaked at 1.12, hard-clipping the WaveShaper downstream.
        amp = this.liveAmp * 0.5;
      } else {
        // ---- read contour at the playhead ----
        const tSec = this.pos / this.sr;
        const ci = tSec * this.ctrlHz;
        const i0 = Math.min(this.f0Arr.length - 1, Math.max(0, Math.floor(ci)));
        const i1 = Math.min(this.f0Arr.length - 1, i0 + 1);
        const fr = ci - i0;
        rawF0 = this.f0Arr[i0] * (1 - fr) + this.f0Arr[i1] * fr;
        amp = this.ampArr[i0] * (1 - fr) + this.ampArr[i1] * fr;
      }

      const target = this.registerMap(rawF0);
      // portamento / glide
      this.curF0 = target + (this.curF0 - target) * glideCoef;

      // ---- trigger grains per unison voice ----
      for (let u = 0; u < nUni; u++) {
        const spread = nUni === 1 ? 0 : (u / (nUni - 1)) * 2 - 1;
        // Taper side voices: 3 equal-amplitude combs can null completely at
        // beat minima (the surviving +9 dB spikes). With sides at 0.55 the
        // centre voice always dominates and deep nulls become impossible.
        const vGain = 1 - 0.45 * Math.abs(spread);

        const semis = (spread * p.detuneCents) / 100;
        let f = this.curF0 * Math.pow(2, semis / 12);
        // floor 16 Hz, not 50: transpose -3 octaves from low E is ~10-20 Hz,
        // where FOF degrades gracefully into separated grain pulses
        f = Math.min(Math.max(f, 16), 2000);
        f = this.quantizeHz(f);

        this.vphase[u] += f / this.sr;
        if (this.vphase[u] >= 1) {
          this.vphase[u] -= Math.floor(this.vphase[u]);
          // Overlap normalisation PER GRAIN, at trigger time. Normalising at
          // read time with the instantaneous pitch caused intermittent volume
          // spikes: on a fast pitch drop the divisor shrank immediately while
          // the buffer still held the denser grain sum from the old pitch
          // (280->88 Hz = ~10 dB transient). Scaling each grain by its own
          // trigger-time overlap factor makes level transitions exactly as
          // smooth as the grain decay, and removes the per-sample divide.
          // 1/sqrt(overlap): grain energies ADD as power, so per-grain
          // amplitude 1/overlap made RMS go as 1/sqrt(f0), i.e. low notes
          // ~3 dB louder per octave down (heard by the human on live guitar).
          // Constant-power normalisation is 1/sqrt(overlap); the 0.55 keeps
          // overall level near the previously approved mid-note loudness.
          const overlapAtTrig = Math.max(1, (gl * f) / this.sr);
          let gain = (0.55 * vGain) / Math.sqrt(overlapAtTrig);
          if (p.ampComp) {
            // amplitude compensation from monk_voice_amplitude()
            const note = 12 * Math.log2(f / 440) + 69;
            gain *= Math.min(3, Math.max(0.1, (note - 12) * (-1 / 72) + 2));
          }
          // overlap-add this voice's own grain
          const grain = this.grains[u];
          let w = this.owrite;
          for (let k = 0; k < gl; k++) {
            this.obuf[w] += grain[k] * gain;
            w++; if (w >= this.obufLen) w = 0;
          }
        }
      }

      // ---- read one sample out of the overlap buffer ----
      let s = this.obuf[this.oread];
      this.obuf[this.oread] = 0;
      this.oread++; if (this.oread >= this.obufLen) this.oread = 0;
      this.owrite++; if (this.owrite >= this.obufLen) this.owrite = 0;

      // Overlap normalisation now happens per grain at write time (see the
      // trigger branch above); divide by the summed voice gains so the
      // side-voice taper does not change the overall level.
      let raw = s / sumG;

      // TARGET LEVELER. The synth-internal signal is constant-level BY
      // DESIGN: every playing dynamic comes from the guitar-envelope
      // multiply below. So normalise raw toward a fixed target. This kills
      // both failure modes at once, measured on real material:
      //   - unison beat wobble (+/-9 dB swells at 5-15 Hz)
      //   - formant-comb alignment (+/-7 dB between notes whose harmonics
      //     land on vs between the formant peaks)
      // 8 ms level tracker, 30 ms gain slew, clamp +/-12 dB.
      // Cost: two one-poles and one divide per sample. Firmware-trivial.
      if (p.leveler) {
        const a = Math.abs(raw);
        this.lvFast += (a - this.lvFast) * this.cFast;
        let g = 0.09 / (this.lvFast + 1e-3);
        if (g < 0.25) g = 0.25; else if (g > 4.0) g = 4.0;
        // asymmetric slew: gain REDUCTION is fast (10 ms) so level bursts
        // are caught before they read as spikes; gain INCREASE is slow
        // (60 ms) so filled-in dips swell gently instead of stepping up
        this.lvG += (g - this.lvG) * (g < this.lvG ? this.cGlvDn : this.cGlvUp);
        raw *= this.lvG;
      }

      out[i] = raw * amp * p.gain;

      if (!this.live) {
        this.pos++;
        if (this.pos >= this.clipSamples) this.pos = 0;
      }
    }

    // report state for the UI (cheap, throttled)
    if (++this.stateCtr >= 4) {
      this.stateCtr = 0;
      if (this.live) {
        // per-channel means over the 4-block reporting window
        const denom = 4 * n;
        this.port.postMessage({
          type: 'livestate', f0: this.liveF0, conf: this.liveConf,
          amp: this.liveAmp, rawIn: this.envFast, gateOpen: this.gateOpen,
          nCh: nInCh, ch0: this.inLvl[0] / denom, ch1: this.inLvl[1] / denom,
        });
        this.inLvl[0] = 0; this.inLvl[1] = 0;
      } else {
        this.port.postMessage({ type: 'pos', seconds: this.pos / this.sr });
      }
    }
    return true;
  }
}

registerProcessor('fof-processor', FofProcessor);
