# DBscreamZ - Handoff

> **HISTORICAL DOCUMENT (banner added 2026-08-27).** Everything below was
> written before any firmware existed and is preserved as-is. The
> firmware has since been written through milestone 5 (controls, voice
> memory, post chain); current state lives in FIRMWARE.md and
> firmware/MILESTONE*.md. The mistakes log (section 11) is still worth
> reading before debugging anything.

Status: prototyping / research. No hardware built. No firmware written.
Everything so far is a browser lab used to find the right algorithm before
committing to parts.

Written at the end of a long session in which the human's verdict was:
**"FOF sounds more like a voice. Pitch tracking is still poor, and it now
sounds an octave too high."** Both of those are unresolved. Read
section 6 first if you only read one thing.

---

## Contents

1. The goal
2. Hardware target and constraints
3. What the reference audio actually measures
4. Engine A: FOF voice synth (the one that sounds right)
5. Engine B: vowel/formant filter (tried, rejected by ear)
6. **THE OPEN PROBLEM: octave and pitch tracking**
7. Pitch tracking: what was tried, what to try next
8. The analog / synth-mod option
9. Microcontroller budget
10. Files, how to run
11. Mistakes I made (do not repeat)
12. Recommended next steps

---

## 1. The goal

A guitar pedal that produces **power-charging vocal screams**: play a note,
and a voice screams it back.

MVP as specified by the human:

- Just the vocal/consonant/resonant timbre that matches the character.
  No attack or decay noise for now (attack noise may be added later).
- One button to cycle characters (later a rotary switch or pot).
- 1/4" input jack (instrument), 1/4" output jack (amp).
- Characters chosen: **Wukong, Rice, Prince, Piccolo**.

Reference material the human supplied:

- MonkSynth / Delay Lama: <https://github.com/JonET/monksynth>

The voice reference clips stay on the human's machine and are not linked
here.

### Hard constraint the human stated late in the session

> "Please ONLY focus on things that we CAN do with our limitations
> (microcontroller, live input, etc.)"

Anything non-causal, or that needs the whole recording, is out of scope.
This invalidated a chunk of earlier work (see section 11).

### Aesthetic constraint the human stated

> "there shouldn't be ANY noise - white noise and such is what I'm talking
> about... we should be able to achieve vocal scream sounds via resonators,
> LFOs, and such (no noise)"

This is firm. An earlier version injected white noise to match a measured
harmonics-to-noise ratio and was rejected outright.

---

## 2. Hardware target and constraints

Not yet purchased. Nothing is committed.

- Microcontroller + audio codec (use a **codec**, not separate ADC and DAC,
  so there is one clock domain: WM8731, PCM3060, AK4619, TLV320AIC3204).
- Guitar input is high impedance (~1 MOhm), 100 mV to 1 V. Needs a JFET or
  opamp buffer plus gain to reach line level.
- Anti-alias filter before ADC, reconstruction filter after DAC.
- 9 V centre-negative pedal supply, separate analog/digital rails. This is
  where most DIY digital pedals fail on noise. The PedalPCB Terrarium is
  known to need an RC filter fix on current Daisy revisions for this reason.
- True-bypass relay, footswitch debounce, 1590B/125B enclosure, RGB LED.

**Daisy Seed is not required.** See section 9: this algorithm needs ~88 KB
RAM and no external SDRAM. Daisy's 64 MB SDRAM exists for long reverb/delay
lines this design never uses, and Daisy supply has been unstable
(RAM shortage reports, tariff repricing, move to Seed3).

### Audio note

**No source audio is used at runtime** in any design here, only derived
numbers. Nothing sampled is shipped or redistributed.

---

## 3. What the reference audio actually measures

Two sources were analysed. **Use the second one.**

### Source 1: the scream compilation (mostly unusable)

The scream compilation, 5:33. Measured:

