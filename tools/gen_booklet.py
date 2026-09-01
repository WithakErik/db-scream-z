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
MENU1 = [("Mix", "mix"), ("Glide ms", "glide_ms"),
         ("Master volume", "master_vol"), ("Vocal volume", "vocal_vol"),
         ("Drive", "drive"), ("Tone", "tone")]
MENU2 = [("F1 Hz", "f1", 200, 1400), ("F1 bandwidth", "bw1", 5, 300),
         ("F1 amount", "a1", 0, 2), ("F2 Hz", "f2", 500, 2600),
         ("F2 bandwidth", "bw2", 5, 400), ("F2 amount", "a2", 0, 2)]
MENU3 = [("F3 Hz", "f3", 1500, 4500), ("F3 bandwidth", "bw3", 5, 500),
         ("F3 amount", "a3", 0, 2), ("Voices", "unison", 1, 8),
         ("Detune cents", "detune_cents", 0, 60),
         ("Aspiration", "aspiration", 0, 1)]
V12 = {"bw1": 32.5, "bw2": 47.5, "bw3": 62.5,
       "a1": 1.0, "a2": 1.0, "a3": 1.0}
# Menu 1 + toggle factory values (firmware/hothouse/voice_params.hpp
# factory_voice(): every character ships with these unless its card
# overrides them).
V12_MENU1 = {"mix": 1.0, "glide_ms": 0.0, "master_vol": 1.0,
             "vocal_vol": 1.0, "drive": 0.0, "tone": 0.0,
             "octave": 0, "gate": "medium"}
OCTAVE_POS = {1: "Up (+1)", 0: "Middle (0)", -1: "Down (-1)"}
GATE_POS = {"high": "Up", "medium": "Middle", "low": "Down"}


