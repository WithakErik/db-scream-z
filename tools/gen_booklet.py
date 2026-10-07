#!/usr/bin/env python3
"""Generate docs/BOOKLET.md: per-character knob-position cards for the
milestone 5 control surface, plus chord mode's own card. Regenerate after
any change to firmware/engine/presets.hpp, the knob ranges in
firmware/hothouse/param_map.hpp (the ranges below mirror that header), or
chord mode's chord_ui.hpp / chord_map.hpp / voice_params.hpp factory_chord().

Values come from presets.hpp (BAKED formants, tract scale applied), not
presets.json (pre-bake), because the knobs must reproduce the baked values.

House style for the booklet prose below (applies to any manual copy
derived from it): spell every initialism out in SQUARE BRACKETS at its
first use, e.g. "DFU [Device Firmware Upgrade]", "LED [light-emitting
diode]". Later uses may go bare. Also state LED colors as blue LEFT /
orange RIGHT; they are a build choice, not firmware.
"""
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRESETS = ROOT / "firmware" / "engine" / "presets.hpp"
VOICE_PARAMS = ROOT / "firmware" / "hothouse" / "voice_params.hpp"
OUT = ROOT / "docs" / "BOOKLET.md"

# Knob ranges: MUST mirror firmware/hothouse/param_map.hpp.
MENU1 = [("Vocal volume", "vocal_vol"), ("Mix", "mix"),
         ("Master volume", "master_vol"), ("Tone", "tone"),
         ("Glide ms", "glide_ms"), ("Vocal size", "vocal_size")]
MENU2 = [("F1 Hz", "f1", 200, 1400), ("F1 bandwidth", "bw1", 5, 300),
         ("F1 amount", "a1", 0, 2), ("F2 Hz", "f2", 500, 2600),
         ("F2 bandwidth", "bw2", 5, 400), ("F2 amount", "a2", 0, 2)]
MENU3 = [("F3 Hz", "f3", 1500, 4500), ("F3 bandwidth", "bw3", 5, 500),
         ("F3 amount", "a3", 0, 2),
         ("Vibrato rate Hz", "vib_rate_hz", 0, 50),
         ("Vibrato depth cents", "vib_depth_cents", 0, 100),
         ("Detune cents", "detune_cents", 0, 60)]
V12 = {"bw1": 32.5, "bw2": 47.5, "bw3": 62.5,
       "a1": 1.0, "a2": 1.0, "a3": 1.0,
       # Vibrato ships off for every character, so every card prints both
       # rows at the fully counter-clockwise stop.
       "vib_rate_hz": 0.0, "vib_depth_cents": 0.0}
# Menu 1 + toggle factory values (firmware/hothouse/voice_params.hpp
# factory_voice(): every character ships with these unless its card
# overrides them).
V12_MENU1 = {"mix": 1.0, "glide_ms": 0.0, "master_vol": 1.0,
             "vocal_vol": 1.0, "vocal_size": 0.0, "tone": 0.0,
             "octave": 0, "gate": "medium"}
OCTAVE_POS = {1: "Up (+1)", 0: "Middle (0)", -1: "Down (-1)"}
GATE_POS = {"high": "Up", "medium": "Middle", "low": "Down"}


def menu1_frac(key, v):
    """Inverse of the menu 1 knob tapers in param_map.hpp: glide is
    cube-tapered, tone has a +/-10% center deadzone, the rest are linear."""
    if key == "mix" or key == "vocal_size":
        return v
    if key == "glide_ms":
        return (v / 300.0) ** (1.0 / 3.0) if v > 0 else 0.0
    if key in ("master_vol", "vocal_vol"):
        return v / 2.0
    if key == "tone":
        if v == 0.0:
            return 0.5
        dz = 0.1
        x = (1.0 if v > 0 else -1.0) * (abs(v) * (1.0 - dz) + dz)
        return x / 2.0 + 0.5
    raise KeyError(key)
SLOTS = {"Wukong": "Set 1, RIGHT", "Prince": "Set 1, LEFT",
         "Rice": "Set 2, RIGHT", "Flute": "Set 2, LEFT",
         "Master": "Set 3, RIGHT", "Ki-Ki": "Set 3, LEFT"}

