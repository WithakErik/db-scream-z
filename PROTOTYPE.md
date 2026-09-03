# DBscreamZ - Prototype Plan

Companion to HANDOFF.md (read that first for the algorithm history).
The algorithm is settled and ear-approved in the browser lab; this document
is the execution plan to get it onto hardware.

## Contents

1. Milestone 0: live-input lab (no purchase, do this first)
2. Hardware options and recommendation
3. Parts list
4. Firmware architecture and port map
5. Milestones and test plan
6. Firmware default settings (ear-approved)
7. Open risks

---

## 1. Milestone 0: live-input lab (no purchase, do this first)

The lab currently plays a looped DI with a PRECOMPUTED pitch contour. That
was a convenience, but it means the causal pitch tracker has never run on
live playing, and the ~98 ms tracking latency has never been felt under the
fingers. Both must be validated before buying anything.

**Build a "Live input" mode in the lab:**

- `getUserMedia` -> audio interface -> the guitar, live.
- Port the pitch chain INTO the worklet, running in real time:
  decimate 48k -> 8k (32-tap FIR), YIN (40 ms frame, 5 ms hop, 70-700 Hz),
  attack blanking (6 frames), voted re-anchor (3 frames), octave anchor with
  300 ms hysteresis, range fold, causal median of 5.
  All of this already exists in Python (`dbscreamz_lab/build_assets.py`,
  `causal_pitch()` and `yin()`); it is a direct transcription.
- Amplitude envelope follower replaces the precomputed amp contour
  (attack ~5 ms, release ~80 ms, then the existing amp mapping).
- Keep the looped-clip mode for A/B regression.

**Exit criteria:** the human plays through it live and answers the two
questions no measurement can: is the latency playable, and does the tracker
hold up on real dynamics. If latency fails, swap the YIN front end for the
Cycfi Q approach (HANDOFF section 7) BEFORE hardware, while iteration is
still free.

**STATUS 2026-08-25: IMPLEMENTED, verified headlessly, awaiting live play.**
The full pitch chain now runs inside `fof-processor.js` (decimator, YIN,
attack blank, vote, hysteresis, fold, median-5) plus a live envelope
follower (6 ms attack, 80 ms release, running-peak normalisation, 0.5
headroom scale so pick attacks cannot clip the downstream WaveShaper).
Headless test feeding the real 35 s DI as live input: 3% of one core,
octave-jump rate 1.23/s (offline reference 1.4/s), output peak 0.56,
finite, render at /tmp/dbscreamz_v6/live_sim_Wukong.wav.

**DEPLOYED (2026-08-25):** <https://withakerik.github.io/db-scream-z/>
GitHub Pages from the public repo `WithakErik/db-scream-z`, so the human
can test live input on the computer connected to the mixer. (The repo was
renamed on 2026-08-27; GitHub does NOT redirect the old github.io
address, and the rename also silently disabled Pages, which had to be
re-enabled from the repo settings.) HTTPS, so
getUserMedia works. The `ref_*.wav` reference clips are deliberately
EXCLUDED from the public deploy (copyright); reference playback buttons are
inert there. **To redeploy, see `docs/PUBLISHING.md`.** (This used to read
"copy the static files, minus ref audio, into a clone of that repo, commit,
push". That was the process before `tools/publish.py`, which replaced the
by-hand copy with an allowlist and a deny-scan, precisely because
remembering to leave the reference audio out is not a guarantee.)

**Things for the human to listen for, in order:**
1. Latency feel: pick a note, does the voice land acceptably late?
2. Octave consistency: play the SAME note repeatedly; does the voice ever
   answer an octave apart on different picks? (Headless analysis shows the
   live and offline front ends choose different octaves on 28% of ambiguous
   frames; per-note pitch is tight at 0.19 st once octave choice is fixed.)
3. Mid-note stability during bends and vibrato.
4. Chords: expected to glitch; the engine is monophonic by design.

