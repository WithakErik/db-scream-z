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
   deprecated snap mode). As of store v3 the header also carries a
   per-character unison stack (unison / detune_cents / aspiration) that
   does NOT come from presets.json, because that file is a frozen copy of
   the lab's; it lives in `tools/gen_presets.py` STACK. The character
   vibrato that used to be in the header went with the vibrato removal
   and now lives only in `firmware/host/render.cpp`, for parity renders.
4. The old YIN/causal tracker in the same file is DEAD CODE for firmware:
   do not port it. BACF replaced it (measured better on every metric).

Also in the lab file but NOT wanted in firmware: the vowel filter engine
(`vowel-processor.js`), contour/clip playback, all the Web Audio FX
(user explicitly wants NO effects chain in the pedal), and the per-channel
diagnostics (replace with nothing; LEDs suffice).

Architecture: audio callback at 48 kHz, block size 4-48 (start with
libDaisy defaults, then minimise). All parameter smoothing already exists
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
| ONE common vibrato LFO for the whole unison stack (jitter on LFO rate) | staggered-phase frequency crossings -> intermittent spikes |
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
| NO noise sources anywhere in the voice path (aspiration = 2 inharmonic sines, default 0) | the rejected hiss |

## 5. Ear-approved defaults (v12) - bake as firmware constants

```
grainMs 20, unison 3, detuneCents 11, aspiration 0
octaveShift 0 (range -3..+3), followPitch on, quantize ON
glideMs 0, ampComp ON (MonkSynth low-note boost), taperEnd on (user ran
  off; either fine - expose nothing), leveler ON
gate 0.02 default - MUST be user-adjustable (knob or trim)
inputGain: hardware analog gain replaces the lab's digital x4; gate
  threshold must be calibrated against the Hothouse input stage level
vibrato per character from presets.json; vibJitter 0.10
characters: Wukong 858/1234/3112 Hz, Rice 1000/1438/3625,
  Prince 742/1066/2688, Piccolo 698/1003/2529 (+ vib rate/depth each)
```

## 6. Control mapping (milestone 5, IMPLEMENTED)

The control surface, voice memory, and post chain are specified in
docs/superpowers/specs/2026-08-26-milestone5-controls-design.md (approved
2026-08-26) and implemented in firmware/hothouse/ (main.cpp plus the
ui_controller / voice_params / param_map / post_chain / knob_pickup
headers). firmware/MILESTONE5.md is the on-device runbook.

Summary: 4 voices (2 stomp slots x 2 memory sets, factory Wukong/Prince and
Rice/Piccolo), one volatile edit buffer, three knob layers (default layer:
mix / glide / master / vocal vol / drive / tone; RIGHT-stomp hold-menu:
F1/F2; LEFT-stomp hold-menu: F3 + unison stack, meaning voices / detune /
aspiration; the pedal has no vibrato as of store v3), toggles = octave / memory page /
gate level, voice-only post chain (tanh drive, tilt tone), QSPI persistence
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
configurable power-up boost (gain/pitch/tone/aspiration overlay, never
written to the edit buffer), configured via toggles while a menu is
latched; spec in docs/superpowers/specs/2026-08-27-charge-mode-design.md.

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

- CPU: engine ~7 MOPS + BACF (~8 ns/sample native) - single-digit
  percent of the Seed3's 480+ MHz M7. No SDRAM needed (~90 KB RAM).
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