# Booklet-only characters: Boo and Fling, not factory slots, dial by hand
# and save wherever you like. Same keys as a parsed preset
# (f1/f2/f3/detune_cents) plus an optional "note" line shown on the card.
#
# Master and Ki-Ki were promoted to factory Set 3 on 2026-09-25
# (tools/gen_presets.py PEDAL_PRESETS).
#
# Detune is all that is left of the bottom row's voice stack: thick, frayed
# voices take a wider spread, thin and piercing ones take a tighter one.
# Voice COUNT was retired on 2026-09-01 (every voice runs the engine's 3)
# and grain length went with it (every card was already at the engine's 20
# ms). The two knobs they freed are vibrato rate and depth, which ship at 0
# for all eight and are the obvious place to re-voice these by ear.
#
# Aspiration used to be this row's third dimension and gave Boo and Master
# their "breath", but it was removed on 2026-09-01 after its two 4-5 kHz
# sinusoids turned out to be the static heard on hardware. All eight are
# designed on paper from the character's vocal quality, not measured.
EXTRA = {
    # Ear-picked 2026-08-27 (candidate B of three LPC-derived options)
    # from the Sparking! ZERO Fat Boo voice clips (Josh Martin):
    # F1/F2/F3 medians pushed bright. Thick and soft: a big loose body.
    "Boo": {"f1": 900.0, "f2": 1750.0, "f3": 2900.0,
            "detune_cents": 24.0},
    # Designed by ear 2026-08-28 (no reference clips in the repo, unlike
    # Boo): values chosen from the character's vocal quality, tunable in
    # the lab. Fling = the brash blonde persona: hard bright female, a
    # modest detune so the edge stays on the formants.
    "Fling": {"f1": 880.0, "f2": 1600.0, "f3": 3050.0,
              "detune_cents": 14.0},
}