- Barely clipped (0.003% at full scale), crest factor 11.5 dB.
- Voice is centre, music/SFX in the sides about **12 dB down**. Contaminated.
- 53 scream segments extracted, 51.5s of voiced material.
- **The 53 segments do not separate into distinct voices.** k-means
  silhouette was weak at every k (best 0.266 at k=3), and cluster envelope
  peaks were near-identical (~900-950 Hz and ~2000 Hz).
  **You cannot derive four character presets from this file.**

### Source 2: the isolated per-character clips (this is the good data)

Isolated, dry, silence-separated, already labelled per character. This solved
both the contamination and the labelling problem.

| Character | f0 median | HNR | notes |
|---|---:|---:|---|
| Rice | 534.8 Hz | +0.1 dB | highest |
| Wukong | 451.9 Hz | -1.0 dB | |
| Prince | 379.8 Hz | -5.2 dB | gravellier |
| Piccolo | 354.1 Hz | -4.7 dB | lowest, gravellier |

**f0 spread is 7 semitones** Rice to Piccolo. This is the single strongest
character discriminator. HNR splits them into two groups
(Wukong/Rice smooth, Prince/Piccolo rough).

Caveat: these are PS1-era ADPCM clips, bandwidth 9.3-11.7 kHz. Codec noise
biases every HNR **downward**. All four share the same codec so the relative
ordering is trustworthy; **the absolute dB values are not**. Trusting them
literally is what caused the rejected noise-injection version.

### Formants could NOT be measured reliably

Tried twice, failed both times. At f0 ~450 Hz there are only **6 harmonics
below 3 kHz**, so the spectral envelope is sampled at 6 points, while /a/'s
F1-to-F2 gap is ~400 Hz. This is information-theoretic, not an estimator bug.
Both constrained least-squares fits came back degenerate with parameters
pinned at their bounds.

**What is shipped instead:** MonkSynth's own open-vowel formant table
(800/1150/2900 Hz) scaled per character by a measured spectral-centroid
ratio. So:

| Character | F1 | F2 | F3 | tract scale | vibrato |
|---|---:|---:|---:|---:|---|
| Wukong | 858 | 1234 | 3112 | 1.073 | 6.7 Hz / 0.42 st |
| Rice | 1000 | 1438 | 3625 | 1.250 | 7.2 Hz / 0.21 st |
| Prince | 742 | 1066 | 2688 | 0.927 | 7.0 Hz / 0.70 st |
| Piccolo | 698 | 1003 | 2529 | 0.872 | 7.1 Hz / 0.70 st |

f0 and vibrato **rate** are real measurements. Formants are a principled
scaling, not measured. Vibrato **depth** for Prince and Piccolo was clamped
from 0.96 and 1.50 down to 0.70 because the f0 tracker was glitching on those
raspy voices and real vocal vibrato is 0.2-0.7 semitones.

---

## 4. Engine A: FOF voice synth (KEEP THIS)

**Human verdict: "FOF sounds more like a voice."** This is the direction.

Faithful port of MonkSynth's `dsp/voice.c` (Rodet/IRCAM formant-wave-function
synthesis). Source: `dbscreamz_lab/static/fof-processor.js`.

```
register-mapped f0 -> grain trigger phase
grain = sum of 3 damped sinusoids at F1/F2/F3, cosine-windowed, 20 ms
      -> overlap-added into a circular buffer at the pitch period
      -> x guitar amplitude envelope
```

Key properties, all verified:

- The grain is **pitch-independent**; it is only recomputed when formant
  parameters change. This is what makes FOF cheap.
- Vibrato is a sine LFO with jitter applied to the **LFO rate**, never to the
  audio. No noise is introduced.
- **Aspiration is forced to zero.** MonkSynth's aspiration is two inharmonic
  sinusoids (4951/3802 Hz), not white noise, but it is off by default.
- Unison up to 8 detuned voices with staggered vibrato phase.

