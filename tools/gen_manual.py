#!/usr/bin/env python3
"""Generate manual/index.html: the DBscreamZ user manual and preset field guide.

Every behavioural claim traces to firmware/hothouse/ (control map in
main.cpp, gestures in ui_controller.hpp, pickup in knob_pickup.hpp, post
chain in post_chain.hpp, charge defaults in voice_params.hpp).
Every preset number traces to docs/BOOKLET.md.

Regenerate with:  python3 tools/gen_manual.py
Add --pdf to also render manual/DBscreamZ-manual.pdf (needs Chrome or
Chromium on PATH), and --large to blow that up onto US Letter sheets as
manual/DBscreamZ-manual-letter.pdf (needs ghostscript). Both are stdlib
plus those two binaries; nothing to install with pip.
"""

import math
import os
import shutil
import subprocess
import sys

# --------------------------------------------------------------------------
# Geometry
# --------------------------------------------------------------------------
# Card geometry follows the hand-tuned SVGs in manual/*.svg, widened and
# made taller: the viewBox is in millimetres of enclosure face.
VB_W, VB_H = 60.0, 98.0
PRINT_W_MM = 48.0

KNOB_R = 4.0        # drawn knob radius
TIP_R = 7.0         # pointer length: dot sits here
DOT_R = 0.4         # dot at the pointer tip
# Two pointers closer than this have tips that touch. Nothing is merged
# because of it; it is only reported, so the layered line styles can be
# checked against the tightest case on any card.
MERGE_DEG = 2.0 * math.degrees(math.asin(DOT_R / TIP_R))

KNOB_XY = [(15.0, 11.0), (30.0, 11.0), (45.0, 11.0),
           (15.0, 32.0), (30.0, 32.0), (45.0, 32.0)]
TOG_XY = [(15.0, 51.0), (30.0, 51.0), (45.0, 51.0)]
LED_XY = [(15.0, 70.0), (45.0, 70.0)]
FS_XY = [(15.0, 85.0), (45.0, 85.0)]
FS_R = 6.0
LED_R = 1.5

TOG_H = 12.0        # toggle body height
TOG_W = 4.0
TOG_MARK_DX = 6.0   # marker sits this far right of the toggle centre
TOG_DOT_R = 1.2

# Menu identity, from the hand-tuned cards in manual/1_wukong.svg:
# colour AND line style, so the menus stay separable in greyscale and for
# colour-blind readers, and so coincident pointers interleave instead of
# hiding each other. Menu 3's "0 1.2" dash with a round cap draws as dots.
MENUS = {
    1: {"name": "Menu 1", "sub": "default layer", "fill": "#6A1B9A",
        "dash": "", "style": "solid", "width": 0.35},
    2: {"name": "Menu 2", "sub": "hold RIGHT", "fill": "#E65100",
        "dash": "0.45 0.75", "style": "dashed", "width": 0.35},
    3: {"name": "Menu 3", "sub": "hold LEFT", "fill": "#1565C0",
        "dash": "0 1.2", "style": "dotted", "width": 0.4},
}


def clock_to_deg(clock):
    """'7:00' -> -150.0 ; '5:00' -> 150.0 ; '12:29' -> 14.5.

    Degrees clockwise from 12 o'clock, wrapped to (-180, 180].
    """
    h, m = clock.split(":")
    a = (int(h) % 12) * 30.0 + int(m) * 0.5
    if a > 180.0:
        a -= 360.0
    return a


def pt(cx, cy, r, deg):
    """Point at `deg` clockwise from 12 o'clock on a circle (SVG y-down)."""
    a = math.radians(deg)
    return cx + r * math.sin(a), cy - r * math.cos(a)


def pie(cx, cy, r, menus):
    """One marker dot, divided into len(menus) wedges.

    Wedge boundaries start at the top of the dot and menus run CLOCKWISE
    in ascending order, so a half-split always reads lower menu on the
    right, and a third-split reads 1 top-right, 2 bottom, 3 top-left.
    """
    if len(menus) == 1:
        return ('<circle cx="%.3f" cy="%.3f" r="%.2f" fill="%s" '
                'stroke="#111" stroke-width="0.18"/>'
                % (cx, cy, r, MENUS[menus[0]]["fill"]))
    step = 360.0 / len(menus)
    parts = []
    for i, menu in enumerate(menus):
        a0, a1 = i * step, (i + 1) * step
        x0, y0 = pt(cx, cy, r, a0)
        x1, y1 = pt(cx, cy, r, a1)
        large = 1 if (a1 - a0) > 180.0 else 0
        parts.append(
            '<path d="M %.3f %.3f L %.3f %.3f A %.2f %.2f 0 %d 1 %.3f %.3f Z" '
            'fill="%s" stroke="#111" stroke-width="0.18" '
            'stroke-linejoin="round"/>'
            % (cx, cy, x0, y0, r, r, large, x1, y1, MENUS[menu]["fill"]))
    return "".join(parts)


def knob_markers(cx, cy, marks):
    """Every mark on one knob, as a pointer line out of the centre.

    Menu 2 and menu 3 wear the colour of the LED on the side you hold to
    reach them (right orange, left blue), so the card and the pedal agree.

    Each menu keeps its TRUE angle: nothing is merged or averaged. Menus
    are told apart by colour and by line style, and the styles are chosen
    so that pointers lying on top of each other interleave rather than
    hide one another: menu 1 solid underneath, menu 2's dashes over it,
    menu 3's round-cap dots on top. Knob 3 wants 12:00 in all three menus
    on every card, and reads as one purple/orange/blue pointer.
    """
    out = []
    for menu, deg in sorted(marks):          # 1 first, 3 last: draw order
        m = MENUS[menu]
        tx, ty = pt(cx, cy, TIP_R, deg)
        dash = (' stroke-dasharray="%s"' % m["dash"]) if m["dash"] else ""
        out.append('<line x1="%g" y1="%g" x2="%.3f" y2="%.3f" stroke="%s" '
                   'stroke-width="%.2f" stroke-linecap="round"%s/>'
                   % (cx, cy, tx, ty, m["fill"], m["width"], dash))
        out.append('<circle cx="%.3f" cy="%.3f" r="%g" fill="%s"/>'
                   % (tx, ty, DOT_R, m["fill"]))
    return "".join(out)


# --------------------------------------------------------------------------
# Preset data (docs/BOOKLET.md). Entry = (label, value, travel%, clock).
# Clock strings carry the finer precision and drive the drawing; travel%
# is the rounded number printed for the reader.
# --------------------------------------------------------------------------
MENU1 = [
    ("Vocal vol", "unity", 50, "12:00"),
    ("Mix", "full voice", 100, "5:00"),
    ("Master vol", "unity", 50, "12:00"),
    ("Tone", "flat", 50, "12:00"),
    ("Glide", "0 ms", 0, "7:00"),
    ("Vocal size", "as written", 0, "7:00"),
]

MENU2_SHARED = {
    1: ("F1 bandwidth", "32.5 Hz", 9, "7:55"),
    2: ("F1 amount", "1.0", 50, "12:00"),
    4: ("F2 bandwidth", "47.5 Hz", 11, "8:04"),
    5: ("F2 amount", "1.0", 50, "12:00"),
}
MENU3_SHARED = {
    1: ("F3 bandwidth", "62.5 Hz", 12, "8:09"),
    2: ("F3 amount", "1.0", 50, "12:00"),
}