# Hand-maintained usage section. MUST mirror firmware/MILESTONE5.md
# sections 2 and 8 (the on-device runbook is the authority); update both
# together when the control surface changes.
USAGE = """\
## How to use the pedal

### Quick start

Plug in, power on. The pedal boots BYPASSED, both LEDs [light-emitting
diodes] off. Flip the middle toggle up (Set 1) and tap the RIGHT stomp:
Wukong engages, the right LED glows solid orange. Tap again to bypass.
Tap LEFT for Prince (left LED, blue). Flip the middle toggle to the
middle for Set 2 (Rice right, Flute left), or down for Set 3 (Master
right, Ki-Ki left).

The two LEDs are single-color: the LEFT one is always blue, the RIGHT
one always orange. Nothing on the pedal ever changes an LED's color;
only whether it is off, solid, or blinking.

### Knobs (three layers)

Normally the six knobs are the DEFAULT layer:

| Knob | Function |
|---|---|
| 1 | Vocal volume |
| 2 | Mix (dry to voice) |
| 3 | Master volume |
| 4 | Tone (dark - flat at center - bright) |
| 5 | Glide (0-300 ms) |
| 6 | Vocal size (character as written - deepest) |

Knob 6, vocal size, scales all three formants together, which is
acoustically vocal tract length: the same character, the same vowel,
coming from a physically bigger body. All the way down (7:00) is the
character exactly as written; all the way up (5:00) is the deepest it
goes.

Hold the RIGHT stomp: about 1 second in, MENU 2 latches
(right LED blinks) and the knobs edit formants F1/F2. It does not wait
for the release, and letting go changes nothing. Hold LEFT the same way
for MENU 3 (F3 + vibrato). While a menu is latched ONLY the blinking
LED is lit: the other goes dark even if that was the engaged side. The
sound keeps playing, its LED just steps aside so one blinking light is
never mistaken for two lit ones. Tap the OTHER stomp to jump straight
to the other menu; tap the blinking side's own stomp to exit. After
any menu change a knob is inert until you move it, so nothing ever
jumps.

Menu 3's bottom row is VIBRATO: how fast it cycles (knob 4, 0 to 50 Hz)
and how far it swings (knob 5, 0 to 100 cents), plus detune (knob 6),
which spreads the engine's three voices apart: the outer two move up to
60 cents either side of the middle one. Every
character ships with vibrato off; wider detune thickens and roughens
the scream, while at 0 the three voices collapse to the bare, focused
character.

Knob 4's travel is cube-tapered rather than straight-line: 6.25 Hz sits
at 12:00, so the musical singer's-vibrato range does not get crammed
into the bottom sixth of the travel.

### Toggles (left to right)

| Toggle | Up | Middle | Down |
|---|---|---|---|
| 1 Octave | +1 | 0 | -1 |
| 2 Memory page | Set 1 | Set 2 | Set 3 |
| 3 Gate | high | medium | low |

Each page holds two voices, one per stomp, so six in all. While a menu
is latched the toggles do something different entirely: see Charge mode
below.

### Saving a sound

Everything you tweak is live but volatile. To SAVE: hold one stomp past
1 second and, while STILL holding it, press the other. The held side
picks the slot (hold RIGHT + press LEFT = the R slot of the current
page). That hold latches its menu on the way past 1 second, as any hold
does; the second press drops the menu again and saves, leaving you
where you started. Both LEDs blink 3 times. Menus never save; exit the
menu first (your tweaks stay).

### CHARGE MODE (the power-up)

Charge mode ONLY happens while a voice is ENGAGED, meaning one of the
LEDs is lit, and no menu is latched. Check for a lit LED before you
stomp: the same two-stomp hold done while BYPASSED is the firmware
update gesture instead, and it will take the pedal off-line mid-song.

With a voice engaged, then, stomp BOTH switches together and hold: the
sound charges up like a power-up scream: at a full charge every row you
have switched on goes as far as its position takes it (see below). The
LEDs alternate faster and faster. Release to let it wind down.
Configure it with the toggles while a menu is latched:

| Toggle | Menu 2 latched | Menu 3 latched |
|---|---|---|
| 1 | Gain: Above 9000! / **on** / off | Pitch: **rise 2 oct** / fall 2 oct / off |
| 2 | Charge time: **Birit Spomb ~6 s** / Hamekameka ~2.5 s / punch ~0.75 s | Tone: **brighter** / darker / off |
| 3 | Decay: fast / **slow** / off (instant) | Size: **full** / half / off |

Bold is the factory setting (the full power-up: a six-second rise of
nearly two octaves that brightens and grows as it builds).

On the amount rows (gain and size) the middle position gets you
HALFWAY from wherever the voice already sits to the top, and the up
position takes it all the way. Tone's two positions are directions
rather than amounts, so both go the whole way: darker means fully dark,
brighter means fully bright.

Pitch glides toward two octaves away for as long as you hold: nearly
there by the end of the slowest charge (Birit Spomb, ~6 s), about one
octave in by the end of the quickest (punch, ~0.75 s).
The two octaves count from wherever the octave toggle sits, so a voice
whose toggle is already up rises three octaves above normal.

A toggle sets charge when you MOVE it, so to pick the position it already
sits in, flick it away and back. Your Set, octave and gate stay as they
were; the switches stop showing them until you next move them with no
menu latched.

The config is global and never touches your saved voices. It is stored
when you leave the menu by tapping the blinking side; power off with a
menu still latched and the change is lost.

### LED language

| State | Left LED (blue) | Right LED (orange) |
|---|---|---|
| Bypassed | off | off |
| R slot engaged | off | solid |
| L slot engaged | solid | off |
| Menu 2 latched | off | blinking |
| Menu 3 latched | blinking | off |
| Save confirmed | 3 blinks | 3 blinks |
| Charging | alternating, speeding up as it builds, slowing as it winds down | |

### Firmware update mode

While BYPASSED, meaning both LEDs are off, press both stomps together
and hold about 2 seconds to enter USB [Universal Serial Bus]
firmware-update mode, also called DFU [Device Firmware Upgrade] mode.
The pedal makes no sound in this mode and waits for a computer. To
update it, open withakerik.github.io/db-scream-z/flash.html in Chrome or
Edge and follow the page. Power cycle it to go back to playing.

Being bypassed is what arms this gesture. Engaged, the identical
two-stomp hold is CHARGE MODE and can never reach DFU. Bypassed, any
two seconds with both stomps down enters DFU, even if one went down
before the other. A save from bypass (hold one, press the other)
happens the moment the second stomp goes down, so release both right
away, or engage a voice before saving.
"""