This milestone is also 90% of the firmware port done early: the worklet then
contains the complete, self-sufficient audio-in -> audio-out algorithm, and
the C port becomes a transcription job.

---

## 2. Hardware options and recommendation

All numbers from FEASIBILITY.md: ~17 MOPS total, 88 KB RAM, no external
SDRAM needed. Almost any modern MCU with an FPU works.

### Option A (recommended): Daisy Seed + pedal interface board

- Daisy Seed (STM32H750, 480 MHz M7, onboard 24-bit codec): 3.5% CPU load.
- Interface: PedalPCB **Terrarium** (mono, 6 knobs, 2 footswitches, cheap,
  well documented) or GuitarML **Funbox** (stereo, MIDI, expression).
- Why: zero codec bring-up, zero I2S debugging, libDaisy handles the audio
  callback, huge community of guitar-pedal precedents. Fastest path from
  parts to first sound by a wide margin.
- Caveats: check stock FIRST (supply has been unstable; Seed2 DFM / rev7 /
  Seed3 all acceptable, pin-compatible). The Terrarium needs the documented
  RC output-filter fix with current Seed revisions.

### Update 2026-08-25: two pedals, Seed3 x2 purchased

The human bought TWO Seed3s (one pedal for them, one for a friend).
Seed3 is pin-compatible with the classic Seed pinout; firmware must use a
current libDaisy for Seed3 support. Pre-assembled carrier option found:
**Cleveland Music Co. Hothouse** ($119 kit / $159 assembled, Seed NOT
included, Seed3 supported per their site, stereo, 6 knobs + 3 toggles +
2 momentary footswitches, open-source CC BY-SA with its own libDaisy board
file in clevelandmusicco/HothouseExamples). Sold out on their shop at time
of writing; they also list on Reverb. Terrarium has NO commercial
assembled option, only occasional secondhand forum/Reverb listings.
BOM for self-sourcing the Terrarium: `terrarium_bom.csv` (project root).

### CHOSEN PATH (2026-08-25): fab the Hothouse from official files

Cleveland's shop sold out, but `clevelandmusicco/open-source-pedals`
(CC BY-SA, branding already stripped) ships JLCPCB-TESTED fabrication
files: gerber zips for all three boards (main, switching/LED daughter,
power/audio IO daughter) plus BOM.csv and CPL.csv with JLCPCB part
numbers pre-assigned, adapted late-2025 for parts that are publicly in
stock at JLCPCB. Workflow: upload `hothouse-no-branding.zip` to JLCPCB
with PCBA enabled + BOM/CPL (SMD arrives pre-soldered); the two
daughterboards are through-hole only, order as plain PCBs. Min qty 5 =
five pedal sets. Their README also lists every through-hole part with
EXACT Tayda SKUs (headers A-1310, toggles A-3670, footswitches A-4807,
pots A-2969, DC jack A-4118, enclosure A-6628, drill+UV services) and
prebond 6-pin ribbon cables from StompBoxParts. Remaining soldering:
through-hole hardware only. NOTE: 1/4-inch audio jacks are absent from
their TH table; check the build guide wiki for the jack part before
ordering. Firmware target: Hothouse board file in HothouseExamples.

### Option B: bare MCU + codec (cheaper, more wiring, closer to production)

- RP2350 (Pico 2, ~$5) or STM32F411 Black Pill (~$6) + WM8731 or PCM3060
  codec breakout + JFET/opamp input buffer.
- You own: I2S/PIO bring-up, clocking, anti-alias filtering, power rails.
- Choose this only if Option A is unavailable or the goal shifts to
  designing the production PCB immediately.

### Option C: audio-interface "pedal" (zero new hardware)

Milestone 0 IS this option: laptop + interface in the signal chain. Not a
pedal, but proves everything except the enclosure. Do not skip it.

---

## 3. Parts list (Option A)