CHARACTERS = [
    {
        "name": "Wukong", "page": "Set 1", "side": "RIGHT",
        "tag": "The default shout",
        "flavour": "Mid-placed formants over the reference three-voice "
                   "stack at the default grain. The straightest read "
                   "of the effect: loud, open, clean. Start here.",
        "m2": {0: ("F1", "858.4 Hz", 55, "12:29"),
               3: ("F2", "1234 Hz", 35, "10:29")},
        "m3": {0: ("F3", "3111.7 Hz", 54, "12:22"),
               3: ("Voices", "3", 31, "10:07"),
               4: ("Detune", "11 ct", 18, "8:49"),
               5: ("Grain", "20 ms", 50, "12:00")},
    },
    {
        "name": "Prince", "page": "Set 1", "side": "LEFT",
        "tag": "Clenched teeth",
        "flavour": "Formants well below Wukong under a wider four-voice "
                   "stack, so it lands darker and "
                   "rougher. Reads as effort rather than power.",
        "m2": {0: ("F1", "741.6 Hz", 45, "11:30"),
               3: ("F2", "1066 Hz", 27, "9:41")},
        "m3": {0: ("F3", "2688.3 Hz", 40, "10:57"),
               3: ("Voices", "4", 44, "11:22"),
               4: ("Detune", "18 ct", 30, "10:00"),
               5: ("Grain", "20 ms", 50, "12:00")},
    },
    {
        "name": "Rice", "page": "Set 2", "side": "RIGHT",
        "tag": "Clean and glassy",
        "flavour": "The highest F3 of the eight over the thinnest stack "
                   "in the factory set: two voices, barely detuned. "
                   "Bright and hard-edged. Cuts without sounding strained.",
        "m2": {0: ("F1", "1000 Hz", 67, "1:40"),
               3: ("F2", "1437.5 Hz", 45, "11:27")},
        "m3": {0: ("F3", "3625 Hz", 71, "2:05"),
               3: ("Voices", "2", 19, "8:52"),
               4: ("Detune", "8 ct", 13, "8:20"),
               5: ("Grain", "20 ms", 50, "12:00")},
    },
    {
        "name": "Piccolo", "page": "Set 2", "side": "LEFT",
        "tag": "Low growl",
        "flavour": "The lowest formants in the factory set under the "
                   "thickest factory stack, five voices spread wide. "
                   "Thick and throaty rather than piercing.",
        "m2": {0: ("F1", "697.6 Hz", 41, "11:08"),
               3: ("F2", "1002.8 Hz", 24, "9:23")},
        "m3": {0: ("F3", "2528.8 Hz", 34, "10:25"),
               3: ("Voices", "5", 56, "12:37"),
               4: ("Detune", "26 ct", 43, "11:20"),
               5: ("Grain", "20 ms", 50, "12:00")},
    },
    {
        "name": "Boo", "page": None, "side": None,
        "tag": "Rubbery and hollow",
        "flavour": "A wide F1-to-F2 gap over a low F3, thickened by "
                   "five widely detuned voices. A big soft body "
                   "rather than an edge.",
        "m2": {0: ("F1", "900 Hz", 58, "12:50"),
               3: ("F2", "1750 Hz", 60, "12:57")},
        "m3": {0: ("F3", "2900 Hz", 47, "11:40"),
               3: ("Voices", "5", 56, "12:37"),
               4: ("Detune", "24 ct", 40, "11:00"),
               5: ("Grain", "20 ms", 50, "12:00")},
    },
    {
        "name": "Fling", "page": None, "side": None,
        "tag": "Light and quick",
        "flavour": "Between Wukong and Rice on every formant, with a "
                   "modest three-voice stack so the edge stays on the "
                   "formants. The most usable of the eight under a band.",
        "m2": {0: ("F1", "880 Hz", 57, "12:40"),
               3: ("F2", "1600 Hz", 52, "12:14")},
        "m3": {0: ("F3", "3050 Hz", 52, "12:10"),
               3: ("Voices", "3", 31, "10:07"),
               4: ("Detune", "14 ct", 23, "9:20"),
               5: ("Grain", "20 ms", 50, "12:00")},
    },
    {
        "name": "Ki-Ki", "page": None, "side": None,
        "tag": "Small and furious",
        "flavour": "The highest F2 of the eight and the thinnest stack "
                   "of the eight, all three formants crowded high with "
                   "nothing blunting them. Shrill, nasal, annoyed.",
        "m2": {0: ("F1", "950 Hz", 62, "1:15"),
               3: ("F2", "1800 Hz", 62, "1:11")},
        "m3": {0: ("F3", "3350 Hz", 62, "1:10"),
               3: ("Voices", "2", 19, "8:52"),
               4: ("Detune", "8 ct", 13, "8:20"),
               5: ("Grain", "20 ms", 50, "12:00")},
    },
    {
        "name": "Master", "page": None, "side": None,
        "tag": "Old and gravelled",
        "flavour": "The lowest F1 and F3 of the eight, the widest "
                   "detune of any card. Dark, "
                   "frayed, faintly ridiculous.",
        "m2": {0: ("F1", "640 Hz", 37, "10:39"),
               3: ("F2", "1080 Hz", 28, "9:45")},
        "m3": {0: ("F3", "2400 Hz", 30, "10:00"),
               3: ("Voices", "4", 44, "11:22"),
               4: ("Detune", "30 ct", 50, "12:00"),
               5: ("Grain", "20 ms", 50, "12:00")},
    },
]

# Charge mode: global, one setting for all voices (voice_params.hpp:53).
# Toggle value mapping is uniform Up=2 / Middle=1 / Down=0, and the factory
# configuration is every toggle centred.
CHARGE = [
    # (toggle, menu 2 role, m2 options up/mid/down, factory m2 position,
    #          menu 3 role, m3 options,             factory m3 position)
    ("T1", "Gain", ["Above 9000!", "on", "off"], "Middle",
           "Pitch", ["rise 2 oct", "fall 2 oct", "off"], "Middle"),
    ("T2", "Charge time", ["Birit Spomb ~6 s", "Hamekameka ~2.5 s",
                           "punch ~0.75 s"], "Middle",
           "Tone", ["brighter", "darker", "off"], "Middle"),
    ("T3", "Decay", ["fast", "slow", "off (instant)"], "Middle",
           "Size", ["full", "half", "off"], "Middle"),
]

TOG_ROWS = ["Up", "Middle", "Down"]


def char_menu(ch, menu):
    """The six (label, value, travel, clock) entries for one menu."""
    if menu == 1:
        return list(MENU1)
    shared = MENU2_SHARED if menu == 2 else MENU3_SHARED
    varying = ch["m2"] if menu == 2 else ch["m3"]
    return [varying.get(i) or shared[i] for i in range(6)]


def char_toggles(ch):
    """Menu 1 toggle positions for a card: (label, value, [positions]).

    T2 is always up OR down, never middle, for every character: a voice
    can live in either Set, but Freeform (middle) has no slot to save to.
    """
    return [("Octave", "0", ["Middle"]),
            ("Memory page", "Set 1 or Set 2", ["Up", "Down"]),
            ("Gate", "medium", ["Middle"])]


# --------------------------------------------------------------------------
# The pedal-face template
# --------------------------------------------------------------------------
DETENT_DY = {"Up": -4.0, "Middle": 0.0, "Down": 4.0}


def svg_face(knob_marks, toggle_marks, dim_knobs=False, width_mm=PRINT_W_MM,
             alternatives=True):
    """knob_marks: 6 lists of (menu, degrees). toggle_marks: 3 lists of
    (menu, 'Up'|'Middle'|'Down'). alternatives=True labels a toggle with
    two marks "OR" (either position works); pass False where two marks on
    one toggle mean two different menus rather than a choice."""
    s = ['<svg class="face" viewBox="0 0 %g %g" width="%gmm" '
         'xmlns="http://www.w3.org/2000/svg" role="img">'
         % (VB_W, VB_H, width_mm)]
    s.append('<rect x="0.6" y="0.6" width="%g" height="%g" rx="3.5" '
             'fill="#fff" stroke="#222" stroke-width="0.5"/>'
             % (VB_W - 1.2, VB_H - 1.2))

    for i, (cx, cy) in enumerate(KNOB_XY):
        dim = ' opacity="0.35"' if dim_knobs else ''
        s.append('<g%s>' % dim)
        s.append('<circle cx="%g" cy="%g" r="%g" fill="#fff" stroke="#000" '
                 'stroke-width="0.4"/>' % (cx, cy, KNOB_R))
        s.append('</g>')
        s.append(knob_markers(cx, cy, knob_marks[i]))
        # Last, so the halo on the numeral masks the pointers crossing it.
        s.append('<text x="%g" y="%g" class="kn">%d</text>'
                 % (cx, cy + 0.95, i + 1))

    for i, (cx, cy) in enumerate(TOG_XY):
        s.append('<rect x="%g" y="%g" width="%g" height="%g" rx="1" '
                 'fill="#f0f0f0" stroke="#000" stroke-width="0.35"/>'
                 % (cx - TOG_W / 2, cy - TOG_H / 2, TOG_W, TOG_H))
        for dy in DETENT_DY.values():
            s.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#888" '
                     'stroke-width="0.25"/>'
                     % (cx + TOG_W / 2, cy + dy, cx + TOG_W / 2 + 1.0,
                        cy + dy))
        s.append('<text x="%g" y="%g" class="kn">T%d</text>'
                 % (cx, cy + TOG_H / 2 + 2.8, i + 1))
        rows = {}
        for menu, row in toggle_marks[i]:
            rows.setdefault(row, []).append(menu)
        for row, menus in rows.items():
            # Two menus wanting the same switch position share a split dot;
            # the colours say which menu, so no label is needed.
            s.append(pie(cx + TOG_MARK_DX, cy + DETENT_DY[row], TOG_DOT_R,
                         sorted(menus)))
        if len(rows) > 1 and alternatives:
            ys = sorted(cy + DETENT_DY[r] for r in rows)
            s.append('<text x="%.2f" y="%.2f" class="orlab">OR</text>'
                     % (cx + TOG_MARK_DX, (ys[0] + ys[-1]) / 2 + 0.7))

    for (cx, cy) in LED_XY:
        s.append('<circle cx="%g" cy="%g" r="%g" fill="#fff" stroke="#888" '
                 'stroke-width="0.35"/>' % (cx, cy, LED_R))
    for (cx, cy), lab in zip(FS_XY, ["L", "R"]):
        s.append('<circle cx="%g" cy="%g" r="%g" fill="#fafafa" '
                 'stroke="#888" stroke-width="0.4"/>' % (cx, cy, FS_R))
        s.append('<text x="%g" y="%g" class="fs">%s</text>'
                 % (cx, cy + 1.6, lab))
    s.append('</svg>')
    return "".join(s)