def clock(frac):
    """Knob travel fraction (0..1) to a 7 o'clock..5 o'clock position."""
    # Rounded to the nearest minute, not truncated: truncation printed
    # 8:49 for an exact 8:50 (11 ct detune) through float error.
    total_min = round(frac * 600)  # 10 clock-hours of travel
    h = 7 + total_min // 60
    m = total_min % 60
    if h > 12:
        h -= 12
    return f"{h}:{m:02d}"


def parse_presets():
    text = PRESETS.read_text()
    rx = re.compile(r'\{"([\w-]+)",\s*\{([\d.]+)f,\s*([\d.]+)f,\s*([\d.]+)f\},'
                    r'\s*([\d.]+)f\}')
    out = {}
    for m in rx.finditer(text):
        name, f1, f2, f3, dt = m.groups()
        out[name] = {"f1": float(f1), "f2": float(f2), "f3": float(f3),
                     "detune_cents": float(dt)}
    assert list(out) == ["Wukong", "Rice", "Prince", "Flute", "Master",
                         "Ki-Ki"], f"unexpected presets parsed: {list(out)}"
    return out


def frow(label, value, frac):
    assert -0.001 <= frac <= 1.001, f"{label}: {value} maps outside the knob"
    frac = min(1.0, max(0.0, frac))
    return f"| {label} | {value:g} | {frac * 100:.0f}% | {clock(frac)} |"


def row(label, value, lo, hi):
    return frow(label, value, (value - lo) / (hi - lo))


# Vibrato rate is CUBE-tapered over 0..50 Hz (map_cube() in param_map.hpp),
# so a linear inverse would put every value in the wrong place on the dial.
# The taper exists because 4..8 Hz singer's vibrato would otherwise sit in
# the bottom sixth of the travel, unsettable on a real pot.
def vib_rate_frac(v):
    return (max(0.0, min(50.0, v)) / 50.0) ** (1.0 / 3.0)


# Chord mode's own tapers (firmware/hothouse/chord_map.hpp
# apply_chord_knob()), the same class of fix as vib_rate_frac() above.
# Most chord knobs are the plain map_lin() row() already inverts
# correctly (vocal vol, mix, master vol, both vowels) or are the knob
# position itself (resonance) or near enough to identity that the
# 32-step quantizer in map_vocal_size() rounds away (vocal size). Three
# are map_cube()'s cube taper (sensitivity, attack, release) and one is
# map_drive()'s log taper (drive); tone reuses map_tone(), which
# menu1_frac() above already inverts for the normal-mode tone knob.
def cube_frac(v, lo, hi):
    """Exact inverse of map_cube(t, lo, hi) = lo + (hi - lo) * t**3."""
    t = (v - lo) / (hi - lo)
    return max(0.0, min(1.0, t)) ** (1.0 / 3.0)


def drive_frac(v):
    """Exact inverse of map_drive(t) = pow(40.0, t) (chord_map.hpp)."""
    return math.log(v) / math.log(40.0)


def chord_frac(key, v, lo, hi):
    if key == "drive":
        return drive_frac(v)
    if key in ("sensitivity", "attack_ms", "release_ms"):
        return cube_frac(v, lo, hi)
    if key == "tone":
        return menu1_frac("tone", v)
    return (v - lo) / (hi - lo)


