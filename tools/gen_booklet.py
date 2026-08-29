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
         ("F3 amount", "a3", 0, 2), ("Vibrato rate Hz", "vib_rate", 0, 14),
         ("Vibrato depth st", "vib_depth", 0, 4),
         ("Vibrato jitter", "vib_jitter", 0, 0.6)]
V12 = {"bw1": 32.5, "bw2": 47.5, "bw3": 62.5,
       "a1": 1.0, "a2": 1.0, "a3": 1.0, "vib_jitter": 0.10}
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
# wherever you like. Same keys as a parsed preset (f1/f2/f3/vib_rate/
# vib_depth) plus an optional "note" line shown on the card.
EXTRA = {
    # Ear-picked 2026-08-27 (candidate B of three LPC-derived options)
    # from the Sparking! ZERO Fat Boo voice clips (Josh Martin):
    # F1/F2/F3 medians pushed bright, slow bubbery vibrato.
    "Boo": {"f1": 900.0, "f2": 1750.0, "f3": 2900.0,
            "vib_rate": 4.5, "vib_depth": 0.6},
    # Designed by ear 2026-08-28 (no reference clips in the repo, unlike
    # Boo): values chosen from the character's vocal quality, tunable in
    # the lab. Fling = the brash blonde persona: hard bright female,
    # shallow fast wobble.
    "Fling": {"f1": 880.0, "f2": 1600.0, "f3": 3050.0,
              "vib_rate": 6.8, "vib_depth": 0.35},
    # Ki-Ki: shrill piercing female, everything pushed high and fast.
    "Ki-Ki": {"f1": 950.0, "f2": 1800.0, "f3": 3350.0,
              "vib_rate": 7.8, "vib_depth": 0.5},
    # Master: old raspy male, small dark tract, slow wide elderly tremor.
    "Master": {"f1": 640.0, "f2": 1080.0, "f3": 2400.0,
               "vib_rate": 5.2, "vib_depth": 0.9},
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
for MENU 3 (F3 + vibrato). While a menu is latched ONLY the blinking
LED is lit: the other goes dark even if that was the engaged side. The
sound keeps playing, its LED just steps aside so one blinking light is
never mistaken for two lit ones. Tap the OTHER stomp to jump straight
to the other menu; tap the blinking side's own stomp to exit. After
any menu change a knob is inert until you move it, so nothing ever
jumps.

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
sound charges up like a power-up scream, gain swelling, pitch sweeping,
vibrato deepening, while the LEDs alternate faster and faster. Release
to let it wind down. Configure it with the toggles while a menu is
latched:

| Toggle | Menu 2 latched | Menu 3 latched |
|---|---|---|
| 1 | Gain: Above 9000! / on / off | Pitch: rise / fall / off |
| 2 | Charge time: Birit Spomb ~6 s / Hamekameka ~2.5 s / punch ~0.75 s | Tone: brighter / darker / off |
| 3 | Decay: fast / slow / off (instant) | Vibrato: high / low / off |

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
                    r'\s*([\d.]+)f,\s*([\d.]+)f\}')
    out = {}
    for m in rx.finditer(text):
        name, f1, f2, f3, vr, vd = m.groups()
        out[name] = {"f1": float(f1), "f2": float(f2), "f3": float(f3),
                     "vib_rate": float(vr), "vib_depth": float(vd)}
    assert len(out) == 4, f"expected 4 presets, parsed {len(out)}"
    return out


def frow(label, value, frac):
    assert -0.001 <= frac <= 1.001, f"{label}: {value} maps outside the knob"
    frac = min(1.0, max(0.0, frac))
    return f"| {label} | {value:g} | {frac * 100:.0f}% | {clock(frac)} |"


def row(label, value, lo, hi):
    return frow(label, value, (value - lo) / (hi - lo))


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