def card_face(ch):
    knob_marks = [[] for _ in range(6)]
    for menu in (1, 2, 3):
        for i, entry in enumerate(char_menu(ch, menu)):
            knob_marks[i].append((menu, clock_to_deg(entry[3])))
    toggle_marks = [[(1, row) for row in rows]
                    for (_lab, _val, rows) in char_toggles(ch)]
    return svg_face(knob_marks, toggle_marks)


def charge_face():
    """Charge config lives on the toggles while a menu is latched, so the
    knobs are shown greyed: they do nothing for charge."""
    toggle_marks = []
    for (_t, _r2, _o2, pos2, _r3, _o3, pos3) in CHARGE:
        toggle_marks.append([(2, pos2), (3, pos3)])
    return svg_face([[] for _ in range(6)], toggle_marks, dim_knobs=True,
                    width_mm=40.0, alternatives=False)


def svg_knob_demo(marks, width_mm=16.0):
    """One knob on its own, for the how-to-read-a-card worked examples."""
    cx = cy = 11.5
    s = ['<svg class="demo" viewBox="0 0 23 23" width="%gmm" '
         'xmlns="http://www.w3.org/2000/svg" role="img">' % width_mm]
    s.append('<circle cx="%g" cy="%g" r="%g" fill="#fff" stroke="#000" '
             'stroke-width="0.4"/>' % (cx, cy, KNOB_R))
    s.append(knob_markers(cx, cy, marks))
    s.append('</svg>')
    return "".join(s)


def overlap_report():
    """Nothing is merged, so this only reports how hard the layered line
    styles are being asked to work: how many pointers coincide exactly,
    and the tightest pair that is close but not equal."""
    exact = 0
    tightest = (999.0, "")
    for ch in CHARACTERS:
        for i in range(6):
            degs = [(m, clock_to_deg(char_menu(ch, m)[i][3]))
                    for m in (1, 2, 3)]
            for (m0, d0), (m1, d1) in [(degs[0], degs[1]), (degs[0], degs[2]),
                                       (degs[1], degs[2])]:
                gap = abs(d1 - d0)
                if gap == 0:
                    exact += 1
                elif gap < tightest[0]:
                    tightest = (gap, "%s K%d menus %d and %d"
                                % (ch["name"], i + 1, m0, m1))
    return ("pointer overlaps: %d exact pairs (drawn as one striped "
            "pointer)\n  tightest distinct pair: %.1f deg (%.0f clock "
            "minutes) at %s" % (exact, tightest[0], tightest[0] * 2,
                                tightest[1]))


# --------------------------------------------------------------------------
# HTML
# --------------------------------------------------------------------------
CSS = """
/* The booklet is the size of the pedal face: 66 x 122 mm, so it drops in
   the box. Everything below is sized for a 56 x 112 mm text block. */
@page { size: 66mm 122mm; margin: 5mm; }
* { box-sizing: border-box; }
html, body { margin: 0; padding: 0; background: #fff; color: #111; }
body {
  font-family: "Charter", "Bitstream Charter", "Georgia", serif;
  font-size: 7.4pt; line-height: 1.34;
}
.page {
  width: 56mm; min-height: 108mm; page-break-after: always;
  position: relative; padding-bottom: 5mm;
}
.page:last-child { page-break-after: auto; }
h1 { font-size: 11pt; margin: 0 0 2.4mm; letter-spacing: -0.2px;
     line-height: 1.1; border-bottom: 0.8pt solid #111;
     padding-bottom: 1mm; }
h1.ctr { text-align: center; }
h1 .num { color: #999; font-weight: normal; margin-right: 1.5mm; }
h2 { font-size: 8.2pt; margin: 3.5mm 0 2mm;
     border-bottom: 0.8pt solid #111; padding-bottom: 0.6mm; }
h3 { font-size: 7.6pt; margin: 2.5mm 0 0.8mm; }
p { margin: 0 0 1.8mm; }
ul, ol { margin: 0 0 1.8mm; padding-left: 3.5mm; }
li { margin-bottom: 0.8mm; }
.lede { font-size: 8pt; }
.small { font-size: 6.6pt; color: #444; }
strong { font-weight: 600; }

table { width: 100%; border-collapse: collapse; font-size: 6.6pt;
        margin: 1mm 0 2mm; }
th, td { border-bottom: 0.3pt solid #bbb; padding: 0.6mm 0.8mm;
         text-align: left; vertical-align: top; }
th { border-bottom: 0.6pt solid #111; font-weight: 600; }
tr:last-child td { border-bottom: 0.6pt solid #111; }

.note { border-left: 2pt solid #E65100; padding: 1mm 0 1mm 2mm;
        margin: 2mm 0; font-size: 6.8pt; background: #fdf5ef; }
.note b { display: block; text-transform: uppercase; letter-spacing: 0.3px;
          font-size: 6.2pt; margin-bottom: 0.5mm; }

.flow { display: block; margin: 2mm auto 3mm; max-width: 100%; }
svg text.fl { font-family: "DejaVu Sans", sans-serif; font-size: 2.6px;
              text-anchor: middle; fill: #222; }
svg text.flk { font-family: "DejaVu Sans", sans-serif; font-size: 2.2px;
               font-weight: bold; text-anchor: middle; fill: #6A1B9A; }

.foot { position: absolute; bottom: 0; left: 0; right: 0;
        font-size: 7.2pt; color: #888; border-top: 0.4pt solid #ddd;
        padding-top: 1mm; display: flex; justify-content: space-between; }

.cover { text-align: center; padding-top: 46mm; }
.cover .word { font-size: 30pt; letter-spacing: -0.5px; margin: 0; }
.cover .sub { font-size: 10.5pt; color: #444; margin-top: 2mm; }
.cover .rule { width: 34mm; height: 1.2pt; background: #111;
               margin: 8mm auto; }

/* character cards */
.cardhead { display: flex; align-items: baseline; gap: 2mm; flex-wrap: wrap;
            border-bottom: 0.8pt solid #111; padding-bottom: 0.8mm; }
.cardhead h1 { margin: 0; border: none; padding-bottom: 0; }
.tag { font-size: 7.4pt; font-style: italic; color: #555; }
h1 .tag { font-size: 7pt; }
.slot { font-size: 6.6pt; color: #444; margin: 1mm 0 1.5mm; }
.figure { margin-top: 1.5mm; }
.mtab { width: 100%; font-size: 6pt; border-collapse: collapse;
        margin-bottom: 1.5mm; }
.mtab caption { text-align: left; font-size: 6.4pt; font-weight: 600;
                padding: 0.6mm 0 0.3mm; }
.mtab th, .mtab td { border-bottom: 0.3pt solid #ddd; padding: 0.2mm 0.6mm;
                     text-align: left; }
.mtab .clk { text-align: right; font-family: "DejaVu Sans Mono", monospace;
             white-space: nowrap; }
.mtab .k { color: #888; width: 4mm; }
.mrow { display: flex; align-items: baseline; }
.mrow > span { flex: 1 1 auto; }
.mrow > .swatch { flex: 0 0 auto; }
.swatch { display: inline-block; width: 2.2mm; height: 2.2mm;
          border: 0.3pt solid #111; vertical-align: -0.2mm;
          margin-right: 1mm; border-radius: 50%; }

.toc { width: 100%; border-collapse: collapse; font-size: 7.2pt; }
.toc td { border: none; padding: 0.7mm 0; vertical-align: baseline; }
.toc .n { width: 5mm; color: #999; }
.toc .p { text-align: right; width: 6mm; color: #666; }
.toc .l2 td { font-size: 6.8pt; color: #444; }
.toc .l2 td:nth-child(2) { padding-left: 4mm; }

.legend { display: flex; flex-direction: column; gap: 1mm; font-size: 6.8pt;
          margin: 1.5mm 0 2mm; }
.legend > div { display: flex; align-items: center; gap: 1.5mm; }
.demos { display: flex; gap: 2mm; margin: 2mm 0; }
.demos figure { margin: 0; flex: 1; }
.demos figcaption { font-size: 5.8pt; color: #444; margin-top: 0.6mm;
                    line-height: 1.2; }
/* The white halo keeps the knob number readable where a pointer crosses. */
svg text.kn { font-family: "DejaVu Sans", sans-serif; font-size: 2.6px;
              text-anchor: middle; fill: #777; stroke: #fff;
              stroke-width: 0.7; paint-order: stroke fill; }
svg text.fs { font-family: "DejaVu Sans", sans-serif; font-size: 4px;
              text-anchor: middle; fill: #999; }
svg text.orlab { font-family: "DejaVu Sans", sans-serif; font-size: 2.4px;
                 text-anchor: middle; fill: #666; }
.face, .demo { display: block; margin: 0 auto; }

@media screen {
  body { background: #e8e8e8; padding: 6mm 0; }
  .page { background: #fff; margin: 0 auto 4mm; padding: 5mm;
          width: 66mm; min-height: 122mm;
          box-shadow: 0 0.6mm 2mm rgba(0,0,0,0.25); }
  .foot { left: 5mm; right: 5mm; bottom: 2mm; }
}
"""


