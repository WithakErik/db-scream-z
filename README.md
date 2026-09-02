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

DBscreamZ does one thing: play a note, and a voice screams it back at you. The voice is synthesised, not sampled, using a MonkSynth-style
**FOF** [formant wave function] engine driven by a **Cycfi Q BACF** pitch
tracker running on the live guitar signal. It tracks what you play, so the
scream bends and vibratos with the note.

Four voices ship in the pedal: **Wukong**, **Rice**, **Prince** and
**Piccolo**. Four more (**Boo**, **Fling**, **Ki-Ki** and **Master**) are
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
  and render the result back out as a WAV

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
LED glows solid orange. Tap again to bypass. Tap LEFT for Prince, and the left
LED glows blue. Flip the middle toggle down for Set 2: Rice on the right,
Piccolo on the left.

The LEDs are single-colour and never change: LEFT is always blue, RIGHT always
orange. Only whether they are off, solid or blinking ever changes.

### Knobs

Normally the six knobs are the default layer:

| Knob | Function |
|---|---|
| 1 | Vocal volume |
| 2 | Mix (dry to voice) |
| 3 | Master volume |
| 4 | Tone (dark, flat at centre, bright) |
| 5 | Glide (0-300 ms) |
| 6 | Vocal size (character as written to deepest) |

Hold the RIGHT stomp: about a second in, **menu 2** latches under your foot,
the right LED starts blinking, and the knobs now edit formants F1 and F2. It
does not wait for you to let go, and letting go changes nothing. Hold LEFT
the same way for **menu 3**, F3 and the voice stack. While a menu is latched, tap the
*other* stomp to jump straight to the other menu; tap the blinking side's own
stomp to exit.

Only the blinking LED is lit in a menu. The other goes dark even if that was
the engaged side: the sound keeps playing, its LED just steps aside so one
blinking light is never mistaken for two lit ones.

After any layer change a knob is inert until you move it, so nothing ever
jumps to wherever the knob happens to be pointing.

Menu 3's bottom row is the **voice stack**: how many copies of the scream
sing at once (knob 4, one to eight), how far apart they are tuned (knob 5,
up to 60 cents), and how long each grain of the voice lasts (knob 6, 4 to
40 ms). More voices and wider detune thicken the scream; one voice with no
detune is the bare, focused version of the same character.

Knob 4 steps through the eight counts in eight equal bands, so it always
lands on a whole number. Knob 6 sets texture rather than pitch: short grains
are buzzy and rough, long ones smooth and vocal. It has a detent at 12:00
that reads as exactly 20 ms, the length every voice ships with, so centring
it always returns you to the factory texture.

Back on the default layer, knob 6 is vocal size. It scales all three
formants together, which is acoustically vocal tract length, so the voice
sounds like it is coming from a physically larger creature: same character,
same vowel, much bigger body. All the way down is the character exactly as
written; all the way up is the deepest it goes.

### Toggles

| Toggle | Up | Middle | Down |
|---|---|---|---|
| 1 Octave | +1 | 0 | -1 |
| 2 Memory page | Set 1 | Freeform | Set 2 |
| 3 Gate | high | medium | low |

Freeform makes the stomps engage and bypass whatever the knobs are set to
right now, with no slot involved. While a menu is latched the toggles do
something else entirely: they configure charge mode.

### Saving

Everything you tweak is live but volatile. To save, hold one stomp past a
second and, while **still holding it**, press the other. The held side picks
the slot: hold RIGHT and press LEFT to write the right slot of the current
page. Both LEDs blink three times. That hold latches its menu on the way
past a second, as any hold does; pressing the other stomp drops the menu
again and saves, so you end up back where you started.

Freeform has no slot, so a save there is refused with one short double
flicker: pick a page first. Menus never save; exit the menu first, and your
tweaks survive.

### Charge mode

With a voice **engaged**, stomp both switches together and hold. The sound
charges like a power-up: at a full charge every row you have switched on
reaches the **top of its range**, so the gain is all the way up, the pitch
has swept two octaves, and the voice has grown to its deepest. The
LEDs alternate faster and faster. Release to let it wind down. It is an
overlay, so it never touches the voice you have saved.

Configure it with the toggles while a menu is latched:

| Toggle | Menu 2 latched | Menu 3 latched |
|---|---|---|
| 1 | Gain: big / on / off | Pitch: rise 2 oct / fall 2 oct / off |
| 2 | Time: ~6 s / ~2.5 s / ~0.75 s | Tone: brighter / darker / off |
| 3 | Decay: fast / slow / instant | Vocal size: full / half / off |

That config is global and survives a power cycle.

> **Check for a lit LED before you stomp both.** Engaged, both stomps together
> is charge mode. **Bypassed**, the identical gesture held for about two
> seconds is DFU [Device Firmware Update] mode, which takes the pedal off-line
> until you power cycle it. Press them together, not staggered: holding one
> first is the save gesture.

### What the LEDs mean

| State | Left (blue) | Right (orange) |
|---|---|---|
| Bypassed | off | off |
| Left slot engaged | solid | off |
| Right slot engaged | off | solid |
| Freeform engaged | solid | solid |
| Menu 2 latched | off | blinking |
| Menu 3 latched | blinking | off |
| Save confirmed | 3 blinks | 3 blinks |
| Save refused | double flicker | double flicker |
| Charging | alternating, speeding up | |

## Building one yourself

The board is the [Hothouse](https://clevelandmusicco.com/hothouse-diy-digital-signal-processing-platform-kit/),
open hardware under CC BY-SA. Gerbers, BOM [bill of materials], placement file
and the drill template are mirrored in [`hardware/`](hardware/). Populate it
with a Daisy Seed3.

One deliberate deviation from the stock kit: the LEDs are blue on the left and
orange on the right, where the kit's bill of materials calls for two reds. The
enclosure artwork is not included as source files; the emulator panel shows a
photograph of the painted faceplate.

Building the firmware needs the Arm GNU toolchain (GCC 12 or newer, since
cycfi/q is C++20) plus clones of libDaisy and HothouseExamples under
`firmware/third_party/`:

```bash
cd firmware/third_party/libDaisy && make -j
cd ../../hothouse
make GCC_PATH=$HOME/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin
make program-dfu     # Seed3 in DFU mode: hold BOOT, tap RESET
```

The [Daisy Web Programmer](https://electro-smith.github.io/Programmer/) flashes
a prebuilt binary with nothing to install.

[FIRMWARE.md](FIRMWARE.md) is the engineering brief: what the engine does, the
invariants that must not be broken, and the constants behind every voice.

## Credits and licensing

- FOF voice synthesis derived from [MonkSynth / Delay Lama](https://github.com/JonET/monksynth).
- Pitch detection by [cycfi/q](https://github.com/cycfi/q), Boost Software
  License 1.0.
- Hardware files under [`hardware/`](hardware/) are from
  [clevelandmusicco/HothouseExamples](https://github.com/clevelandmusicco/HothouseExamples),
  distributed as open source hardware under CC BY-SA 4.0, with the upstream
  licence alongside them.

This is a personal, non-commercial project.