def card(name, p):
    slot = SLOTS.get(name)
    where = (f"Factory slot: {slot}." if slot
             else "No factory slot: dial it in, save it anywhere.")
    vals = dict(V12_MENU1)
    vals.update(V12)
    vals.update(p)
    lines = [f"## {name}", "",
             f"{where} To dial from scratch: latch the",
             "menu, turn each knob to the position below (knobs are inert",
             "until moved), then save to any slot.", ""]
    if p.get("note"):
        lines += [p["note"], ""]
    lines += ["### Menu 1 (default layer): knobs 1-6",
              "", "| Param | Value | Travel | Clock |", "|---|---|---|---|"]
    for label, key in MENU1:
        lines.append(frow(label, vals[key], menu1_frac(key, vals[key])))
    lines += ["", "### Menu 2 (hold RIGHT stomp): knobs 1-6",
              "", "| Param | Value | Travel | Clock |", "|---|---|---|---|"]
    for label, key, lo, hi in MENU2:
        lines.append(row(label, vals[key], lo, hi))
    lines += ["", "### Menu 3 (hold LEFT stomp): knobs 1-6",
              "", "| Param | Value | Travel | Clock |", "|---|---|---|---|"]
    for label, key, lo, hi in MENU3:
        if key == "vib_rate_hz":
            lines.append(frow(label, vals[key], vib_rate_frac(vals[key])))
        else:
            lines.append(row(label, vals[key], lo, hi))
    lines += ["",
              f"Toggles: 1 Octave = {OCTAVE_POS[vals['octave']]}, "
              f"3 Gate = {GATE_POS[vals['gate']]} ({vals['gate']}).", ""]
    return "\n".join(lines)


# Chord mode (firmware/hothouse/chord_ui.hpp, chord_map.hpp). Knob ranges
# MUST mirror the ChordParams field comments in voice_params.hpp.
CHORD_MAIN = [("Vocal vol", "vocal_vol", 0, 2), ("Mix", "mix", 0, 1),
              ("Master vol", "master_vol", 0, 2), ("Tone", "tone", -1, 1),
              ("Sensitivity", "sensitivity", 0, 8), ("Drive x", "drive", 1, 40)]
CHORD_MENU = [("Closed vowel (0 oo .. 4 ee)", "closed_vowel", 0, 4),
              ("Open vowel (0 oo .. 4 ee)", "open_vowel", 0, 4),
              ("Vocal size", "vocal_size", 0, 1),
              ("Resonance", "resonance", 0, 1),
              ("Attack ms", "attack_ms", 1, 50),
              ("Release ms", "release_ms", 20, 500)]

# Chord mode's one factory setting (firmware/hothouse/voice_params.hpp
# factory_chord()). Mix, tone, vocal_size and gate are copied there from
# factory_voice(0), which is
# already mirrored above as V12_MENU1, so they are pulled from there
# instead of duplicated. check_factory_chord() below asserts the rest
# (chord mode's own literal values) against a regex pull of
# factory_chord()'s body, the same drift guard parse_presets() gives the
# character presets.
FACTORY_CHORD = {
    "vocal_vol": 2.0,
    "mix": V12_MENU1["mix"],
    "master_vol": 1.2,
    "tone": V12_MENU1["tone"],
    "sensitivity": 1.6,
    "drive": 40.0,
    "closed_vowel": 0.0,
    "open_vowel": 4.0,
    "vocal_size": V12_MENU1["vocal_size"],
    "resonance": 0.53,
    "attack_ms": 17.0,
    "release_ms": 20.0,
    "gate": V12_MENU1["gate"],
}


def check_factory_chord():
    """Parses factory_chord()'s own literal `c.field = N.Nf;` assignments
    out of voice_params.hpp and asserts they match FACTORY_CHORD, so a
    tuning change there cannot silently drift from this booklet. The
    fields factory_chord() copies from `v` (mix, tone, vocal_size,
    gate) have no literal here to check against; they
    are kept in step by hand via V12_MENU1 above instead."""
    text = VOICE_PARAMS.read_text()
    m = re.search(r'inline ChordParams factory_chord\(\)\s*\{(.*?)\n\}',
                  text, re.S)
    assert m, "factory_chord() not found in voice_params.hpp"
    rx = re.compile(r'c\.(\w+)\s*=\s*([\d.]+)f;')
    literal = {name: float(val) for name, val in rx.findall(m.group(1))}
    assert literal, "no literal c.field = N.Nf; assignments found to check"
    for key, val in literal.items():
        assert key in FACTORY_CHORD, (
            f"factory_chord() sets {key}, not mirrored in gen_booklet.py")
        assert FACTORY_CHORD[key] == val, (
            f"factory_chord() {key}={val} but gen_booklet.py FACTORY_CHORD "
            f"has {FACTORY_CHORD[key]}: update the mirror")