SVG_TEXT_CSS = (
    # The white halo keeps the knob number readable where a pointer line
    # crosses it.
    'text.kn{font-family:sans-serif;font-size:2.6px;text-anchor:middle;'
    'fill:#888;stroke:#fff;stroke-width:0.7;paint-order:stroke fill}'
    'text.fs{font-family:sans-serif;font-size:4px;text-anchor:middle;'
    'fill:#aaa}'
    'text.orlab{font-family:sans-serif;font-size:2.2px;font-weight:bold;'
    'text-anchor:middle;fill:#444}')


_page_no = [0]


def page(inner, num, label=""):
    """`num` is kept only as a flag: pass "" on covers to hide the folio.
    The printed number is the running count, so inserting a page cannot
    leave the numbering wrong."""
    _page_no[0] += 1
    foot = ('<div class="foot"><span>%s</span><span>%s</span></div>'
            % (label, _page_no[0] if num else ""))
    return '<div class="page">%s%s</div>' % (inner, foot)


def swatch(menu):
    return ('<span class="swatch" style="background:%s"></span>'
            % MENUS[menu]["fill"])


def legend_mark(menu):
    """A pointer and its dot, inline, for the legend."""
    m = MENUS[menu]
    dash = (' stroke-dasharray="%s"' % m["dash"]) if m["dash"] else ""
    return ('<svg viewBox="0 0 11 4" width="11mm" style="vertical-align:-0.6mm"'
            ' xmlns="http://www.w3.org/2000/svg"><line x1="0.4" y1="2" '
            'x2="8" y2="2" stroke="%s" stroke-width="0.38" '
            'stroke-linecap="round"%s/><circle cx="8" cy="2" r="1.1" '
            'fill="%s" stroke="#111" stroke-width="0.18"/></svg>'
            % (m["fill"], dash, m["fill"]))


def menu_table(title, menu, entries):
    rows = "".join(
        '<tr><td class="k">K%d</td><td>%s</td><td>%s</td>'
        '<td class="clk">%d%% &middot; %s</td></tr>'
        % (i + 1, lab, val, trav, clk)
        for i, (lab, val, trav, clk) in enumerate(entries))
    return ('<table class="mtab"><caption>%s%s</caption>%s</table>'
            % (swatch(menu), title, rows))


def flow_svg():
    """The signal path, laid out for a 56 mm column: the voice chain runs
    down the left, the dry signal down the right, and they meet at the mix
    crossfade. Boxes a knob controls are tinted and carry the knob."""
    W, H = 56.0, 86.0
    BW, BH, SPINE = 25.0, 6.0, 14.5

    def box(x, y, w, label, knob=None):
        fill = "#f6eefa" if knob else "#f4f4f4"
        edge = "#6A1B9A" if knob else "#aaa"
        out = ('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="1.2" '
               'fill="%s" stroke="%s" stroke-width="0.4"/>'
               % (x, y, w, BH, fill, edge))
        out += ('<text x="%.1f" y="%.1f" class="fl">%s</text>'
                % (x + w / 2, y + 3.9, label))
        if knob:
            out += ('<text x="%.1f" y="%.1f" class="flk">%s</text>'
                    % (x + w - 2.6, y + 3.9, knob))
        return out

    def path(d):
        return ('<path d="%s" stroke="#777" stroke-width="0.4" fill="none" '
                'marker-end="url(#ar)"/>' % d)

    s = ['<svg class="flow" viewBox="0 0 %g %g" width="52mm" '
         'xmlns="http://www.w3.org/2000/svg" role="img">' % (W, H)]
    s.append('<defs><marker id="ar" viewBox="0 0 6 6" refX="5.4" refY="3" '
             'markerWidth="3.4" markerHeight="3.4" orient="auto">'
             '<path d="M 0 0 L 6 3 L 0 6 z" fill="#777"/></marker></defs>')
    s.append('<text x="%.1f" y="3.4" class="fl">guitar in</text>' % SPINE)
    s.append('<path d="M %.1f 4.6 V 12.6" stroke="#777" stroke-width="0.4" '
             'fill="none"/>' % SPINE)
    s.append(path('M %.1f 7.5 H 30.4' % SPINE))
    s.append(box(31, 4.5, 23, "dry, untouched"))

    chain = [("gate", "T3"), ("pitch track", None), ("FOF voice", None),
             ("tone", "K4"), ("vocal vol", "K1")]
    y = 12.6
    for i, (label, knob) in enumerate(chain):
        s.append(box(2, y, BW, label, knob))
        y += BH
        s.append(path('M %.1f %.1f V %.1f' % (SPINE, y, y + 2.0)))
        y += 2.0
    # voice into the mix
    mix_y = y + 1.4
    s.append(box(2, mix_y, BW, "mix", "K2"))
    # dry down the right and back into the mix
    s.append(path('M 42.5 10.5 V %.1f H %.1f' % (mix_y + 3.0, BW + 2.6)))
    s.append(path('M %.1f %.1f V %.1f' % (SPINE, mix_y + BH, mix_y + BH + 2.6)))
    s.append(box(2, mix_y + BH + 2.6, BW, "master", "K3"))
    s.append(path('M %.1f %.1f H 34' % (BW + 2, mix_y + BH + 5.6)))
    s.append('<text x="35" y="%.1f" class="fl" style="text-anchor:start">out'
             '</text>' % (mix_y + BH + 6.6))
    s.append('</svg>')
    return "".join(s)


TOC = []


def toc(num, title, level=1):
    """Record that the NEXT page starts a section, for the contents page."""
    TOC.append((num, title, level, _page_no[0] + 1))


KNOB_ROWS = "".join(
    '<tr><td><strong>K%d</strong></td><td>%s</td><td>%s</td><td>%s</td></tr>'
    % (i + 1, MENU1[i][0],
       (CHARACTERS[0]["m2"].get(i) or MENU2_SHARED[i])[0],
       (CHARACTERS[0]["m3"].get(i) or MENU3_SHARED[i])[0])
    for i in range(6))

