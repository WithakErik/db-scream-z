<p align="center">
  <img src="images/logo.png" alt="DBscreamZ" width="480">
</p>

<p align="center">
  A guitar pedal that turns your playing into charged-up vocal screams.
</p>

<p align="center">
  <strong>▶ <a href="https://withakerik.github.io/db-scream-z/">Try it live in your browser</a></strong>
</p>

---

## What this is

DBscreamZ does one thing: play a note, and a voice screams it back at you. The voice is synthesized, not sampled, using a MonkSynth-style
**FOF** [formant wave function] engine driven by a **Cycfi Q BACF** [bitstream autocorrelation function] pitch
tracker running on the live guitar signal. It tracks what you play, so the
scream bends and vibratos with the note.

Six voices ship in the pedal: **Wukong**, **Prince**, **Rice**,
**Flute**, **Master** and **Ki-Ki**. Two more (**Boo** and **Fling**) are
recipes in the [manual](manual/DBscreamZ-manual.pdf) that you dial in by hand
and save to a slot.

There is deliberately **no noise anywhere in the voice path**. No white noise,
no hiss generator, no impulse responses. Everything is grains and resonators.
An earlier version injected noise to match a measured harmonics-to-noise
ratio and was rejected by ear on the spot.

The engine can add breath as two extra inharmonic tones inside the voice
rather than as a noise source, but the pedal keeps it switched off: on
hardware those tones read as static, so no control reaches them.