| Item | Est. cost | Note |
|---|---|---|
| Daisy Seed (any current rev, with headers) | ~$30-40 | check stock first |
| PedalPCB Terrarium PCB | ~$12 | or GuitarML Funbox |
| 125B enclosure, drilled | ~$15 | Tayda pre-drilled for Terrarium |
| 2x 1/4" mono jacks | ~$4 | |
| 9V DC jack (centre negative) | ~$2 | |
| 3PDT footswitch | ~$5 | true bypass |
| Momentary SPST (character cycle) | ~$2 | MVP spec: one button |
| RGB LED (common cathode) + resistors | ~$2 | character indicator |
| Pots/knobs (start with 3: octave, gain, glide) | ~$10 | |
| Misc: headers, wire, standoffs | ~$10 | |

Total roughly $90-100. Order the Seed immediately if in stock; everything
else is commodity.

---

## 4. Firmware architecture and port map

Plain C (float; H750/F411 both have FPUs, no fixed-point needed). Two rates:

- **Audio rate (48 kHz callback):** FOF overlap-add, leveler, output.
- **Control rate (200 Hz, from the audio callback every 240 samples):**
  decimated YIN, causal octave logic, envelope follower, glide targets.

Port map, JS/Python -> C:

| Source | Function | C module |
|---|---|---|
| `fof-processor.js` `buildGrain()` | per-voice grain tables | `grain.c` (rebuild only on preset/param change, off the audio thread) |
| `fof-processor.js` trigger + overlap-add | synthesis core | `fof.c` (the hot loop: phase accum, per-grain write with vGain/overlap norm, common vibrato LFO) |
| `fof-processor.js` leveler | ratio leveler | `level.c` (two one-poles + divide) |
| `fof-processor.js` `registerMap()` mode 0 | transpose | one multiply, table of 2^n |
| `build_assets.py` `yin()` | pitch detect | `yin.c` (on 8 kHz stream; 4.7 MOPS at 100 Hz control rate) |
| `build_assets.py` `causal_pitch()` | octave stabiliser | `pitch.c` (attack blank, vote, hysteresis, fold, median-5) |
| `presets.json` | 4 character presets | generated `presets.h` (write a 20-line generator script) |

Memory plan (static allocation, no heap):
grain tables 8 x 960 f32 (30 KB), overlap ring 200 ms (38 KB), pitch
buffers ~2.5 KB, everything else <1 KB. Fits F411's 128 KB with room; H750
does not even notice.

Controls for the prototype:
- Footswitch: true bypass (relay or 3PDT hard bypass).
- Button: cycle Wukong -> Rice -> Prince -> Piccolo. RGB LED colour per
  character (orange / white / blue / green).
- Knob 1: TRANSPOSE (detented feel: 0 / +1 / +2). Knob 2: output gain.
  Knob 3: glide. Everything else is a firmware constant from section 6.

---

## 5. Milestones and test plan

Each milestone has a pass test. Do not stack unverified layers.

| # | Milestone | Pass test |
|---|---|---|
| 0 | Live-input lab (section 1) | human plays live, judges latency + tracking |
| 1 | Hardware passthrough | guitar -> pedal -> amp, clean, noise floor acceptable by ear and scope |
| 2 | FOF at fixed pitch | button cycles 4 characters, each sounds like the lab render (A/B against `/tmp/dbscreamz_v6` WAVs regenerated locally) |
| 3 | Envelope follower | voice loudness follows picking; no synth heard when muted strings |
| 4 | Live pitch tracking | port `yin.c` + `pitch.c`; same riff through pedal vs lab live mode, comparable behaviour |
| 5 | Controls + LED + bypass | one-button character cycle, colour feedback, silent bypass |
| 6 | Enclosure | plays a rehearsal without embarrassment |

Milestone 2 before 4 on purpose: fixed-pitch FOF isolates codec/synth bugs
from tracker bugs, the exact separation that worked in the lab.

---

