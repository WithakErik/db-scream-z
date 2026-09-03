#!/usr/bin/env python3
"""Generate docs/BOOKLET.md: per-character knob-position cards for the
milestone 5 control surface. Regenerate after any change to
firmware/engine/presets.hpp or the knob ranges in
firmware/hothouse/param_map.hpp (the ranges below mirror that header).

Values come from presets.hpp (BAKED formants, tract scale applied), not
presets.json (pre-bake), because the knobs must reproduce the baked values.

House style for the booklet prose below (applies to any manual copy
derived from it): spell every initialism out in SQUARE BRACKETS at its
first use, e.g. "DFU [Device Firmware Upgrade]", "LED [light-emitting
diode]". Later uses may go bare. Also state LED colours as blue LEFT /
orange RIGHT; they are a build choice, not firmware.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRESETS = ROOT / "firmware" / "engine" / "presets.hpp"
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
         "Rice": "Set 2, RIGHT", "Piccolo": "Set 2, LEFT"}

# Booklet-only characters: not factory slots, dial by hand and save
# wherever you like. Same keys as a parsed preset (f1/f2/f3/detune_cents)
# plus an optional "note" line shown on the card.
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
    # Ki-Ki: shrill piercing female, everything pushed high. Narrowest
    # detune in the booklet so nothing blunts the point of it.
    "Ki-Ki": {"f1": 950.0, "f2": 1800.0, "f3": 3350.0,
              "detune_cents": 8.0},
    # Master: old raspy male, small dark tract. The widest detune of the
    # eight, for the frayed edge of an elderly voice.
    "Master": {"f1": 640.0, "f2": 1080.0, "f3": 2400.0,
               "detune_cents": 30.0},
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
Tap LEFT for Prince (left LED, blue). Flip the middle toggle down for
Set 2 (Rice right, Piccolo left).

The two LEDs are single-colour: the LEFT one is always blue, the RIGHT
one always orange. Nothing on the pedal ever changes an LED's colour;
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

Hold the RIGHT stomp: about 1 second in, MENU 2 latches under your foot
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
and how far it swings (knob 5, 0 to 100 cents), plus detune (knob 6, up
to 60 cents), which spreads the engine's three voices apart. Every
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
| 2 Memory page | Set 1 | Freeform | Set 2 |
| 3 Gate | high | medium | low |

Freeform (toggle 2 middle) makes the stomps engage/bypass whatever the
knobs are currently set to, no slot involved. While a menu is latched
the toggles do something different entirely: see Charge mode below.

### Saving a sound

Everything you tweak is live but volatile. To SAVE: hold one stomp past
1 second and, while STILL holding it, press the other. The held side
picks the slot (hold RIGHT + press LEFT = the R slot of the current
page). That hold latches its menu on the way past 1 second, as any hold
does; the second press drops the menu again and saves, leaving you
where you started. Both LEDs blink 3 times. On Freeform there is no slot,
so the LEDs give one short double-flicker instead: pick a page first.
Menus never save; exit the menu first (your tweaks stay).

### CHARGE MODE (the power-up)

Charge mode ONLY happens while a voice is ENGAGED, meaning one or both
LEDs are lit, and no menu is latched. Check for a lit LED before you
stomp: the same two-stomp hold done while BYPASSED is the firmware
update gesture instead, and it will take the pedal off-line mid-song.

With a voice engaged, then, stomp BOTH switches together and hold: the
sound charges up like a power-up scream: at a full charge every row you
have switched on reaches the TOP of its range, so the gain is all the
way up, the pitch has swept two octaves, and the voice has grown to its
deepest. The LEDs alternate faster and faster. Release
to let it wind down. Configure it with the toggles while a menu is
latched:

| Toggle | Menu 2 latched | Menu 3 latched |
|---|---|---|
| 1 | Gain: Above 9000! / on / off | Pitch: rise 2 oct / fall 2 oct / off |
| 2 | Charge time: Birit Spomb ~6 s / Hamekameka ~2.5 s / punch ~0.75 s | Tone: brighter / darker / off |
| 3 | Decay: fast / slow / off (instant) | Size: full / half / off |

On the amount rows (gain and size) the middle position gets you
HALFWAY from wherever the voice already sits to the top, and the up
position takes it all the way. Tone's two positions are directions
rather than amounts, so both go the whole way: darker means fully dark,
brighter means fully bright.

The config is global, remembered across power cycles, and never touches
your saved voices.

### LED language

| State | Left LED (blue) | Right LED (orange) |
|---|---|---|
| Bypassed | off | off |
| R slot engaged | off | solid |
| L slot engaged | solid | off |
| Freeform engaged | solid | solid |
| Menu 2 latched | off | blinking |
| Menu 3 latched | blinking | off |
| Save confirmed | 3 blinks | 3 blinks |
| Save rejected (Freeform) | double-flicker | double-flicker |
| Charging | alternating, speeding up with the charge | |

### Firmware update mode

While BYPASSED, meaning both LEDs are off, press both stomps together
and hold about 2 seconds to enter USB [Universal Serial Bus]
firmware-update mode, also called DFU [Device Firmware Upgrade] mode.
The pedal makes no sound in this mode and waits for a computer. Power
cycle it to go back to playing.

Being bypassed is what arms this gesture. Engaged, the identical
two-stomp hold is CHARGE MODE and can never reach DFU. Press the two
stomps together, not staggered: holding one for a second first is the
save gesture.
"""


def clock(frac):
    """Knob travel fraction (0..1) to a 7 o'clock..5 o'clock position."""
    total_min = frac * 600  # 10 clock-hours of travel
    h = 7 + int(total_min // 60)
    m = int(total_min % 60)
    if h > 12:
        h -= 12
    return f"{h}:{m:02d}"


def parse_presets():
    text = PRESETS.read_text()
    rx = re.compile(r'\{"(\w+)",\s*\{([\d.]+)f,\s*([\d.]+)f,\s*([\d.]+)f\},'
                    r'\s*([\d.]+)f\}')
    out = {}
    for m in rx.finditer(text):
        name, f1, f2, f3, dt = m.groups()
        out[name] = {"f1": float(f1), "f2": float(f2), "f3": float(f3),
                     "detune_cents": float(dt)}
    assert len(out) == 4, f"expected 4 presets, parsed {len(out)}"
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


# The beast bank hint. Deliberately says WHAT and not HOW: the gesture is
# documented in FIRMWARE.md and HANDOFF.md, which owners do not read. See
# firmware/hothouse/beast_presets.hpp.
HINT = """## One more thing

The pedal knows four voices that are not in this booklet, and they are
not people. They are already in there. Nothing you can do from the front
panel will find them by accident, and nothing they do can overwrite the
four characters above.

Someone will have to tell you.
"""


def main():
    presets = parse_presets()
    order = ["Wukong", "Rice", "Prince", "Piccolo"]
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
    parts += [HINT]
    OUT.write_text("\n".join(parts))
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