CHARGE_ROWS_2 = "".join(
    '<tr><td><strong>%s</strong><br>%s</td><td>%s</td><td>%s</td><td>%s</td>'
    '</tr>' % (t, role, o[0], o[1], o[2])
    for (t, role, o, _p, _r3, _o3, _p3) in CHARGE)

CHARGE_ROWS_3 = "".join(
    '<tr><td><strong>%s</strong><br>%s</td><td>%s</td><td>%s</td><td>%s</td>'
    '</tr>' % (t, role3, o3[0], o3[1], o3[2])
    for (t, _r, _o, _p, role3, o3, _p3) in CHARGE)


def build_pages(toc_rows=""):
    P = []
    TOC.clear()
    _page_no[0] = 0        # both passes must number identically

    # ---- cover ----------------------------------------------------------
    P.append(page(
        '<div class="cover">'
        '<p class="word">DBscreamZ</p>'
        '<p class="sub">Pitch-tracking formant scream synthesiser</p>'
        '<div class="rule"></div>'
        '<p class="small">User manual<br>&amp; preset field guide</p>'
        '</div>', "", ""))

    P.append(page(
        '<h1 class="ctr">Contents</h1>'
        '<table class="toc">' + (toc_rows or "<tr><td>&nbsp;</td></tr>") +
        '</table>', "", "Contents"))

    # ---- what it is -----------------------------------------------------
    toc('1', 'What this is')
    P.append(page(
        '<h1><span class="num">1</span>What this is</h1>'
        '<p class="lede">Play a note. A synthesised voice screams it back '
        'at you, tracking your pitch as you bend, slide and vibrato.</p>'
        '<p>The voice is not a sample and not a filter draped over your '
        'guitar. It is built from scratch every block: a pitch tracker '
        'follows your fundamental, and a bank of formant grains rebuilds a '
        'vowel at that pitch. Three formants (F1, F2, F3) decide which '
        'vowel and how bright it is. There is no noise anywhere in the '
        'voice path, which is why it screams rather than hisses.</p>'
        '<p>Your dry signal is never processed. It is passed through '
        'untouched and crossfaded against the voice at the end, so Mix at '
        '7:00 is your guitar, exactly as it went in.</p>',
        "y", "What this is"))

    P.append(page(
        '<h2 style="margin-top:0">Signal flow</h2>'
        + flow_svg() +
        '<p class="small">The gate watches your input envelope and '
        'decides when the voice may sound. It never gates your dry '
        'signal.</p>',
        "y", "Signal flow"))

    # ---- quick start ----------------------------------------------------
    toc('2', 'First sound')
    P.append(page(
        '<h1><span class="num">2</span>First sound</h1>'
        '<ol>'
        '<li>Guitar into <strong>IN</strong>, amp into <strong>OUT</strong>, '
        '9&nbsp;V DC into the barrel jack. The pedal boots '
        '<strong>bypassed</strong>, both LEDs off.</li>'
        '<li>Middle toggle <strong>up</strong> (Set&nbsp;1). Put the other '
        'two toggles in the <strong>middle</strong>.</li>'
        '<li>Tap the <strong>RIGHT</strong> footswitch. The right LED goes '
        'solid: Wukong is engaged.</li>'
        '<li>Play single notes, cleanly, one at a time. The voice follows '
        'your pitch.</li>'
        '<li>Nothing screaming? Turn <strong>knob 2 (Mix)</strong> '
        'clockwise and set the <strong>right toggle to the middle</strong>.'
        '</li>'
        '<li>Now stomp <strong>both</strong> footswitches at once and hold. '
        'That is Charge mode.</li>'
        '</ol>'
        '<div class="note"><b>One note at a time</b>'
        'Pitch tracking needs a single fundamental. Chords, open strings '
        'ringing under a lead line, and heavy pick noise all confuse it. '
        'Mute what you are not playing.</div>'
        '<p>Tap the same footswitch again to bypass. Tap the '
        '<strong>LEFT</strong> footswitch instead for the other voice on '
        'this page (Prince). Flip the middle toggle <strong>down</strong> '
        'for Set&nbsp;2: Rice on the right, Piccolo on the left.</p>'
        '<p>Everything you change is live immediately and lost at power '
        'off until you save it. Saving is in section 5.</p>',
        "y", "Quick start"))

    # ---- control surface ------------------------------------------------
    blank = svg_face([[] for _ in range(6)], [[] for _ in range(3)],
                     width_mm=44.0)
    toc('3', 'The controls')
    P.append(page(
        '<h1><span class="num">3</span>The controls</h1>'
        '<div style="text-align:center">' + blank + '</div>'
        '<p style="margin-top:1.5mm">Six knobs in two rows of three, '
        'numbered left to right: <strong>1 2 3</strong> on top, '
        '<strong>4 5 6</strong> below. Three toggles, each up, middle or '
        'down. Two footswitches, an LED [light-emitting diode] above '
        'each.</p>'
,
        "y", "The controls"))

    P.append(page(
        '<h2 style="margin-top:0">Knob travel</h2>'
        '<p>Knobs run <strong>7:00</strong> fully anticlockwise, through '
        '<strong>12:00</strong> at centre, to <strong>5:00</strong> fully '
        'clockwise. Every position in this booklet is a clock face.</p>'
        '<h2>What the knobs do</h2>'
        '<table><tr><th></th><th>Menu 1</th><th>Menu 2</th>'
        '<th>Menu 3</th></tr>' + KNOB_ROWS + '</table>'
        '<p class="small">Menu 3: <strong>K4</strong> steps through the '
        'eight voice counts in eight equal bands, so it always lands on a '
        'whole number. <strong>K6</strong> has a detent at <strong>12:00'
        '</strong> that reads as exactly 20 ms, the grain length every '
        'character ships with, so centring it always returns you to the '
        'factory texture.</p>',
        "y", "The controls"))

    P.append(page(
        '<h2 style="margin-top:0">What the menu 1 knobs do</h2>'
        '<p><strong>Vocal vol</strong> (K1) is how loud the synthesised '
        'voice is on its own. <strong>Mix</strong> (K2) crossfades it '
        'against your untouched dry signal: 7:00 is the guitar alone, '
        '5:00 is the voice alone. <strong>Master</strong> (K3) is the '
        'level of the pair leaving the pedal. All three are unity at '
        '12:00.</p>'
        '<p><strong>Tone</strong> (K4) tilts the <em>voice</em> dark '
        'below 12:00 and bright above it, and never touches the dry. It '
        'has a detent at 12:00 that is exactly flat.</p>'
        '<p><strong>Glide</strong> (K5) is how long the voice takes to '
        'reach each new note, 0 to 300 ms. The taper is steep: the first '
        'two thirds of the travel covers 0 to 100 ms, so the whole usable '
        'range of slurs sits below 2:00.</p>'
        '<p><strong>Vocal size</strong> (K6) scales all three formants '
        'together, which is acoustically vocal tract length: the same '
        'character shouting the same vowel out of a physically bigger '
        'body. At <strong>7:00</strong> it is the character exactly as '
        'written, which is what every card in section 8 assumes. At '
        '<strong>5:00</strong> every formant is halved, the deepest it '
        'goes.</p>',
        "y", "The controls"))

    P.append(page(
        '<h2 style="margin-top:0">What the toggles do</h2>'
        '<table><tr><th></th><th>Up</th><th>Middle</th><th>Down</th></tr>'
        '<tr><td><strong>T1</strong><br>Octave</td><td>+1</td><td>0</td>'
        '<td>&minus;1</td></tr>'
        '<tr><td><strong>T2</strong><br>Page</td><td>Set 1</td>'
        '<td>Freeform</td><td>Set 2</td></tr>'
        '<tr><td><strong>T3</strong><br>Gate</td><td>high</td>'
        '<td>medium</td><td>low</td></tr></table>'
        '<p>That is with no menu latched. While a menu <em>is</em> latched '
        'the toggles configure Charge mode instead, and nothing else. See '
        'section 6.</p>'
        '<p class="small">The gate decides how loud you must play before '
        'the voice speaks. High needs a firm attack and stays quiet '
        'between notes; low lets quiet playing through and hangs on '
        'longer.</p>',
        "y", "The controls"))

    # ---- menus ----------------------------------------------------------
    toc('4', 'The three menus')
    P.append(page(
        '<h1><span class="num">4</span>The three menus</h1>'
        '<p>Six knobs, eighteen parameters. What a knob does depends on '
        'which menu is latched.</p>'
        '<table>'
        '<tr><th>Menu</th><th>How to get there</th></tr>'
        '<tr><td><div class="mrow">' + swatch(1) + '<span><strong>1</strong>'
        '</span></div></td>'
        '<td>Where you are with nothing latched</td></tr>'
        '<tr><td><div class="mrow">' + swatch(2) + '<span><strong>2</strong>'
        '</span></div></td>'
        '<td>Hold the <strong>RIGHT</strong> footswitch. About 1 second '
        'in it latches under your foot: right LED blinks, left goes '
        'dark. Letting go changes nothing.</td></tr>'
        '<tr><td><div class="mrow">' + swatch(3) + '<span><strong>3</strong>'
        '</span></div></td>'
        '<td>Hold the <strong>LEFT</strong> footswitch the same way. '
        'Left LED blinks, right goes dark.</td></tr></table>',
        "y", "The three menus"))

    P.append(page(
        '<p>A menu <strong>latches</strong>: it stays until you leave it.</p>'
        '<ul>'
        '<li>Tap the <strong>other</strong> footswitch to jump straight to '
        'the other menu.</li>'
        '<li>Tap the <strong>blinking side\'s own</strong> footswitch to '
        'drop back to menu 1.</li>'
        '</ul>'
        '<div class="note"><b>Knob pickup: nothing jumps</b>'
        'Every time you change menu, or recall a voice, all six knobs go '
        '<strong>inert</strong>. A knob does nothing until you '
        '<strong>move it</strong>, about a fingernail\'s nudge. The moment '
        'it moves it takes over its parameter, from wherever it is '
        'physically sitting.<br><br>'
        'A knob you do not touch cannot hurt the sound. A knob you do '
        'touch grabs control at once: it does not wait for you to sweep '
        'back to the stored value.</div>',
        "y", "The three menus"))

    P.append(page(
        '<h2 style="margin-top:0">What the formant controls mean</h2>'
        '<p><strong>F1 and F2</strong> decide which vowel the voice is '
        'shouting. Move them and the scream changes from an <em>ah</em> to '
        'an <em>ee</em> to an <em>oh</em>.</p>'
        '<p><strong>F3</strong> mostly decides how bright and nasal it '
        'sounds.</p>'
        '<p><strong>Bandwidth</strong> is how wide each resonance is: '
        'narrow is more vocal, wide is more washed out. '
        '<strong>Amount</strong> is how much of that formant you get.</p>'
        '<p><strong>Voices</strong> is how many copies of the scream sing '
        'at once, one to eight, and <strong>Detune</strong> is how far '
        'apart they are tuned, in cents. Together they are the thickness '
        'of the voice: one voice is bare and focused, eight spread wide is '
        'a crowd of one person.</p>'
        '<p><strong>Grain</strong> is how long each grain of the voice '
        'lasts, from 4 to 40 ms. It sets texture rather than pitch: short '
        'grains are buzzy and rough, long ones smooth and vocal. There is '
        'no noise generator anywhere in this pedal, so every one of these '
        'controls shapes the voice itself.</p>',
        "y", "The three menus"))

    # ---- memory ---------------------------------------------------------
    toc('5', 'Memory')
    P.append(page(
        '<h1><span class="num">5</span>Memory</h1>'
        '<p>The middle toggle picks a <strong>page</strong>. Each page has '
        'two <strong>slots</strong>, one per footswitch. Four stored '
        'voices.</p>'
        '<table>'
        '<tr><th>T2</th><th>Page</th><th>Left</th><th>Right</th></tr>'
        '<tr><td>Up</td><td>Set 1</td><td>Prince</td><td>Wukong</td></tr>'
        '<tr><td>Mid</td><td>Freeform</td><td colspan="2">no slots: the '
        'footswitches engage and bypass whatever the knobs are set to right '
        'now</td></tr>'
        '<tr><td>Down</td><td>Set 2</td><td>Piccolo</td><td>Rice</td></tr>'
        '</table>'
        '<p class="small">Those four are what it ships with. Save over any '
        'of them: the recipes to dial them back are in section 8.</p>',
        "y", "Memory"))

    P.append(page(
        '<h2 style="margin-top:0">Saving</h2>'
        '<p>Everything you tweak is live but volatile. To save:</p>'
        '<ol>'
        '<li>Hold <strong>one</strong> footswitch past 1 second.</li>'
        '<li><strong>While still holding it</strong>, press the other.</li>'
        '</ol>'
        '<p>The <strong>held</strong> side picks the slot. Hold RIGHT and '
        'press LEFT and you have saved to the <strong>right</strong> slot '
        'of the current page. Both LEDs blink three times.</p>'
        '<div class="note"><b>Freeform cannot save</b>'
        'It has no slot, so the save is refused and both LEDs give one '
        'short double-flicker. Pick Set&nbsp;1 or Set&nbsp;2 first.</div>'
        '<p>Leave any latched menu before saving. Menus never save, and '
        'your edits survive the exit.</p>'
        '<p class="small">The held footswitch latches its own menu on the '
        'way past 1 second, the same as any hold. The second press drops '
        'that menu again and saves, so you finish where you began.</p>',
        "y", "Memory"))

    # ---- charge ---------------------------------------------------------
    toc('6', 'Charge mode')
    P.append(page(
        '<h1><span class="num">6</span>Charge mode</h1>'
        '<p>With a voice engaged and <strong>no menu latched</strong>, '
        'stomp <strong>both</strong> footswitches together and hold. The '
        'scream charges: gain swells, pitch sweeps, the voice grows, and '
        'the LEDs alternate faster and faster as it builds. Release and it '
        'winds down.</p>'
        '<p>At a <strong>full charge every row you have switched on reaches '
        'the top of its range</strong>: the gain is all the way up, the '
        'pitch has swept two octaves, and the voice has grown to its '
        'deepest. On the amount rows, gain and size, the middle '
        'position gets you halfway there from wherever the voice already '
        'sits and the up position takes it the whole way. Tone is a '
        'direction rather than an amount, so both of its positions go the '
        'whole way: darker is fully dark, brighter fully bright.</p>'
        '<p>It lasts exactly as long as you hold it. No latch, no '
        'timeout.</p>'
        '<p><strong>Engaged is the whole condition.</strong> The same '
        'two-stomp hold while <em>bypassed</em> is the firmware-update '
        'gesture instead, so charge can never reach it and it can never '
        'reach charge. See section 11.</p>'
        '<p class="small">Charge is configured on the '
        '<strong>toggles</strong>, while a menu is latched. Latch menu 2 '
        '(hold RIGHT) for the orange settings, menu 3 (hold LEFT) for the '
        'blue ones. The knobs do nothing here.</p>',
        "y", "Charge mode"))

    P.append(page(
        '<h2 style="margin-top:0">Factory charge settings</h2>'
        '<div style="text-align:center">' + charge_face() + '</div>'
        '<p style="margin-top:1.5mm">Every toggle centred. Each dot is '
        'half orange and half blue because both menus want that switch in '
        'the middle: orange on the right, blue on the left.</p>'
        '<p class="small">This configuration is <strong>global</strong>: '
        'one setting shared by every voice, kept across power cycles, and '
        'never touched by saving or recalling. That is why it is not on '
        'the character cards.</p>',
        "y", "Charge mode"))

    P.append(page(
        '<h3 style="margin-top:0">' + swatch(2) + 'With menu 2 latched</h3>'
        '<table><tr><th></th><th>Up</th><th>Middle</th>'
        '<th>Down</th></tr>' + CHARGE_ROWS_2 + '</table>'
        '<h3>' + swatch(3) + 'With menu 3 latched</h3>'
        '<table><tr><th></th><th>Up</th><th>Middle</th>'
        '<th>Down</th></tr>' + CHARGE_ROWS_3 + '</table>'
        '<p class="small">Centred means the charge pitch <em>falls</em>. '
        'For the rising power-up, latch menu 3 and flick T1 up.</p>',
        "y", "Charge mode"))

    # ---- how to read a card ---------------------------------------------
    legend = "".join(
        '<div>%s <span><strong>%s</strong>, %s pointer<br>'
        '<span class="small">%s</span></span></div>'
        % (legend_mark(m), MENUS[m]["name"], MENUS[m]["style"],
           MENUS[m]["sub"]) for m in (1, 2, 3))
    toc('7', 'Reading a card')
    P.append(page(
        '<h1><span class="num">7</span>Reading a card</h1>'
        '<p>Each card is the pedal face with a pointer drawn out of every '
        'knob, one per menu, ending at the position that menu wants. '
        '<strong>Twenty-one positions:</strong> six knobs in each of three '
        'menus, plus three toggles. Set every one. None can be assumed: '
        'the knobs are wherever you left them.</p>'
        '<div class="legend">' + legend + '</div>'
        '<p class="small">Told apart by colour <em>and</em> by line, so a '
        'photocopy still works. Menus 2 and 3 wear the colour of the LED '
        'on the side you hold to reach them: orange for the right, blue '
        'for the left.</p>',
        "y", "Reading a card"))

    fling = next(c for c in CHARACTERS if c["name"] == "Fling")
    demo_a = [(m, clock_to_deg("12:00")) for m in (1, 2, 3)]
    demo_b = [(1, clock_to_deg("7:00")), (2, clock_to_deg("7:55")),
              (3, clock_to_deg("8:09"))]
    demo_c = [(1, clock_to_deg("12:00")),
              (2, clock_to_deg(fling["m2"][3][3])),
              (3, clock_to_deg(fling["m3"][3][3]))]
    P.append(page(
        '<p style="margin-top:0">Every pointer sits at its exact position, '
        'including when menus agree. The line styles interleave, so where '
        'two or three menus want the same knob in the same place you get '
        'one pointer striped in their colours rather than three lines '
        'fighting over the same pixels.</p>'
        '<div class="demos">'
        '<figure>' + svg_knob_demo(demo_a) +
        '<figcaption><strong>All three agree.</strong> Knob 3 on every '
        'card: 12:00 in all three menus.</figcaption></figure>'
        '<figure>' + svg_knob_demo(demo_b) +
        '<figcaption><strong>One apart, two close.</strong> Knob 2 on every '
        'card.</figcaption></figure>'
        '<figure>' + svg_knob_demo(demo_c) +
        '<figcaption><strong>A tight cluster.</strong> Fling\'s knob 4, '
        'inside 14 minutes.</figcaption></figure>'
        '</div>'
        '<div class="note"><b>The table is the exact word</b>'
        'Pointers are drawn true, but a knob is a blunt instrument and two '
        'pointers a few minutes apart look like one. Each card is followed '
        'by the same twenty-one positions as a table. When the drawing is '
        'ambiguous, read the table.</div>',
        "y", "Reading a card"))

    P.append(page(
        '<h2 style="margin-top:0">Toggles on a card</h2>'
        '<p>Toggle dots sit to the right of the switch, level with the '
        'position they want.</p>'
        '<p>The middle toggle always shows <strong>two</strong> dots, '
        'marked OR. A voice can live on either Set, and which one you pick '
        'is up to you. It cannot live on Freeform, because Freeform has no '
        'slot to save into.</p>'
        '<p>The other two toggles show one dot each. Every character wants '
        'octave 0 and the medium gate; both are the middle position.</p>'
        '<p class="small">Four of the eight have a factory home. The other '
        'four are recipes: dial them in and save them wherever you '
        'like.</p>',
        "y", "Reading a card"))

    # ---- the eight characters -------------------------------------------
    for n, ch in enumerate(CHARACTERS):
        if n == 0:
            toc('8', 'The eight voices')
        toc('', ch["name"], level=2)
        slot = ("Factory: %s, %s slot" % (ch["page"], ch["side"])
                if ch["page"] else "No factory slot: save it to any page")
        P.append(page(
            '<div class="cardhead"><h1>%s</h1>'
            '<span class="tag">%s</span></div>'
            '<p class="slot">%s</p>'
            '<div class="figure">%s</div>'
            '<p style="margin-top:2mm">%s</p>'
            % (ch["name"], ch["tag"], slot, card_face(ch), ch["flavour"]),
            "y", ch["name"]))
        tabs = "".join(
            menu_table("Menu %d %s" % (m, ["", "(default)", "(hold RIGHT)",
                                           "(hold LEFT)"][m]),
                       m, char_menu(ch, m))
            for m in (1, 2, 3))
        trows = "".join(
            '<tr><td class="k">T%d</td><td>%s</td><td>%s</td>'
            '<td class="clk">%s</td></tr>'
            % (i + 1, lab, val, " or ".join(rows))
            for i, (lab, val, rows) in enumerate(char_toggles(ch)))
        P.append(page(
            '<h2 style="margin:0 0 1mm">%s: every position</h2>'
            '%s<table class="mtab"><caption>%sToggles</caption>%s</table>'
            % (ch["name"], tabs, swatch(1), trows),
            "y", ch["name"]))

    # ---- reference ------------------------------------------------------
    toc('9', 'Every gesture')
    P.append(page(
        '<h1><span class="num">9</span>Every gesture</h1>'
        '<table>'
        '<tr><th>To do this</th><th>Do that</th></tr>'
        '<tr><td>Engage or bypass</td><td>Tap either footswitch</td></tr>'
        '<tr><td>Latch menu 2<br>(F1, F2)</td>'
        '<td>Hold RIGHT ~1 s: it latches while you hold</td></tr>'
        '<tr><td>Latch menu 3<br>(F3, voice stack)</td>'
        '<td>Hold LEFT ~1 s: it latches while you hold</td></tr>'
        '<tr><td>Jump to the other menu</td>'
        '<td>With one latched, tap the other footswitch</td></tr>'
        '<tr><td>Leave a menu</td>'
        '<td>Tap the blinking side\'s own footswitch</td></tr>'
        '<tr><td>Save to a slot</td>'
        '<td>Hold one footswitch past 1 s, then press the other while '
        'still holding. The held side is the slot.</td></tr>'
        '<tr><td>Charge</td>'
        '<td>Engaged, no menu: stomp both together and hold</td></tr>'
        '<tr><td>Firmware update</td>'
        '<td><strong>Bypassed only:</strong> press both together, hold '
        '~2 s</td></tr>'
        '</table>',
        "y", "Reference"))

    P.append(page(
        '<div class="note"><b>Together, not staggered</b>'
        'Both-footswitch gestures need both switches going down at the '
        'same time. Holding one for a second and then adding the other is '
        'the <em>save</em> gesture, and it will save instead.</div>'
        '<h2>What the LEDs mean</h2>'
        '<p class="small">Single colour, both of them: the left LED is '
        'always blue, the right always orange. Nothing ever changes an '
        'LED\'s colour, only whether it is off, solid or blinking.</p>'
        '<table>'
        '<tr><th>State</th><th>Left (blue)</th>'
        '<th>Right (orange)</th></tr>'
        '<tr><td>Bypassed</td><td>off</td><td>off</td></tr>'
        '<tr><td>Left slot engaged</td><td>solid</td><td>off</td></tr>'
        '<tr><td>Right slot engaged</td><td>off</td><td>solid</td></tr>'
        '<tr><td>Freeform engaged</td><td>solid</td><td>solid</td></tr>'
        '<tr><td>Menu 2 latched</td><td>off</td><td>blinking</td></tr>'
        '<tr><td>Menu 3 latched</td><td>blinking</td><td>off</td></tr>'
        '<tr><td>Saved</td><td>3 blinks</td><td>3 blinks</td></tr>'
        '<tr><td>Save refused</td><td colspan="2">one double-flicker, '
        'both</td></tr>'
        '<tr><td>Charging</td><td colspan="2">alternating, speeding up'
        '</td></tr>'
        '</table>',
        "y", "Reference"))

    # ---- troubleshooting ------------------------------------------------
    toc('10', 'When it misbehaves')
    P.append(page(
        '<h1><span class="num">10</span>When it misbehaves</h1>'
        '<h3>No scream, just my guitar</h3>'
        '<p>Check an LED is solid. Then <strong>knob 2 (Mix)</strong>: at '
        '7:00 you hear only dry signal. Then the <strong>gate</strong> '
        '(T3): on <em>high</em> with low-output pickups the voice may '
        'never open. Try the middle.</p>'
        '<h3>A knob does nothing</h3>'
        '<p>Working as designed. After every menu change and every recall '
        'the knobs are inert until moved. Nudge it further.</p>'
        '<h3>It chases the wrong note</h3>'
        '<p>Play one note at a time and mute what you are not using. Open '
        'strings ringing under a lead line are the usual culprit. Heavy '
        'drive in front makes tracking worse: put DBscreamZ earlier.</p>',
        "y", "Troubleshooting"))

    P.append(page(
        '<h3 style="margin-top:0">Both LEDs flickered, nothing saved</h3>'
        '<p>You were on Freeform. It has no slot. Flip T2 to Set&nbsp;1 or '
        'Set&nbsp;2 and save again.</p>'
        '<h3>I tried to save and got update mode</h3>'
        '<p>Or the reverse. The gestures differ by timing: save is hold '
        'one then add the other; update is both at once, from bypass.</p>'
        '<h3>It sounds thin and buzzy</h3>'
        '<p>Formant bandwidths set too wide will do that. The cards give '
        'the factory values: 7:55, 8:04 and 8:09.</p>'
        '<h3>The scream lags my playing</h3>'
        '<p>Check <strong>glide</strong> (knob 5). At 5:00 it takes 300 ms '
        'to reach each new note. At 7:00 it is instant.</p>',
        "y", "Troubleshooting"))

    # ---- specs ----------------------------------------------------------
    toc('11', 'Specifications')
    P.append(page(
        '<h1><span class="num">11</span>Specifications</h1>'
        '<table>'
        '<tr><td>Platform</td><td>Cleveland Music Co. Hothouse, Daisy '
        'Seed3, 125B enclosure</td></tr>'
        '<tr><td>Processing</td><td>48 kHz, 48-sample blocks, 480 MHz</td>'
        '</tr>'
        '<tr><td>Voice</td><td>FOF formant-grain synthesis, three '
        'formants, no noise sources</td></tr>'
        '<tr><td>Tracking</td><td>Cycfi Q bitstream autocorrelation, '
        'monophonic</td></tr>'
        '<tr><td>Bypass</td><td>Buffered, 10 ms crossfade on engage</td>'
        '</tr>'
        '<tr><td>Power</td><td>9 V DC, 2.1 mm barrel, centre negative</td>'
        '</tr>'
        '<tr><td>Memory</td><td>4 voice slots plus the global charge '
        'configuration, kept across power cycles</td></tr>'
        '</table>',
        "y", "Specifications"))

    P.append(page(
        '<h2 style="margin-top:0">Firmware update</h2>'
        '<p>From <strong>bypass</strong>, press both footswitches together '
        'and hold about 2 seconds. The pedal appears over USB [universal '
        'serial bus] in DFU [Device Firmware Upgrade] mode. '
        'Flash it with the Daisy Web Programmer or dfu-util. Press the '
        'switches together, not one and then the other.</p>'
        '<h2>Credits</h2>'
        '<p class="small">Formant voice synthesis derived from MonkSynth / '
        'Delay Lama. Pitch detection by cycfi/q, Boost Software License '
        '1.0. Hardware from clevelandmusicco/HothouseExamples, open source '
        'hardware under CC BY-SA 4.0. The voices are synthesised from '
        'measured formant coefficients; no audio is redistributed.</p>'
        '<p class="small">Personal, non-commercial build. The names here '
        'are ours and refer to nothing.</p>',
        "y", "Credits"))

    return P