def chord_section():
    lines = [
        "## Chord mode", "",
        "Hold both footswitches down and keep holding as you power the",
        "pedal on: for that session it replaces the six characters with",
        "one vowel filter, chords included, driven straight off your",
        "guitar with no pitch tracker anywhere in the path. Both LEDs",
        "flash three times to say it is live. Power off and back on",
        "WITHOUT holding the footswitches and the six characters are",
        "exactly as you left them.", "",
        "| Stomp | Does |", "|---|---|",
        "| LEFT tap | Engage or bypass. In the chord menu, leaves it "
        "instead. |",
        "| LEFT hold ~1 s | Latch the chord menu (left LED blinks). Tap "
        "LEFT again to leave. |",
        "| RIGHT, held | Open the mouth: right LED lit for as long as "
        "you hold it (dark in the chord menu). Momentary, never a menu, "
        "never a save. |",
        "| Both together | Charge, engaged and outside the menu only: "
        "the same gain, tone and size overlay as the characters. |", "",
        "While holding RIGHT (mouth open), press LEFT as well to start a",
        "charge. The mouth closes while both are down. Let go of LEFT and",
        "the charge winds down, and the mouth opens again if RIGHT is",
        "still held. In the chord menu, a LEFT tap with RIGHT held does",
        "not count, so let go of RIGHT before tapping LEFT to leave.", "",
        "Toggle 3 is the gate (high/medium/low), same as the characters;",
        "toggles 1 and 2 do nothing. Chord mode has one setting, not",
        "six slots, and it saves itself: a few seconds after you stop",
        "turning a knob, or at once when you leave the chord menu, and",
        "only when something actually changed. There is nothing to",
        "press, and your saved characters are never touched by it.", "",
        "### Main layer (default): knobs 1-6", "",
        "| Param | Value | Travel | Clock |", "|---|---|---|---|",
    ]
    for label, key, lo, hi in CHORD_MAIN:
        v = FACTORY_CHORD[key]
        lines.append(frow(label, v, chord_frac(key, v, lo, hi)))
    lines += [
        "", "### Chord menu (hold LEFT stomp): knobs 1-6", "",
        "| Param | Value | Travel | Clock |", "|---|---|---|---|",
    ]
    for label, key, lo, hi in CHORD_MENU:
        v = FACTORY_CHORD[key]
        lines.append(frow(label, v, chord_frac(key, v, lo, hi)))
    lines += [
        "",
        "Vowel knobs each pick one of oo, oh, ah, eh, ee (0 to 4). Play",
        "softly and you hear the closed vowel; the harder you pick, the",
        "further it moves toward the open vowel. Sensitivity (K5) sets how",
        "hard you must pick to reach it. Hold RIGHT and it goes straight",
        "to the open vowel.", "",
        f"Toggles: 3 Gate = {GATE_POS[FACTORY_CHORD['gate']]} "
        f"({FACTORY_CHORD['gate']}).", "",
    ]
    return "\n".join(lines)


CHORD = chord_section()


def main():
    check_factory_chord()
    presets = parse_presets()
    order = ["Wukong", "Prince", "Rice", "Flute", "Master", "Ki-Ki"]
    parts = ["# DBscreamZ: Instruction Booklet",
             "",
             "GENERATED by tools/gen_booklet.py from firmware/engine/",
             "presets.hpp and the milestone 5 + charge-mode control map.",
             "Do not edit by hand; regenerate with: python3 tools/gen_booklet.py",
             "",
             "Knob travel: 7:00 = fully counter-clockwise, 12:00 = center,",
             "5:00 = fully clockwise.", "",
             USAGE,
             "# Character settings cards", ""]
    parts += [card(n, presets[n]) for n in order]
    parts += [card(n, p) for n, p in EXTRA.items()]
    parts += [CHORD]
    OUT.write_text("\n".join(parts))
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