It runs on the [Cleveland Music Co. Hothouse](https://clevelandmusicco.com/hothouse-diy-digital-signal-processing-platform-kit/)
platform with a **Daisy Seed3**, in a 125B enclosure: six knobs, three
three-position toggles, two footswitches, two LEDs [light-emitting diodes].

## Try it live

**<https://withakerik.github.io/db-scream-z/>**

The whole pedal, in the browser. Not a video and not a simplified demo: the
same FOF synthesis in an AudioWorklet, the same voices, and the same control
surface the firmware runs. Stomp holds latch the same menus, the toggles
switch to charge settings in the same places, and the LEDs say the same
things.

One thing it does that the pedal cannot: the knobs move themselves. Latch a
menu on hardware and its six pots still point wherever you left them, which is
why a knob there stays inert until you nudge it. On screen they swing to the
values the layer actually holds.

Three ways to feed it:

- **the built-in clips**, for hearing it with nothing plugged in
- **live input**, so you can play a guitar through your interface into it
- **your own track**: drop in a file, play it through, tweak while it runs,
  and render the result back out as a WAV [Waveform Audio File Format] file

Add `?chord` to the page address to try [chord mode](#chord-mode) instead of
the six characters.

Two controls are buttons rather than switches, because one mouse pointer
cannot press two footswitches at once: **CHARGE** and **SAVE**. Everything
else is the real gesture.

Click **Start Audio** first; browsers block audio until you interact with the
page.

## Using the pedal

The [full manual](manual/DBscreamZ-manual.pdf) is a printable booklet with a
settings card for every voice. This is the short version.

### Quick start

Plug in, power on. The pedal boots **bypassed**, both LEDs off. Flip the
middle toggle up (Set 1) and tap the RIGHT stomp: Wukong engages and the right
LED [light-emitting diode] glows solid orange. Tap again to bypass. Tap LEFT for Prince, and the left
LED glows blue. Flip the middle toggle to the middle for Set 2: Rice on the
right, Flute on the left, or down for Set 3: Master on the right, Ki-Ki on
the left.

The LEDs are single-color and never change: LEFT is always blue, RIGHT always
orange. Only whether they are off, solid or blinking ever changes.

### Knobs

Normally the six knobs are the default layer:

| Knob | Function |
|---|---|
| 1 | Vocal volume |
| 2 | Mix (dry to voice) |
| 3 | Master volume |
| 4 | Tone (dark, flat at center, bright) |
| 5 | Glide (0-300 ms) |
| 6 | Vocal size (character as written to deepest) |

Hold the RIGHT stomp: about a second in, **menu 2** latches and
the right LED starts blinking, and the knobs now edit formants F1 and F2 [formant 1 and formant 2]. It
does not wait for you to let go, and letting go changes nothing. Hold LEFT
the same way for **menu 3**, F3 [formant 3] and vibrato. While a menu is latched, tap the
*other* stomp to jump straight to the other menu; tap the blinking side's own
stomp to exit.

Only the blinking LED is lit in a menu. The other goes dark even if that was
the engaged side: the sound keeps playing, its LED just steps aside so one
blinking light is never mistaken for two lit ones.

After any layer change a knob is inert until you move it, so nothing ever
jumps to wherever the knob happens to be pointing.

Menu 3's bottom row is **vibrato**: how fast the pitch wobbles (knob 4), how
far it swings (knob 5), and how far apart the character's three stacked
voices are tuned (knob 6, up to 60 cents either side). Every voice ships with vibrato
off.

Knob 4 barely moves for its first half: off at the counter-clockwise stop,
opening into a natural, singer's wobble around 12 o'clock. Past that it
speeds up fast, ending in a fluttering, metallic warble no voice could do
on its own. Knob 5 sets how far each wobble swings the pitch, from nothing
up to a full semitone. Knob 6 spreads the three voices apart; wound to zero
they collapse into the bare, focused version of the character, and opened
up they thicken into a chorus.

Back on the default layer, knob 6 is vocal size. It scales all three
formants together, which is acoustically vocal tract length, so the voice
sounds like it is coming from a physically larger creature: same character,
same vowel, much bigger body. All the way down is the character exactly as
written; all the way up is the deepest it goes.

### Toggles

| Toggle | Up | Middle | Down |
|---|---|---|---|
| 1 Octave | +1 | 0 | -1 |
| 2 Memory page | Set 1 | Set 2 | Set 3 |
| 3 Gate | high | medium | low |

Each page holds two voices, one per stomp: Set 1 is Wukong (right) and
Prince (left), Set 2 is Rice and Flute, Set 3 is Master and Ki-Ki. While
a menu is latched the toggles do something else entirely: they configure
charge mode.

### Saving

Everything you tweak is live but volatile. To save, hold one stomp past a
second and, while **still holding it**, press the other. The held side picks
the slot: hold RIGHT and press LEFT to write the right slot of the current
page. Both LEDs blink three times. That hold latches its menu on the way
past a second, as any hold does; pressing the other stomp drops the menu
again and saves, so you end up back where you started. Menus never save;
exit the menu first, and your tweaks survive.

### Charge mode

With a voice **engaged** and no menu latched, stomp both switches together
and hold. The sound charges like a power-up: gain swells, the voice grows,
and the pitch glides toward two octaves away for as long as you hold. On
the gain and size rows the up position goes all the way to the top and
the middle position halfway. The LEDs alternate faster and faster. Release
to let it wind down. It is an overlay, so it never touches the voice you
have saved.

Configure it with the toggles while a menu is latched:

| Toggle | Menu 2 latched | Menu 3 latched |
|---|---|---|
| 1 | Gain: big / **on** / off | Pitch: **rise 2 oct** / fall 2 oct / off |
| 2 | Time: **~6 s** / ~2.5 s / ~0.75 s | Tone: **brighter** / darker / off |
| 3 | Decay: fast / **slow** / instant | Vocal size: **full** / half / off |

Bold is the factory setting: out of the box it is the full power-up, a
six-second rise of nearly two octaves that brightens and grows as it
builds. That config is global and is stored when you leave the menu, so it
survives a power cycle. A toggle changes it when you move it, so to choose
the position a switch already sits in, flick it away and back.

> **Check for a lit LED before you stomp both.** Engaged, both stomps together
> is charge mode. **Bypassed**, the identical gesture held for about two
> seconds is DFU [Device Firmware Upgrade] mode, which takes the pedal off-line
> until you power cycle it. It counts however they went down, so a save from
> bypass (hold one, press the other) overwrites the held side's slot and
> should be let go of at once.

### Chord mode

Hold both stomps while powering the pedal on and it spends that session as a
single talking vowel filter driven straight off your own guitar, chords
included, instead of the six characters. It opens as you pick harder. There
is no pitch tracker in this path, so it can never make an octave error. Toggle
1 shifts everything you play down or up an octave, and charge can sweep it two
more. At octave 0 there is no delay at all; while shifted, there is a slight
one (about 20 ms) and a little grain, more the further you go.

Both LEDs flash three times to say it is live. Power off and back on without
holding both stomps and the six characters are exactly as you left them.

| Stomp | Does |
|---|---|
| Left tap | Engage/bypass; a tap in the chord menu leaves it |
| Left hold ~1 s | Latch the chord menu (left LED blinks); tap left again to leave |
| Right, held | Open the mouth (right LED lit) |
| Both together | Charge, whose gain, pitch, tone and size settings apply |

While holding right (mouth open), press left as well to start a charge. The
mouth closes while both are down. Let go of left and the charge winds down, and
the mouth opens again if right is still held. In the chord menu, a left tap
with right held does not count, so let go of right before tapping left to leave
the menu.

Two knob layers:

| Knob | Main | Chord menu (hold LEFT) |
|---|---|---|
| 1 | Vocal volume | Closed vowel |
| 2 | Mix (dry to voice) | Open vowel |
| 3 | Master volume | Vocal size |
| 4 | Tone (dark, flat at center, bright) | Resonance (soft to sharp) |
| 5 | Sensitivity (how hard picking opens the mouth) | Attack |
| 6 | Drive | Release |

The vowel knobs each pick one of five vowels: oo, oh, ah, eh, ee. Play softly
and you hear the closed vowel; the harder you pick, the further it moves toward
the open vowel. Sensitivity (knob 5) sets how hard you must pick to reach it.
Hold the right stomp and it goes straight to the open vowel.

Toggle 1 is the octave (up +1, middle 0, down -1), toggle 3 is the gate, same
as the characters; toggle 2 does nothing. Charge's pitch setting sweeps two
octaves up or down from wherever toggle 1 sits, the same way it does for the
characters.

Saving is automatic: a few seconds after you stop adjusting, or the moment you
leave the chord menu. There is nothing to press, and your saved characters are
never touched by it.

### What the LEDs mean

| State | Left (blue) | Right (orange) |
|---|---|---|
| Bypassed | off | off |
| Left slot engaged | solid | off |
| Right slot engaged | off | solid |
| Menu 2 latched | off | blinking |
| Menu 3 latched | blinking | off |
| Save confirmed | 3 blinks | 3 blinks |
| Charging | alternating, speeding up as it builds, slowing as it winds down | |
| Chord mode entry | 3 flashes | 3 flashes |
| Chord mode on | solid | mouth open |
| Chord menu latched | blinking | off |

## Updating the firmware

**<https://withakerik.github.io/db-scream-z/flash.html>**

Plug the pedal's Seed into a computer over USB [Universal Serial Bus], open
that page in Chrome or Edge, and follow it: put the pedal in DFU mode (bypassed,
hold both stomps about two seconds), press Connect, then Flash. It writes the
latest firmware, built automatically from the source in this repo, and the
page shows exactly which commit that is. Your saved voices and settings are
kept.

Firefox and Safari cannot do this: the page needs WebUSB [Web Universal
Serial Bus], which only Chromium browsers have.

## Building one yourself

The board is the [Hothouse](https://clevelandmusicco.com/hothouse-diy-digital-signal-processing-platform-kit/),
open hardware under CC BY-SA [Creative Commons Attribution-ShareAlike]. Gerbers, BOM [bill of materials], placement file
and the drill template are mirrored in [`hardware/`](hardware/). Populate it
with a Daisy Seed3.

One deliberate deviation from the stock kit: the LEDs are blue on the left and
orange on the right, where the kit's bill of materials calls for two reds. The
enclosure artwork is not included as source files; the emulator panel shows a
photograph of the painted faceplate.

Building the firmware needs the Arm GNU [GNU's Not Unix] toolchain (GCC [GNU Compiler Collection] 12 or newer, since
cycfi/q is C++20) plus libDaisy, HothouseExamples and cycfi/q at pinned
commits, which `tools/fetch-deps.sh` clones into `firmware/third_party/`:

```bash
tools/fetch-deps.sh
cd firmware/third_party/libDaisy && make -j
cd ../../hothouse
make GCC_PATH=$HOME/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin
make program-dfu     # Seed3 in DFU mode: hold BOOT, tap RESET
```

The binary the [flash page](https://withakerik.github.io/db-scream-z/flash.html)
writes is built by GitHub Actions from this source, with the same toolchain
and the same pins, on every update to the repo.

[FIRMWARE.md](FIRMWARE.md) is the engineering brief: what the engine does, the
invariants that must not be broken, and the constants behind every voice.

## Credits and licensing

- The firmware under [`firmware/`](firmware/) is licensed GPL-3.0 [GNU
  General Public License, version 3], in [`firmware/LICENSE`](firmware/LICENSE),
  because it links the Hothouse board support from HothouseExamples, which is
  GPL-3.0. No license is offered for anything outside `firmware/`.
- FOF voice synthesis derived from [MonkSynth / Delay Lama](https://github.com/JonET/monksynth), MIT [Massachusetts Institute of Technology] license.
- [libDaisy](https://github.com/electro-smith/libDaisy), MIT license.
- The flash page uses [webdfu](https://github.com/devanlai/webdfu), ISC [Internet Systems Consortium] license.
- Pitch detection by [cycfi/q](https://github.com/cycfi/q), Boost Software
  License 1.0.
- Hardware files under [`hardware/`](hardware/) are from
  [clevelandmusicco/HothouseExamples](https://github.com/clevelandmusicco/HothouseExamples),
  distributed as open source hardware under CC BY-SA 4.0, with the upstream
  license alongside them.

This is a personal, non-commercial project.
