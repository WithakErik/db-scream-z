# DBscreamZ - Firmware Handoff

The brief for the session that writes the pedal firmware. Read this first,
then HANDOFF.md (algorithm history and mistakes log) and PROTOTYPE.md
(hardware decisions). The browser lab is the working reference
implementation; the firmware job is a careful TRANSCRIPTION, not a design
task. All design decisions are made and ear-validated.

## Contents

1. State of the project
2. Target hardware and toolchain
3. What to port, from where
4. Engine invariants (break these and known bugs return)
5. Ear-approved defaults (v12)
6. Control mapping (proposal + open decisions)
7. Milestones and test plan
8. Budgets and expectations
9. Gotchas
10. Suggested kickoff

---

## 1. State of the project

Goal: a guitar pedal that produces power-charging vocal screams. The synthesis algorithm (MonkSynth-style FOF
with a Cycfi Q BACF pitch tracker) is DONE and validated by the human
playing live guitar through the deployed browser lab
(<https://withakerik.github.io/db-scream-z/>, build v12).

Hardware: ALL ORDERED 2026-08-25 (see `orders/ORDERS.md`). Two pedals are
being built on the Cleveland Music Co. Hothouse platform (open source,
CC BY-SA), fabbed at JLCPCB with SMD pre-assembled. Two Daisy **Seed3**
modules are in hand. While parts ship, the firmware gets written; the day
boards arrive should end with flash-and-play.

## 2. Target hardware and toolchain

**Hothouse** (3-PCB design in a 125B): main board (Seed3 socket, SMD,
6 pots, 3 toggles) + switching/LED daughterboard (2 momentary footswitches,
2x 3mm LEDs) + power/audio IO daughterboard (2x Neutrik NRJ6HM-1 stereo
TRS, DC jack). Stereo-capable I/O; our effect is mono, so process left,
copy to both outputs (or duplicate).

- Board support: `clevelandmusicco/HothouseExamples` - has `hothouse.h`
  board file, Makefile-based examples, a wiki on creating your own effect,
  and pre-compiled-binary flashing via the Daisy Web Programmer.
- Library: **libDaisy, current version** (Seed3 support requires it; pin
  the submodule commit once it builds). We do NOT need DaisySP; the DSP is
  all ours plus cycfi/q headers.
- Pitch detector: **cycfi/q C++ headers used directly** (Boost Software
  License 1.0, header-only). Clone <https://github.com/cycfi/q> with the
  `infra` submodule. Needed: `q/pitch/*.hpp`, `q/utility/bitset.hpp`,
  `bitstream_acf.hpp`, `zero_crossing_collector.hpp`, `ring_buffer.hpp`,
  `q/fx/median.hpp`, `q/support/*`. Requires C++20; verified to compile
  with g++ 11 (a native test harness was built this session:
  concept proven, rebuild it as needed).
- Flashing: USB-C on Seed3, DFU via `make program-dfu` or the Web
  Programmer. Note HothouseExamples' docs re the SRAM bootloader if the
  binary outgrows flash (ours will not; it is small).

## 3. What to port, from where

Single source of truth: **`dbscreamz_lab/static/fof-processor.js`** (deployed as
v12). It contains, top to bottom:

1. A JS port of the cycfi/q pitch stack - in firmware use the C++ headers
   instead; the JS is the behavioural reference. Config:
   `pitch_detector(70_Hz, 1300_Hz, sps, -45_dB)`, fed the FULL-RATE input
   sample (no decimation), `get_frequency()` per sample, hold last value
   when it returns 0.
2. The FOF engine: per-voice grain tables (`buildGrain`), trigger +
   overlap-add loop, common vibrato LFO, register/transpose, quantizer,
   glide, leveler, envelope follower, noise gate. Port each verbatim.
3. Presets: `dbscreamz_lab/static/presets.json` -> generate a `presets.h`
   (4 characters: formants_hz[3]; f0 stats are legacy, only used by the
   deprecated snap mode). As of store v3 the header also carried a
   per-character voice stack (unison / detune_cents / grain_ms) that
   does NOT come from presets.json, because that file is a frozen copy of
   the lab's; it lives in `tools/gen_presets.py` STACK. Unison was retired
   2026-09-01 and grain_ms 2026-09-02, both under the store v6 bump; both
   are now pinned in main.cpp's `to_fof_params()` instead, so only
   detune_cents remains in STACK now. Store v7 (2026-09-03) changed only
   the factory charge config (Birit Spomb / rise / brighter / full); the
   layout is unchanged and the bump exists because main.cpp only restores
   defaults on a version mismatch, so without it a pedal keeps its v6
   charge config forever.
   The character vibrato left presets.hpp at commit 37ed8bf (Aug 31) and moved to render.cpp,
   then was removed on 2026-09-01 when the vibrato LFO itself was retired; `firmware/host/render.cpp` no longer
   reproduces the v12 milestone reference renders either, an accepted
   cost of the removal (see the vocal size design spec section 7). The
   LFO itself returned to the engine 2026-09-02 as menu 3 knobs 4 and 5
   (vibrato rate and depth), factory OFF for every character and beast;
   render.cpp's renders stay unreconciled with v12, since no stored voice
   carries a nonzero vibrato to reproduce.
4. The old YIN/causal tracker in the same file is DEAD CODE for firmware:
   do not port it. BACF replaced it (measured better on every metric).

Also in the lab file but NOT wanted in firmware: the vowel filter engine
(`vowel-processor.js`), contour/clip playback, all the Web Audio FX
(user explicitly wants NO effects chain in the pedal), and the per-channel
diagnostics (replace with nothing; LEDs suffice).

Architecture: audio callback at 48 kHz, block size 128 (was the libDaisy
default 48 until 2026-09-03; raised because the pitch tracker's
once-per-window burst needs a block it fits in, section 9). All parameter smoothing already exists
inside the engine (glide, gate slew, leveler slew). Static allocation
only; grain tables rebuilt only on character/param change, OUTSIDE the
audio callback (flag + rebuild in main loop, double-buffer the tables).

## 4. Engine invariants (break these and known bugs return)

Each of these fixed a bug the human heard. The mistakes log in HANDOFF.md
section 11 explains the history.

| Invariant | Prevents |
|---|---|
| Per-grain gain = 0.55 * vGain / sqrt(overlap), overlap = grainLen*f/sr, computed AT TRIGGER TIME | low notes ~3 dB/octave louder; level spikes on pitch motion |
| Side-voice gain taper: vGain = 1 - 0.45*abs(spread) | deep unison nulls -> +9 dB swells |
| ONE common vibrato LFO for the whole unison stack, computed once per sample outside the unison loop. The LFO was deleted 2026-09-01 and restored 2026-09-02 with knobs on menu 3; the shared-phase rule was re-examined then and kept. Jitter was NOT restored | staggered-phase frequency crossings -> intermittent spikes |
| Per-voice grain sinusoid phases, deterministic golden-ratio scatter, voice 0 = zero phase | coherent unison beating |
| Target leveler BEFORE envelope multiply: 8 ms rectified tracker, g = 0.09/(lvl+1e-3), clamp 0.25-4, slew 10 ms down / 60 ms up | formant-comb loudness (+/-7.8 dB between notes) and residual wobble |
| Envelope: 6 ms attack / 80 ms release, peak-normalised with FLOOR 0.05, output amp scaled x0.5 | silence-AGC (constant Ahhh from noise floor); WaveShaper-era clipping |
| Noise gate on raw env: hysteresis open at `gate`, close at `gate/2`, gain slew 5 ms open / 60 ms close | mixer hiss driving the voice; gate chatter |
| Mix ALL input channels (average) | right-channel guitar silently dropped |
| Pitch: BACF full-rate 70-1300 Hz, -45 dB hysteresis; hold last f on unvoiced | octave-down above F5 (old 700 Hz cap); tracking latency |
| Quantizer: 32 steps/semitone on the TRUE octave (idx = round(note*32)) | everything an octave low (MonkSynth's internal -12 offset trap) |
| Transpose = plain 2^octaveShift multiply; characters NEVER change pitch | the +2-octave "sounds too high" failure |
| Grain floor 16 Hz | -3 octave transpose clamping to 50 Hz |
| MonkSynth grain: 20 ms, 3 damped sinusoids, cosine window (1.8 ms attack, release from 13 ms), exp(-pi*BW*i/sr) decay, BW = 32.5/47.5/62.5 | the voice sounding wrong in ways nobody wants to rediscover |
| NO noise sources anywhere in the voice path. The engine's aspiration (2 inharmonic sines) is the nearest thing and the pedal pins it to 0: main.cpp never passes it through. Removed from the controls 2026-09-01 after it was measured as the on-hardware "static", +22.6 dB in the 3-6 kHz band at 0.3 with no change in broadband level | the rejected hiss |
| Unison pinned to 3, not reachable from any control. Retired 2026-09-01 after stacks above 3 were heard as a ringmod-like artifact on hardware, audible even at mix 0 where the voice path is multiplied by zero. Briefly dropped to 1 on 2026-09-02 and reverted the same day, see section 9 | per-block grain accumulation cost scaling with voice count |
| `DBSCREAMZ_MAX_UNISON=3` on the firmware build, so the grain tables hold exactly the pinned stack. `build_grains()` fills all `kMaxUnison` voices on every rebuild but `process_block` reads only the first `p.unison`, so voices 3..7 were built and never read until 2026-09-02. The HOST default stays 8 to match the frozen JS `MAX_UNISON`, and `main.cpp`'s `static_assert(kPinnedUnison <= kMaxUnison)` ties the two | 75 KB of SRAM spent on tables nothing reads, and grain rebuilds 8/3 more expensive than needed |
| Grain length pinned to 20 ms, not reachable from any control. Retired 2026-09-02 to free knob 6; every character and beast already stored exactly 20 | per-block cost scales with grain length as well as voice count |

## 5. Ear-approved defaults (v12) - bake as firmware constants

```
grainMs 20 and unison 3 both pinned (not reachable from any control),
  detuneCents 11 still set per character, vibRate 0 and vibDepth 0 (both
  knob-settable as of store v6: rate 0..50 Hz, depth 0..100 cents)
aspiration 0 (pinned; not reachable from any control)
octaveShift 0 (range -3..+3), followPitch on, quantize ON
glideMs 0, ampComp ON (MonkSynth low-note boost), taperEnd on (user ran
  off; either fine - expose nothing), leveler ON
gate 0.02 default - MUST be user-adjustable (knob or trim)
inputGain: hardware analog gain replaces the lab's digital x4; gate
  threshold must be calibrated against the Hothouse input stage level
vibrato per character from presets.json; vibJitter 0.10 (v12 lab baseline
  only: no character stores a nonzero vibrato, and the engine's own LFO
  restored 2026-09-02 has no jitter, so this baseline stays unreproduced
  on the pedal)
characters: Wukong 858/1234/3112 Hz, Rice 1000/1438/3625,
  Prince 742/1066/2688, Piccolo 698/1003/2529 (+ vib rate/depth each, v12
  lab baseline only)
```

## 6. Control mapping (milestone 5, IMPLEMENTED)

The control surface, voice memory, and post chain are specified in
docs/superpowers/specs/2026-08-26-milestone5-controls-design.md (approved
2026-08-26) and implemented in firmware/hothouse/ (main.cpp plus the
ui_controller / voice_params / param_map / post_chain / knob_pickup
headers). firmware/MILESTONE5.md is the on-device runbook.

Summary: 4 voices (2 stomp slots x 2 memory sets, factory Wukong/Prince and
Rice/Piccolo), one volatile edit buffer, three knob layers (default layer:
vocal vol / mix / master / tone / glide / vocal size; RIGHT-stomp hold-menu:
F1/F2; LEFT-stomp hold-menu: F3, vibrato rate, vibrato depth and detune;
the pedal had no vibrato from store v3 (menu 3's bottom row was the voices
stack instead), no aspiration as of v4, and no drive as of v5, which
retired the drive knob and replaced it with vocal size in the same slot;
store v6 retired voices (2026-09-01) and then grain length (2026-09-02) in
turn, pinning them to 3 and 20 ms, and gave the two knobs they freed to
vibrato rate and depth, restoring the LFO the pedal had lost since v3),
toggles = octave / memory page /
gate level, voice-only post chain (tilt tone), QSPI persistence
via libDaisy PersistentStorage, save gesture = hold one stomp ~1 s then
press the other while still holding (menus never save; in a menu the
other stomp's tap switches menus and the own stomp's tap exits; amended
2026-08-27). Physical mapping: FOOTSWITCH_1/LED_1 = LEFT, FOOTSWITCH_2/LED_2
= RIGHT (derived from the Hothouse PCB netlists; see the milestone 5 plan).
LED colours are a build choice, not firmware: LED_1 (LEFT) is BLUE, LED_2
(RIGHT) is ORANGE. Both are plain single-colour 3mm parts driven on/off,
so no LED ever changes colour at runtime. The stock Hothouse kit BOM calls
for two red 3mm LEDs; we deviate. Any manual or booklet copy must say blue
LEFT / orange RIGHT.
Charge mode (2026-08-27): both stomps together while engaged ramps a
configurable power-up boost (gain/pitch/tone/size overlay, never
written to the edit buffer), configured via toggles while a menu is
latched; spec in docs/superpowers/specs/2026-08-27-charge-mode-design.md.
The size row was a stack row until the vocal size redesign (spec
2026-09-01-vocal-size-design.md): charge mode no longer ramps the unison
count during a charge, it ramps vocal size instead.

The section-6 proposal that previously lived here (FS2 character cycle,
knob-per-function map) is obsolete; the open questions it posed were
resolved by the spec.

## 7. Milestones and test plan

| # | Milestone | Pass test |
|---|---|---|
| 0 | Host build harness: compile the full engine as portable C++ on the Linux box, WAV in -> WAV out | renders of guitar_long.wav match the v12 lab renders by ear and by the session's metrics (flatness ~1e-4, jump rate ~0.5/s, flat loudness) |
| 1 | Hothouse blink + passthrough | audio in -> out clean on real hardware |
| 2 | FOF at fixed pitch, 4 characters on FS2 | sounds like the lab with followPitch off |
| 3 | Envelope + gate live | silence is SILENT; picking dynamics track |
| 4 | BACF live tracking | plays like the deployed lab, minus browser latency |
| 5 | Controls + LEDs + bypass | full control map works |
| 6 | Second pedal flashed identically | friend smiles |

Milestone 0 is the important discipline: the entire engine verified
against the lab ON THE HOST before any hardware debugging can confound
it. This is the same JS-headless trick that caught every bug this
session, in C++ form.

## 8. Budgets and expectations

- CPU: engine ~7 MOPS + BACF (~8 ns/sample native). No SDRAM needed.
  This bullet used to claim "single-digit percent of the Seed3's 480+ MHz
  M7" flatly. That held for the 3-voice default but NOT across the range
  the retired voices knob could reach, so it is amended rather than
  deleted. Per-block cost scales with the unison count: every grain
  trigger accumulates a full grain length into the overlap buffer once
  per voice, so 8 voices at a 40 ms grain is roughly eight times the
  work of the default, in bursts that land on whichever block the
  triggers happen to align in. On 2026-09-01 high stacks were audible
  as a ringmod-like artifact, still present at mix 0 where the voice is
  multiplied by zero, which is what pointed at cost rather than signal.
  Unison is pinned to 3 for that reason (main.cpp `to_fof_params`).

  **2026-09-02: the estimate is no longer the only evidence.** A day spent
  chasing a high-note crackle on the host ruled out three candidates that
  had nothing to do with per-block cost:

  - **The voice is not clipping.** Rendering `--set followPitch=0` and
    sweeping `targetF0` from 100 Hz to 2 kHz, peak output stays between
    0.08 and 0.25 and *falls* with pitch. The `/sqrt(overlap)` division at
    `fof_engine.hpp:227-228` plus the leveler holds it flat.
  - **The doubles are not emulated.** libDaisy builds
    `-mfpu=fpv5-d16 -mfloat-abi=hard`, so the `pow`/`log2`/`sin` in the
    synth loop run on the hardware FPU.
  - **The pitch tracker does not break down up there.** Synthetic tones
    from 110 Hz to 1320 Hz track with zero standard deviation and zero
    octave jumps, including above the detector's stated 1300 Hz ceiling.
    Total render time across that range rises only ~20%, so the
    `bacf_period_detector::autocorrelate()` nested loop over `num_edges()`
    is not the quadratic burst it looks like on paper.
    **(2026-09-03: this last inference was wrong, see below. The tracker
    does not break down; it just gets expensive.)**

  The engine renders clean at every pitch, which leaves the audio ISR
  deadline (a 1 ms budget at `kBlockSize` 48 / 48 kHz) as the surviving
  hypothesis, plus `post_chain.hpp:34`, which has 4x of available gain
  above a 0.25-peak signal and no limiter anywhere.

  The CpuLoadMeter in `main.cpp` closes the measurement that this section
  had listed as open since the project began. It logs the WORST block per
  second, not the average, because one block over budget is one audible
  dropout, and it logs the tracked f0 at that block so the pitch
  dependence is visible directly. Do not go back to estimating.

  **2026-09-03: measured, and the crackle IS a deadline miss.** The meter,
  on hardware, playing up the neck:

  ```
  cpu avg 41.5%  worst 51.3%   worst@ 110 Hz
  cpu avg 50.9%  worst 131.9%  worst@ 660 Hz
  cpu avg 54.8%  worst 91.3%   worst@ 879 Hz
  cpu avg 57.8%  worst 112.9%  worst@ 1047 Hz
  ```

  Base cost is ~50% of the block. On top of it, once every `window/2` =
  688 samples (14.3 ms), `bacf_period_detector::autocorrelate()` runs
  inside ONE sample's call: for every pair of qualifying rising edges
  within half a window it runs a bitstream ACF over 20 32-bit words, and
  the firmware build calls libgcc `__popcountsi2` for every word (no
  POPCNT on Cortex-M7; `build/main.lst`). Edges grow linearly with f0, so
  pairs grow quadratically. Counted on the host with an instrumented copy
  of the header, fed `guitar_long.wav` the way the pedal feeds it:

  | input | worst window: ACF calls | words |
  |---|---|---|
  | real DI, as recorded (~100-300 Hz) | 75 | 1,500 |
  | real DI, +1 octave | 212 | 4,240 |
  | real DI, +2 octaves (up to 1.1 kHz) | **646** | **12,920** |
  | synthetic 1320 Hz + 2 harmonics | 11 | 220 |

  ~35 cycles a word makes the worst burst ~0.9 ms on its own, which is
  the 60-80 points the meter shows above the base. Why the 09-02 host
  check missed it: `autocorrelate()` returns early on a perfect match
  (`count == 0`), which a clean synthetic tone hits at once (last row,
  30x less work than a real guitar); a total-time average amortises the
  burst over 688 samples; and x86 has hardware popcount. The same early
  exit is why the crackle is "semi-deterministic": a window that happens
  to correlate perfectly costs almost nothing, so the same note is fine
  one time and over budget the next.

  Fix, in three independent steps, each measured with the meter:

  1. **DONE 2026-09-03: memoise the per-sample transcendentals**
     (`fof_engine.hpp` synth loop, `pitch_math.hpp octave_multiplier`).
     `pow(2, octave)` is hoisted per block; the LFO `sin` is skipped when
     it cannot contribute; and `pow(2, semis/12)`, `quantize_hz`, and
     `amp_comp_gain` are each cached on their exact input per voice.
     With vibrato off and the glide settled (cur_f0_ converges EXACTLY
     onto f0 within ~35 ms at glide 0) the steady-state loop runs zero
     transcendentals per sample. Verified bit-identical against a
     snapshot of the previous engine on `guitar_long.wav` across 12
     param sets (octave, glide, vibrato on/off/switching, detune, unison
     1 and 3, quantize and follow_pitch flips); host loop time halved.
     **Revised the same day:** the first version keyed the quantize memo
     on the exact `f`, and the meter showed it only paid off on clean
     sustained notes (avg 24% at 128 samples one run, 50% the next, same
     code). On the real DI it MISSED 50-74% of voice-samples: every
     tracker update, bend or finger vibrato restarts the glide and each
     restart misses for ~35 ms. `quantize_hz` only depends on the
     1/32-semitone bin, so the memo is now keyed on the bin, with the
     bin's Hz interval shrunk 1e-9 relative each side so the fast path
     can never disagree with the log2 path (`pitch_math.hpp
     quantize_bin / quantize_bin_hz`). Misses on the DI: 0.5-1.8%.
     Verified bit-identical against the pre-memo snapshot, 36 cases
     (12 param sets x DI at x1/x2/x4 speed). Host loop 415 -> 92 ms.
     Vibrato ON now costs 4 transcendentals a sample (the LFO sin plus
     one pow per voice for `semis`; folding that across voices would not
     be bit-exact), down from 10. A charge sweep is just a long glide
     and is covered by the bin memo like any other pitch motion.
  2. **DONE 2026-09-03 (awaiting the hardware number): inline popcount**
     via `firmware/engine/q_shim/q/detail/count_bits.hpp`, a shim ahead
     of q on both Makefiles' include paths (the pattern
     `host/third_party/q/utility/bitset.hpp` already uses). Replaces the
     per-word `bl __popcountsi2` (libgcc byte-table loop, 4 loads +
     call/return) with 12 inline ALU instructions; verified from the
     M7 assembly that GCC 13 does not fold the idiom back into the
     builtin. Bit-exact by definition; `test_count_bits` checks every
     16-bit pattern against the builtin AND that the shim is the header
     bitstream_acf actually picks up. **Measured:** the burst (worst
     minus avg at 1.1 kHz) went from ~28 points of the 2.67 ms block to
     ~18. That run's avg was 20 points HIGHER than the run before it at
     every pitch, which was not the shim (it only runs inside the burst)
     but the exact-f quantize memo missing on a played-like-a-guitar
     take; see the step 1 revision above.
  3. **DONE 2026-09-03: `kBlockSize` 48 -> 128** (main.cpp). The burst
     lands at most once per block, so a 2.67 ms deadline absorbs it.
     ~1.7 ms more latency. This is the margin for the vibrato-on and
     charge-sweep cases step 1 cannot help. Taken before step 2 because
     the step 1 hardware numbers showed the WORST block had only moved
     5-10 points (avg fell 41% -> 24%, worst 113% -> 108% at 1050 Hz):
     the tracker publishes its new f0 in the same sample the burst runs
     in, and any f0 change restarts the glide, which misses the memo for
     ~35 ms, so the burst block is also a full-cost block.
     **Measured after steps 1 and 3** (percent of the 2.67 ms block):
     worst 35-45% below 500 Hz, 69% at 1105 Hz, 73% at 1155 Hz, and the
     crackle is gone. The remaining exposure is vibrato ON or a charge
     sweep at the top of the neck, where the memo misses every sample:
     estimated ~82% from the pre-memo base cost, so still under budget
     but with the least margin the pedal has. Step 2 is what buys that
     margin back.

  The vibrato LFO restored 2026-09-02 costs one `std::sin` per sample,
  shared across the whole stack rather than one per voice. Against the
  pedal as it stood on 2026-09-01 that is one more transcendental per
  sample; against 2026-08-31 it is one FEWER, because `ec97ed6` removed
  two (the LFO and its jitter oscillator) and only one came back. If the
  ringmod artifact is ever heard again this LFO is a suspect too, and the
  cheap fix is a lookup-table LFO rather than removing the control.
- RAM: measured from the map file, not estimated. `.bss` links into
  SRAM (512 KB at 0x24000000), NOT the 128 KB DTCMRAM, and was 119 KB
  at MAX_GRAIN_LEN 1024. The grain tables dominate it at 64 bytes per
  sample of length (2 sets x 8 voices x 4 bytes), so the 1920 kept as
  deliberate 2x headroom over the 960-sample 20 ms grain pin (main.cpp)
  puts `.bss` near 175 KB, about a third of SRAM.
  There is room; RAM has never been the binding budget here.
- Flash IS the binding budget: `.text` was 121,696 of 131,072 bytes
  (92.8%) as of 2026-08-31. Grain tables are `.bss` and cost none of it,
  but new CODE is nearly out of room. Check the map before adding any.
- Latency: ~28-32 ms note-lock (BACF, measured) + ~2-3 ms codec I/O.
  Better than the browser because the browser I/O tax disappears.
- Binary: small; internal flash is fine.

## 9. Gotchas

1. Seed3 requires current libDaisy; pin the working commit.
2. Grain rebuilds happen in the main loop, never the audio IRQ.
3. The gate threshold units change on hardware (no more inputGain x4);
   recalibrate default by ear at milestone 3.
4. cycfi/q is C++20; arm-none-eabi-gcc must be recent (12+ safe).
5. Read HANDOFF.md section 11 (mistakes log) before debugging anything by
   intuition. Measure first. Every wrong guess this project made is
   documented there.
6. The lab (db-scream-z repo, v12) must stay untouched - it is the
   reference against which firmware is validated, and the human's
   instrument in the meantime.
7. The BACF pitch tracker's analysis window is word-size-dependent:
   `bacf_period_detector` rounds its window up to a whole number of
   `cycfi::q::bitset<T>` words, and the word width `T` changes which
   samples get analysed, changes `is_ready()` cadence, and changes which
   period gets picked. The v12 JS reference hardcodes 32-bit words
   (`const QVS = 32`); firmware MUST verify the BACF bitset instantiates
   over 32-bit words (`natural_uint` on the Cortex-M7 target, which is
   naturally 32-bit) or pitch behavior silently shifts away from the
   ear-validated v12 reference with no compiler error. Measured on the
   host: an unshimmed 64-bit bitset locked the first note to 332.5 Hz
   where the reference locks to 83.7 Hz, with 23% of trace blocks a whole
   octave off. See `firmware/MILESTONE0.md` section 1 and the compile-time
   `static_assert` in `firmware/host/tests/test_pitch_tracker.cpp`.

## 10. Suggested kickoff

Where things stand (2026-08-31): **it runs on real hardware.** The
milestone 5 firmware, charge mode included, was built and flashed to an
assembled Hothouse with a Seed3 and played: audio path, footswitches,
LEDs, voice recall and the engage/bypass path all work on first power-on.
Build size 123,060 B, 93.88% of the 128 KB internal flash. HANDOFF.md and
PROTOTYPE.md are historical background from the research phase, not
current state.

What has NOT been done on hardware yet: the six criterion scripts in
MILESTONE5.md section 5 as a full pass, the charge-mode test script
(section 8), and the gate calibration (section 6). The three kGateLevels
values and every charge-mode modulation depth are still placeholders
carried over from the browser lab, never tuned by ear on the real analog
input stage.

To pick the work up, start a fresh session in this project directory
with:

> Read FIRMWARE.md, then firmware/MILESTONE5.md. The pedal is built and
> flashed and works. Run the on-device test scripts in MILESTONE5.md
> section 5 (spec success criteria 1-6) in order, then the charge-mode
> script in section 8, and finish with the gate calibration in section 6.
> If anything fails, use the triage table in MILESTONE5.md section 7 and
> the earlier runbooks (firmware/MILESTONE1.md, firmware/MILESTONE2-4.md)
> before debugging by intuition.

The earlier milestone runbooks stay relevant on hardware day:
MILESTONE1.md has the build/flash mechanics, MILESTONE2-4.md has the
fixed-pitch and tracking checks to fall back to if a milestone 5 script
fails, and MILESTONE0.md documents the host harness that regenerates the
reference renders.

## 11. Beast mode (the hidden bank)

**Gesture: hold BOTH footswitches while powering the pedal on.**

The four DBZ characters are replaced for that session by four sustained
animal calls: Set 1 is Cow (right) / Wolf (left), Set 2 is Whale (right)
/ Elephant (left). Toggle 2 still pages between the sets, Freeform is
unchanged, and charge mode works on the animals exactly as it does on the
characters.

Nothing is written to QSPI. The save chord still works as a session
sandbox (tweak a cow, save it, keep it until you pull the plug), but
`main.cpp` skips `storage->Save()` while beast mode is active, so the
saved characters can never be overwritten by it. Power cycle to get them
back.

Why it is cheap: `ui_controller.hpp` holds a `const VoiceStore*` and only
ever reads it, so the whole mechanism is pointing that at a static store
built by `beast_store()`. The switches are already debounced by the
50-pass ADC settle loop that runs before `ui.init()`.

**The one real collision, and how it is handled.** The pedal boots
BYPASSED, which is exactly when the Hothouse DFU escape is armed, and that
escape fires once both stomps have been held for 2000 ms
(`CheckResetToBootloader` / `HOLD_THRESHOLD_MS` in
`third_party/HothouseExamples/src/hothouse.cpp:181`). Left alone, holding
both stomps a beat too long at power-up would load the bank and then
reboot straight into the bootloader, which is a trap rather than a
feature. So the entry gesture CONSUMES that grip: `main.cpp` swallows the
DFU check until both stomps have been seen released once. After the first
release the escape behaves exactly as it always has, beast session or not.
Skipping the call outright rather than gating it also leaves Hothouse's
`dfu_start_time_` at 0, so no partial hold is banked while we wait.

This is the one behaviour here that no host test can cover, because it
lives in `main.cpp` and `hothouse.cpp`. Check it by hand on the next
hardware pass: hold both stomps through power-up for a good five seconds
and confirm the pedal is playing animals rather than sitting in DFU, then
release, hold both again for 2 s, and confirm the LEDs do their triple
alternation and the board enumerates as `0483:df11`.

The bank lives in `firmware/hothouse/beast_presets.hpp`, deliberately
outside the generated `firmware/engine/presets.hpp`: a `CharacterPreset`
carries only formants and detune, and these voices
need octave, glide and bandwidth to sound like anything. The engine is a
pitch-tracked formant resynth, which is why all four are SUSTAINED calls;
it does vowel-like animal voices well and percussive ones (a bark, a
quack) badly, because there is no per-attack amplitude shaper or noise
burst anywhere in the chain.

Every number in the bank is a first-pass ear target, in the same state as
the `kGateLevels` placeholders: plausible on paper, never heard through
an amp. `firmware/host/tests/test_beast_presets.cpp` asserts ranges and
relationships rather than exact values so a tuning pass does not mean
rewriting the test.

**Flash cost: NOT YET MEASURED.** The bank is ~208 bytes of table plus
the builders and about fifteen lines in `main.cpp`; the estimate is well
under 1 KB against the 7,916 bytes that were free at 123,156 B. Measure
it on the next build and record the real number here rather than trusting
that estimate.

The booklet (`docs/BOOKLET.md`, generated) carries a hint that the four
voices exist and does not say how to reach them. This section and
HANDOFF.md are the only places the gesture is written down.

### In the emulator

`pedal/` gets the same bank via `?beast` (or `#beast`) on the URL, which
is the browser's only equivalent of a decision made at boot. `app.js`
swaps the store and its `persist()` refuses to write, mirroring the
firmware's skipped QSPI commit. `pedal/static/beast-presets.js` is a
value-for-value port, and `pedal/tests/presets.test.mjs` parses
`beast_presets.hpp` to keep the two from drifting, the same way it
already does for `presets.hpp`.