def pages_with_toc():
    """Build once to find where each section lands, then again with the
    contents filled in. The contents page exists in both passes, so the
    numbers it prints are the numbers that get printed."""
    build_pages()
    rows = "".join(
        '<tr class="l%d"><td class="n">%s</td><td>%s</td>'
        '<td class="p">%d</td></tr>' % (level, num, title, pg)
        for num, title, level, pg in TOC)
    return build_pages(rows)


BROWSERS = ("google-chrome", "google-chrome-stable", "chromium",
            "chromium-browser", "chrome", "microsoft-edge")


def render_pdf(html_path, pdf_path, expected_pages):
    """Print the booklet through headless Chrome, then check the page
    count. A page whose content outgrows the 66 x 122 mm sheet does not
    fail loudly, it silently spills onto an extra sheet and drags the
    footer with it, so the count is the check that catches it."""
    exe = next((shutil.which(b) for b in BROWSERS if shutil.which(b)), None)
    if exe is None:
        sys.exit("no Chrome or Chromium on PATH (tried: %s)"
                 % ", ".join(BROWSERS))
    subprocess.run([exe, "--headless", "--disable-gpu", "--no-sandbox",
                    "--no-pdf-header-footer",
                    "--print-to-pdf=" + pdf_path, html_path],
                   check=True, capture_output=True)
    print("wrote %s (%.1f KB)" % (pdf_path, os.path.getsize(pdf_path) / 1024))

    if shutil.which("pdfinfo") is None:
        print("  pdfinfo not found: page count unverified")
        return
    info = subprocess.run(["pdfinfo", pdf_path], check=True,
                          capture_output=True, text=True).stdout
    got = {k.strip(): v.strip() for k, v in
           (ln.split(":", 1) for ln in info.splitlines() if ":" in ln)}
    pages, size = int(got.get("Pages", 0)), got.get("Page size", "?")
    print("  %d pages, %s" % (pages, size))
    if expected_pages is not None and pages != expected_pages:
        sys.exit("PAGE COUNT MISMATCH: %d sheets from %d pages. Some page "
                 "overflows its sheet; shorten it or split it."
                 % (pages, expected_pages))