## 6. Firmware default settings (ear-approved, updated after LIVE testing v10)

The human's settings from playing live guitar through the deployed lab:

```
grainMs 20, unison 3, detuneCents 11, aspiration 0
octaveShift 0         (voice at guitar pitch; expose -3..+3 on a knob)
quantize ON, glideMs 0, tracker BACF (fastTrack obsolete)
followPitch on, ampComp ON (human preference, v12), taperEnd on,
leveler ON (target mode)
gate ~0.02 (MUST be a hardware trim/knob), inputGain per rig
ALL post-engine effects bypassed: the human wants no FX chain in the
pedal. Firmware scope = engine + gate + leveler only. (The FX added no
latency in the lab; they are simply not wanted.)
vibrato: common LFO, per-preset rate/depth (presets.json)
formants/vibrato per character: presets.json (regenerate presets.h)
```

Engine invariants that must survive the C port: constant-power grain
normalisation (1/sqrt overlap, x0.55), target leveler (8 ms tracker,
asymmetric 10/60 ms gain slew, clamp 0.25-4, target 0.09), common vibrato
LFO, side-voice taper 0.55, per-voice grain phases, noise gate with
hysteresis, pitch range 75-1250 Hz (YIN tmin 6 at sr/6).

Measured tracker numbers (v10): note-lock 37-45 ms fastTrack / 75 ms
normal; octave jumps on reference DI 2.35/s fastTrack / 1.26/s normal;
correct tracking verified 82-1175 Hz.

### v11: Cycfi Q BACF tracker is now the default (2026-08-25)

The Cycfi swap recommended in HANDOFF section 7 is DONE, in the lab.
A line-faithful JS port of cycfi/q's pitch stack (BSL-1.0 license,
32-bit words instead of 64) lives at the top of `fof-processor.js`,
validated against a native C++ build of the same library
(`scratchpad/bacf_shootout.cpp`):

| metric | YIN normal | YIN fast | BACF (C++) | BACF (JS port) |
|---|---|---|---|---|
| note lock | 75 ms | 37-45 ms | 28-32 ms | 16-32 ms |
| octave jumps/s (DI) | 1.26 | 2.35 | 0.68 | 0.45-0.49 |
| pitch error | ~10 cents high | same | +2 cents | 0.00 st |
| CPU | - | - | 8 ns/sample | 1.9-3.9% JS realtime |

No blanking/voting/median scaffolding needed; the detector's own bias
logic and median-3 are the whole stabiliser. YIN kept as a UI fallback
toggle. **For firmware, use cycfi/q's C++ headers directly** (header-only,
BSL-1.0, ~1500 lines incl. utilities); the JS port doubles as readable
reference. Detector config: 70-1300 Hz, -45 dB hysteresis, full audio
rate (no decimation).

Unison implementation notes that must survive the port (HANDOFF section 4a):
common vibrato LFO across voices, side-voice gain taper 0.55, per-voice
grain phase scatter, per-grain overlap normalisation at write time, ratio
leveler before the envelope multiply. Removing any of these brings back the
intermittent volume spikes.

---

## 7. Open risks

1. **Latency playability is still unproven live.** Milestone 0 answers it.
   Fallback: Cycfi Q front end (HANDOFF section 7), ~12-30 ms potential.
2. **Analog input stage noise.** The known Terrarium/Seed RC-fix issue;
   budget an evening for gain staging and grounding.
3. **Residual "spikes" reported 2026-08-25** are believed to be the dry
   track's own dynamics passing through the envelope follower (the anomaly
   metric, which excludes guitar-envelope jumps, reads zero). On hardware
   this becomes an envelope attack/release tuning question. If live play
   still shows level jumps the guitar did not make, reopen HANDOFF 4a.
4. **Daisy stock.** If unavailable in 2 weeks, fall back to Option B rather
   than waiting.
5. **Commercial use**: personal build only. No sampled audio is used at
   runtime, only derived coefficients.