**Measured spectral flatness 2.3e-5 to 1.2e-4.** For comparison, the dry
guitar measures 2e-4 and the rejected noise-based version measured 0.06-0.09.
The engine is provably clean. Do not add noise sources.

### Volume spikes: root-caused and fixed (2026-08-25, post-doc)

The human reported intermittent volume spikes with all FX bypassed. Ablation
(spike metric: output jumps >6 dB where the guitar envelope did not) found
unison=1 gave ZERO spikes, so the unison stack was the cause. Three
mechanisms, three fixes, in order of importance:

1. **Staggered per-voice vibrato phases** (MonkSynth's choir behaviour).
   With 42 cents of vibrato swing against 11 cents of detune spacing, the
   voices' frequencies kept crossing; each crossing stalls their relative
   phase into a coherent lock (+9 dB) or a hole. Fix: ONE common vibrato LFO
   for the whole stack; detune alone supplies thickness. This took the spike
   count to zero on its own merits.
2. **Equal-amplitude voices can null completely** at beat minima.
   Fix: side voices tapered to 0.55, centre always dominates.
3. **Coherent grain waveforms.** All voices shared one grain table.
   Fix: per-voice grains with deterministic golden-ratio phase scatter
   (voice 0 keeps zero phase, so unison=1 is still the exact MonkSynth grain).

Plus a **ratio leveler** (8 ms/250 ms rectified-average trackers, gain =
slow/fast clamped to +/-6 dB) applied BEFORE the guitar-envelope multiply, so
playing dynamics pass through untouched. UI toggle "Unison leveler".
Verified: 0 anomalous spikes across all parameter ablations, spectral
flatness 2.7e-4 (still clean). Overlap normalisation also moved from read
time (instantaneous divisor, wrong during pitch motion) to per-grain at
write time, which is the correct firmware structure anyway.

### Settings the human approved by ear (2026-08-25)

Wukong preset with: quantize OFF (a large audible improvement per the human),
octaveShift 0 (voice at guitar pitch, no transposition), taperEnd 0,
unison 3, detune 11, glide 35 ms, gain 0.8, all FX bypassed. Saved verbatim
in the session log; the export button reproduces it.

### Things already fixed in this engine (do not re-break)

- **Overlap-add normalisation.** At 450 Hz with a 20 ms grain roughly 9-10
  grains overlap. Dividing only by the unison count left peaks at 3.745.
  Now divides by `unison * overlap` where `overlap = grainLen * f0 / sr`.
- **Gain staging.** WaveShaper clamps its input to +/-1 and hard-clips
  anything hotter, even at drive=0. The shaper now runs **before** the makeup
  gain, with 4x oversampling.
- **Grain end taper.** MonkSynth's release window deliberately stops short of
  zero ("open tail"), so grains end on a step. Added an optional taper.
  Measured benefit only 0.2-0.9 dB, so this was NOT the hiss source. Kept
  because harmless.

### Cheap, noise-free levers not yet explored for "scream" character

The human's remaining concern is that pure FOF can read as monk chant rather
than scream. Untried, all noise-free:

- Higher F1 (a yelled /a/ has a much more open jaw than a sung one)
- Faster and wider vibrato
- Formant shift during the attack
- Shorter grains (measured: 8 ms grain is buzzier, rms 0.047 vs 0.021)

---

## 5. Engine B: vowel/formant filter (tried, rejected by ear)

**Human verdict: "I couldn't find good settings for the Vowel filter."**

Source: `dbscreamz_lab/static/vowel-processor.js`. Takes the guitar as live input:

```
in -> drive (tanh) -> 4 parallel RBJ bandpass biquads -> sum -> out
                            ^ envelope follower sweeps centres
```

It is **not broken**. Verified: output peaks land on their targets
(E gives 527/1834 Hz against a 530/1840 target; O gives 557/838 against
570/840; U gives 838/2232 against 870/2240). `mix=0` reproduces the dry
guitar exactly. Stable across all parameter extremes.

It just does not sound like the goal. It sounds like a **talking guitar**,
because your guitar keeps its own pitch and timbre and the vowel rides on
top. That is inherent to filtering, not a tuning failure.

Worth keeping in the lab as an A/B reference and as the digital stand-in for
the analog option (section 8), but it is not the product.

**One real finding from it:** at drive=0 the output is nearly silent
(rms 0.0285 vs 0.125 dry). A clean guitar has too few harmonics in the
formant passbands. The drive stage is load-bearing, not a flavour control.
This matters for any analog version too.

---

## 6. THE OPEN PROBLEM: octave and pitch tracking

**This is the thing to fix. Two separate problems got tangled together.**

### 6a. It sounds an octave too high, and this is my fault

The human's last words: *"it all sounds an octave up... I thought you called
this out and fixed it?!?! ... it sounds higher than before. I think you went
in the wrong direction."*

What happened, precisely:

1. I ported MonkSynth's `grain_period()` literally:
   `internal = midi_note - 12; idx = int(internal*32); freq = 2^(idx/384) * 8.175799`.
   In MonkSynth, `midi_note` is an **internal** representation already offset
   by +12, so that `-12` converts into their table space. Applied to a real
   frequency it **halves the pitch**. Measured ratio: exactly 0.5000.
2. I fixed it. Correct in isolation.
3. **But that bug was accidentally compensating for a second problem I never
   questioned.** The register mapper transposes the guitar up to reach the
   character's measured f0. For Wukong: median guitar pitch ~140 Hz,
   target 452 Hz, so `round(log2(452/140)) = +2 octaves`. 140 -> 560 Hz.
4. Before the fix, that +2 octaves got halved back to +1. The human liked it.
   After the fix, they get the full +2 and it is too high.

**So the measured "correct" scream register is wrong for this application.**
The human's ear says +1 octave. Do not trust the 354-535 Hz targets as
transposition destinations just because they are measured.

**FIXED (2026-08-25, after this doc was first written).** Transposition is
now a plain `octaveShift` control defaulting to **+1**, fully decoupled from
the character's measured f0. Characters set formants and vibrato only; all
four now render at identical pitch (verified: cross-character f0 ratio
1.000-1.005, engine-internal shift ratios exactly 2.0000, output tracks
contour x2 within 0.39 semitones). Default glide raised 12 -> 35 ms so any
residual tracking glitch becomes a fast slide instead of a hard step.
Awaiting ear validation. A/B renders in `/tmp/dbscreamz_v5/`
(four characters at +1, plus Wukong at +0/+1/+2).

### 6b. Pitch tracking is still poor

Guitar pitch tracking is the hardest part of this project, and it is where
comparable commercial pedals are weakest too (reviewers report glitching on
bends and chords, with latency of 30-100 ms).

Raw YIN on the human's own DI throws **9.8 octave errors per second**.
Current causal tracker gets that to 1.4/s but the human still hears it.

---

## 7. Pitch tracking: what was tried, what to try next

### What is implemented now (causal, firmware-legal)

In `dbscreamz_lab/build_assets.py`, function `causal_pitch()`. Runs offline in the
lab only for convenience; **the algorithm itself is causal and portable**.

1. Decimate 48 kHz to 8 kHz (32-tap FIR). Never run YIN at 48 kHz: it costs
   338 MOPS versus 9.4 decimated.
2. YIN, 40 ms frame, 5 ms hop, search 70-700 Hz.
3. **Attack blanking**, 6 frames. A pick transient has no stable period, so
   YIN is worst exactly at onset.
4. **Voted re-anchor**: median of the first 3 confident frames sets the
   note's pitch. One frame is not enough; a single octave error poisons the
   whole note.
5. **Octave anchor + hysteresis**: during a note, snap to the octave nearest
   the anchor. Mid-note octave changes need 60 frames (300 ms) of persistent
   disagreement, because a real octave leap requires a re-pick. This value
   matters a lot: at 3 frames the output range blew out to 40-487 Hz.
6. **Range fold** to 75-700 Hz.
7. Causal median of 5.

Measured octave jumps per second:

| clip | raw YIN | causal (shippable) | offline Viterbi (NOT implementable) |
|---|---:|---:|---:|
| guitar_long | 9.8 | **1.4** | 1.0 |
| guitar_short | 8.2 | **1.9** | 0.9 |
| guitar_intro | 11.8 | **0.4** | 0.6 |

Cost: **98 ms total latency** (40 ms YIN window + 30 ms blanking + 15 ms
anchor vote + 10 ms median + 3 ms I/O).

### What to try next: Cycfi Q (this is the strongest lead)

<https://github.com/cycfi/q> - MIT licence, C++, explicitly designed to be
"efficient enough to run on small microcontrollers".

**Bitstream Autocorrelation (BACF)** works on the zero-crossing bitstream
using bitwise ops instead of float multiplies: speedup factor N (64 on a
64-bit word) over standard autocorrelation.

The number that matters: Cycfi report **~12 ms latency on low E**, which is
one full cycle of 82.41 Hz, the theoretical floor.
<https://www.cycfi.com/2018/03/fast-and-efficient-pitch-detection-bitstream-autocorrelation/>

Two details that hit exactly the failures above:

- Their peak-trigger front end (envelope follower + Schmitt trigger at ~90%
  of the envelope) is **immune to multiple triggers**. The attack-blanking
  hack above is a crude workaround for the same problem and costs 30 ms.
- They hit the same median-filter-delays-onset tradeoff and solve it by
  running **two BACFs in parallel** rather than median filtering.

**Caveat:** BACF is retired in Q v1.5, replaced by a newer "Hz" detector with
integrated onset detection. Those benchmarks are historical. The 12 ms
single-cycle floor is fundamental and carries over.

If BACF or Hz gets latency from 98 ms to 12-30 ms, the digital design becomes
far more attractive and the argument for going analog largely evaporates.

### Other open-source options

- `ashokfernandez/Yin-Pitch-Tracking` - C YIN, embedded-suitable.
- Teensy Audio Library `AudioAnalyzeNoteFrequency` - YIN-based, ships ready.
  Fastest path to a working prototype baseline.
- `adamski/pitch_detector` - MPM (McLeod), JUCE-dependent; check whether the
  de-JUCE-ing on their roadmap has landed.
- coertvonk's Arduino pitch detector - good optimisation case study,
  autocorrelation at half the memory of FFT.

---

## 8. The analog / synth-mod option

The human asked twice about doing this with analog filters. I initially said
flatly no. **That was too absolute and I corrected it.** The accurate claim:

> Analog cannot put the voice at a **chosen pitch**. It can absolutely deliver
> the **vowel character**.

Existence proof: a talkbox keeps the guitar's pitch and still sounds vocal.

### Prior art worth studying

- **EHX Bassballs** - literally two parallel envelope-swept resonant filters
  plus fuzz, marketed as producing "vocal-like sounds". Schematic widely
  available. Best DIY starting point.
- **Synthrotek Motomouth** DIY kit - 3-band Sedra-Espinoza dual-amplifier
  bandpass (DABP) tuned to vowel formants. DABP matters because Q and centre
  frequency are largely independent, which makes **switching resistor banks
  on a rotary switch practical** - i.e. your character selector.
- **EHX Stereo Talking Machine** - 9 voices, 7 of them vowels, with a built-in
  fuzz stage. But it draws 185 mA, so it is digital inside. Discontinued 2022,
  not clonable.
- **Octavia / Foxx Tone Machine** - full-wave rectifier octave-up, zero
  latency, no tracking at all.
- Key principle: a wah has **one** peak. A vowel needs **two, in parallel,
  not cascaded**. Two is enough to make a vowel unmistakable.

### The design this points to: analog audio, digital control

```
guitar -> fuzz -> [octave-up rectifier, switchable] -> 3 parallel DABP
          formant sections -> summing amp -> out
                          ^
        MCU sets centre frequencies via CD4051 muxes or digipots
        (character select + RGB LED, no audio DSP at all)
```

Zero latency, no octave glitches ever, digital preset recall, no pitch
tracking. What you give up is per-character **pitch**.

### Honest limits

Rectifier octave-up is "strongest above the 12th fret, weak and gated on low
strings", wants neck pickup with tone rolled off, and any gain imbalance
between rectifier legs leaves fundamental feedthrough. The human's DI sits at
**71-280 Hz**, which is exactly its weak spot.

---

## 9. Microcontroller budget

Analytic, not measured on hardware. Order-of-magnitude only.

Audio rate, per sample at 48 kHz, full chain including all effects:

| | ops/sample | MOPS |
|---|---:|---:|
| unison=1 | 146 | 7.01 |
| unison=3 | 152 | 7.30 |
| unison=8 | 167 | 8.02 |

Control rate, pitch at 200 Hz: **9.66 MOPS** (YIN dominates at 9.39).

**Grand total ~17 MOPS.** STM32F411 @100 MHz: 17%. RP2350 @150 MHz: 11%.
STM32H750 @480 MHz: 3.5%.

**RAM 88 KB**, largest items the overlap-add ring (38 KB) and reverb delay
lines (36 KB). Fits internal SRAM on an STM32F411 (128 KB). **No external
SDRAM needed.**

Free win not yet applied: dropping the pitch control rate from 200 Hz to
100 Hz **halves YIN cost from 9.4 to 4.7 MOPS with no latency penalty.**

The vowel filter engine (section 5) costs under 1 MOPS and a few hundred
bytes, if that route is ever taken.

Unvalidated: codec driver overhead, interrupt jitter, real ADC noise floor,
and whether the latency is playable.

---

## 10. Files, how to run

```

  Wash.Rinse.Repeat EMU_CLEAN.wav   the human's guitar DI (3:22, dual mono
                                    44.1k). Playing at 5-18s, 55-92s, 110-123s
  HANDOFF.md                        this file
  dbscreamz_lab/
    serve.py                        stdlib HTTP server, no deps
    build_assets.py                 guitar excerpts, pitch contours, presets
    README.md                       lab usage
    FEASIBILITY.md                  the MCU/real-time audit
    static/
      index.html                    UI
      app.js                        effects graph, UI, spectrum analyser
      fof-processor.js              ENGINE A: FOF voice synth (the good one)
      vowel-processor.js            ENGINE B: vowel/formant filter
      presets.json                  the 4 character presets
      contours.json                 precomputed guitar pitch/amp contours
      audio/                        guitar clips + reference character clips
```

Run:

```bash
cd dbscreamz_lab
python3 serve.py          # http://127.0.0.1:8000
```

Click "Start Audio" (browsers block audio until you interact), then "Play".
Stdlib only. Re-run `python3 build_assets.py` only to change guitar excerpts
or retune the pitch tracker.

**Note on the lab's architecture:** the guitar pitch/amplitude contour is
precomputed offline and shipped as JSON, so the browser does no pitch
detection. That is a lab convenience. The *algorithm* is causal and portable;
the precomputation is not part of the product.

### Testing without a browser

The AudioWorklets can be run headlessly in Node with shimmed globals, which
is how everything above was verified:

```js
globalThis.sampleRate = 48000;
globalThis.AudioWorkletProcessor = class {
  constructor(){ this.port = {onmessage:null, postMessage(){}}; }
};
let CLS; globalThis.registerProcessor = (n,c) => { CLS = c; };
eval(require('fs').readFileSync('static/fof-processor.js','utf8'));
// then: new CLS(), .onMsg({type:'contour',...}), .process([],[[buf]])
```

Do this. It caught a peak-level bug (3.745 full scale) that would have been
guesswork otherwise.

---

## 11. Mistakes I made (do not repeat these)

Listed because several cost real time and the human noticed all of them.

1. **Matched a statistic I had already flagged as unreliable.** I measured
   HNR at -1 to -5 dB, noted in writing that ADPCM codec noise biases it
   downward, then injected white noise to hit that number anyway. The human
   rejected the result outright. **The reference clips' noisiness is largely
   codec artefact. Do not model it.**

2. **Fixed a bug without checking what it was compensating for.** The
   quantizer octave bug (section 6a). The fix was correct; the result was
   worse, because a second unexamined problem had been cancelling it.
   **When a fix makes things worse, suspect a compensating pair.**

3. **Trusted pitch estimators repeatedly, and they were wrong repeatedly.**
   Autocorrelation, cepstral and comb-fit estimators disagreed by up to 14
   semitones on these noisy signals. At one point I dismissed a correct
   autocorrelation reading of 281 Hz as an "estimator octave error" when the
   synth really was playing 280. **Verify against the commanded control
   signal, which is ground truth, not against a re-measurement of the audio.**

4. **Guessed three times at the "hiss" cause and was wrong each time**
   (WaveShaper clipping, CPU overrun, grain end-step). Measured refutations:
   input peaked at 0.57 so no clipping; CPU was 4.7% of one core; the taper
   bought 0.2-0.9 dB. The actual cause was most likely the octave-flip warble.
   **Measure before asserting a cause.**

5. **Over-generalised from one failed experiment.** Model B (fuzz into a
   formant bank) scored badly on a 7.9-semitone *pitch* error, and I concluded
   "analog cannot work". The correct conclusion was narrower: analog cannot
   set pitch. It handles vowels fine. See section 8.

6. **Built an offline algorithm for a real-time product.** The Viterbi octave
   corrector needs the whole clip. It made the lab sound better than the pedal
   ever could, which is worse than useless for tuning. The human had to tell
   me to stop. **Every algorithm must be causal and bounded-memory.**

7. **Killed the human's server** with an over-broad `pkill` aimed at my own
   test instance.

---

## 12. Recommended next steps

**2026-08-25 update: the project has moved to the prototype phase. See
`PROTOTYPE.md` for the execution plan (live-input lab first, then Daisy
Seed hardware). The list below predates it and items 1 and partially 3 are
done; the spike issue in section 4a is fixed and ear-checked, with residual
level movement attributed to the dry track's own dynamics.**

In priority order.

1. **Octave: DONE in code (see 6a), still needs ear validation.** Transpose
   is now an explicit +0..+3 control defaulting to +1, decoupled from the
   character's measured f0. Listen and confirm before moving on.

2. **Swap the pitch tracker for Cycfi Q** (BACF or the newer Hz detector).
   Potentially 98 ms to 12-30 ms. This is the single biggest available win
   and it also addresses the multiple-trigger problem directly.

3. **Push FOF toward "scream" and away from "chant."** The human says FOF
   already sounds like a voice; it needs to sound like a *screaming* voice.
   Untried noise-free levers in section 4. Do not reach for noise.

4. **Then, and only then, think about hardware.** Nothing is bought. The
   budget says almost any modern MCU works and no external SDRAM is needed,
   so this decision can wait until the algorithm is right.

### Things NOT to spend time on

- Re-deriving formants from the reference audio. It has been attempted twice
  and is information-theoretically impossible at f0 ~450 Hz. Use the scaled
  MonkSynth table.
- The scream compilation file. Use the isolated per-character clips.
- Any noise source, of any kind, anywhere in the voice path.
- The vowel/formant filter engine as the primary product. Keep it as an A/B
  reference only.

### The one question only the human can answer

Whether the latency is playable. Everything in this document is analytic or
measured in Python/JS. Nothing has been on hardware, and nobody has played
through it live.