def build_html():
    return ('<!DOCTYPE html>\n<html lang="en">\n<head>\n'
            '<meta charset="utf-8">\n'
            '<title>DBscreamZ &mdash; manual and preset field guide</title>\n'
            '<style>%s</style>\n</head>\n<body>\n%s\n</body>\n</html>\n'
            % (CSS, "\n".join(pages_with_toc())))


def scale_pdf(src, dst, paper="letter"):
    """Blow the booklet up to a full sheet: same pages, same layout, just
    bigger. Ghostscript scales the page contents to fit the media and
    keeps everything vector, so the type stays type."""
    if shutil.which("gs") is None:
        sys.exit("ghostscript (gs) not found; needed for the large edition")
    subprocess.run(["gs", "-q", "-dNOPAUSE", "-dBATCH", "-sDEVICE=pdfwrite",
                    "-sPAPERSIZE=" + paper, "-dFIXEDMEDIA", "-dPDFFitPage",
                    "-dAutoRotatePages=/None", "-o", dst, src], check=True)
    print("wrote %s (%.1f KB)" % (dst, os.path.getsize(dst) / 1024))
    if shutil.which("pdfinfo"):
        info = subprocess.run(["pdfinfo", dst], check=True,
                              capture_output=True, text=True).stdout
        for line in info.splitlines():
            if line.startswith(("Pages:", "Page size:")):
                print("  " + " ".join(line.split()))


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "manual", "index.html")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    html = build_html()
    with open(out, "w") as f:
        f.write(html)

    # Standalone cards, for anyone laying the booklet out elsewhere. Written
    # to manual/cards/ so the hand-tuned originals in manual/ stay put.
    cards = os.path.join(root, "manual", "cards")
    os.makedirs(cards, exist_ok=True)
    for n, ch in enumerate(CHARACTERS):
        svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %g %g" '
               'width="%gmm" height="%gmm"><style>%s</style>%s</svg>'
               % (VB_W, VB_H, VB_W, VB_H, SVG_TEXT_CSS,
                  card_face(ch).split(">", 1)[1].rsplit("</svg>", 1)[0]))
        with open(os.path.join(cards, "%d_%s.svg"
                               % (n + 1, ch["name"].lower())), "w") as f:
            f.write(svg)
    print("wrote %s/*.svg (%d cards)" % (cards, len(CHARACTERS)))

    n_pages = html.count('class="page"')
    print("wrote %s (%d pages, %.1f KB)" % (out, n_pages, len(html) / 1024.0))
    print(overlap_report())

    if "--pdf" in sys.argv or "--large" in sys.argv:
        render_pdf(out, os.path.join(root, "manual",
                                     "DBscreamZ-manual.pdf"), n_pages)

    if "--large" in sys.argv:
        scale_pdf(os.path.join(root, "manual", "DBscreamZ-manual.pdf"),
                  os.path.join(root, "manual",
                               "DBscreamZ-manual-letter.pdf"))


if __name__ == "__main__":
    main()