def menu1_frac(key, v):
    """Inverse of the menu 1 knob tapers in param_map.hpp: glide is
    cube-tapered, tone has a +/-10% center deadzone, the rest are linear."""
    if key == "mix" or key == "drive":
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
# wherever you like. Same keys as a parsed preset (f1/f2/f3/unison/
# detune_cents/aspiration) plus an optional "note" line shown on the card.
#
# The unison stack replaced the vibrato these characters used to be voiced
# with, so the bottom row of menu 3 carries them now: thick and rough voices
# take more stacked voices, wider detune and audible breath; thin and
# piercing ones take fewer, tighter, drier. All eight are designed on paper
# from the character's vocal quality, not measured, and are meant to be
# re-tuned by ear on hardware.
EXTRA = {
    # Ear-picked 2026-08-27 (candidate B of three LPC-derived options)
    # from the Sparking! ZERO Fat Boo voice clips (Josh Martin):
    # F1/F2/F3 medians pushed bright. Thick and breathy: a big soft body.
    "Boo": {"f1": 900.0, "f2": 1750.0, "f3": 2900.0,
            "unison": 5, "detune_cents": 24.0, "aspiration": 0.35},
    # Designed by ear 2026-08-28 (no reference clips in the repo, unlike
    # Boo): values chosen from the character's vocal quality, tunable in
    # the lab. Fling = the brash blonde persona: hard bright female, a
    # modest stack so the edge stays on the formants.
    "Fling": {"f1": 880.0, "f2": 1600.0, "f3": 3050.0,
              "unison": 3, "detune_cents": 14.0, "aspiration": 0.12},
    # Ki-Ki: shrill piercing female, everything pushed high. Thinnest
    # stack in the booklet so nothing blunts the point of it.
    "Ki-Ki": {"f1": 950.0, "f2": 1800.0, "f3": 3350.0,
              "unison": 2, "detune_cents": 8.0, "aspiration": 0.10},
    # Master: old raspy male, small dark tract. The most breath of the
    # eight, wide detune for the frayed edge of an elderly voice.
    "Master": {"f1": 640.0, "f2": 1080.0, "f3": 2400.0,
               "unison": 4, "detune_cents": 30.0, "aspiration": 0.45},
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
| 1 | Mix (dry to voice) |
| 2 | Glide (0-300 ms) |
| 3 | Master volume |
| 4 | Vocal volume |
| 5 | Drive (clean to saturated) |
| 6 | Tone (dark - flat at center - bright) |

Hold the RIGHT stomp: about 1 second in, MENU 2 latches under your foot
(right LED blinks) and the knobs edit formants F1/F2. It does not wait
for the release, and letting go changes nothing. Hold LEFT the same way
for MENU 3 (F3 + the voice stack). While a menu is latched ONLY the blinking
LED is lit: the other goes dark even if that was the engaged side. The
sound keeps playing, its LED just steps aside so one blinking light is
never mistaken for two lit ones. Tap the OTHER stomp to jump straight
to the other menu; tap the blinking side's own stomp to exit. After
any menu change a knob is inert until you move it, so nothing ever
jumps.

Menu 3's bottom row is the VOICE STACK: how many copies of the voice
sing at once (knob 4, one to eight), how far apart they are tuned
(knob 5, up to 60 cents) and how much breath rides on top (knob 6).
More voices and wider detune thicken the scream; one voice with no
detune is the bare, focused version of the same character.

Knob 4 steps: the travel is eight equal bands, one per voice count, so
it lands on a whole number wherever you leave it. Turning knob 6 fully
counter-clockwise switches the aspiration OFF: the last sliver of
travel before 7:00 is a detent that reads as a hard zero, so the voice
path goes back to having no breath in it at all rather than a residual
trickle.

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
way up, the pitch has swept two octaves, and the breath has torn right
into the voice. The LEDs alternate faster and faster. Release
to let it wind down. Configure it with the toggles while a menu is
latched:

| Toggle | Menu 2 latched | Menu 3 latched |
|---|---|---|
| 1 | Gain: Above 9000! / on / off | Pitch: rise 2 oct / fall 2 oct / off |
| 2 | Charge time: Birit Spomb ~6 s / Hamekameka ~2.5 s / punch ~0.75 s | Tone: brighter / darker / off |
| 3 | Decay: fast / slow / off (instant) | Aspiration: high / low / off |

On the amount rows (gain and aspiration) the middle position gets you
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
                    r'\s*([\d.]+)f,\s*([\d.]+)f,\s*([\d.]+)f\}')
    out = {}
    for m in rx.finditer(text):
        name, f1, f2, f3, un, dt, asp = m.groups()
        out[name] = {"f1": float(f1), "f2": float(f2), "f3": float(f3),
                     "unison": float(un), "detune_cents": float(dt),
                     "aspiration": float(asp)}
    assert len(out) == 4, f"expected 4 presets, parsed {len(out)}"
    return out


def frow(label, value, frac):
    assert -0.001 <= frac <= 1.001, f"{label}: {value} maps outside the knob"
    frac = min(1.0, max(0.0, frac))
    return f"| {label} | {value:g} | {frac * 100:.0f}% | {clock(frac)} |"


def row(label, value, lo, hi):
    return frow(label, value, (value - lo) / (hi - lo))


# Aspiration has a detent at the BOTTOM of the travel that maps to exactly 0
# (map_lin_off() in param_map.hpp), so the breath can be switched off on a pot
# that never reads exactly 0 at full counter-clockwise. Every non-zero value
# therefore sits OFF_DZ further up the knob than a plain linear range would
# put it.
OFF_DZ = 0.05


def off_frac(v, hi):
    return 0.0 if v <= 0 else OFF_DZ + (1.0 - OFF_DZ) * (v / hi)


# The voice count is eight equal bands over 1..8 (map_voices() in
# param_map.hpp), not a linear range: aim for the CENTRE of the band so a
# knob set by eye lands on the right count with the most margin either side.
def voices_frac(n):
    return (min(8, max(1, int(round(n)))) - 0.5) / 8


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
        if key == "aspiration":
            lines.append(frow(label, vals[key], off_frac(vals[key], hi)))
        elif key == "unison":
            lines.append(frow(label, vals[key], voices_frac(vals[key])))
        else:
            lines.append(row(label, vals[key], lo, hi))
    lines += ["",
              f"Toggles: 1 Octave = {OCTAVE_POS[vals['octave']]}, "
              f"3 Gate = {GATE_POS[vals['gate']]} ({vals['gate']}).", ""]
    return "\n".join(lines)


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
    OUT.write_text("\n".join(parts))
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
