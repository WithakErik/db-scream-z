#!/usr/bin/env python3
"""Generate manual/index.html: the DBscreamZ user manual and preset field guide.

Every behavioral claim traces to firmware/hothouse/ (control map in
main.cpp, gestures in ui_controller.hpp, pickup in knob_pickup.hpp, post
chain in post_chain.hpp, charge defaults in voice_params.hpp, chord mode's
control surface in chord_ui.hpp and chord_map.hpp).
Every preset number traces to docs/BOOKLET.md.

Regenerate with:  python3 tools/gen_manual.py

One run regenerates everything; there are no flags (old ones like --pdf
and --large are accepted and ignored). It writes:

  manual/index.html, manual/cards/*.svg
  manual/DBscreamZ-manual.pdf          the A6 manual (needs Chrome or
                                       Chromium on PATH)
  manual/DBscreamZ-manual-letter.pdf   the same blown up onto US Letter
                                       (needs ghostscript)
  manual-print/DBscreamZ-manual-booklet.pdf
  manual-print/DBscreamZ-manual-print.pdf
  manual-print/DBscreamZ-manual-print-letter.pdf
  manual-print/DBscreamZ-manual-print-booklet.pdf

manual-print/ (its PDFs and HTML) and manual/cards/ are emptied first, so
nothing stale from an earlier layout survives next to the fresh files.

The -print files are the print edition: the same manual with the
character cover art (images/DBscreamZ_Whole.png) as its cover. That
folder is ignored by version control and is NOT in tools/publish.py's
MANIFEST: the cover depicts the anime's characters and must never be
committed or published. The art is local-only, so a fresh worktree skips
the print edition with a warning; regenerate print files from the main
checkout.

The -booklet files impose the A6 manual for printing: two pages a side
on US Letter in saddle-stitch order, with cut lines out to the paper
edge. Printed double sided, portrait (the ordinary flip-like-a-book
setting), cut, stacked and folded in half, it is the book. Stdlib plus
Chrome and ghostscript; nothing to install with pip.
"""

import base64
import math
import os
import shutil
import subprocess
import sys

# --------------------------------------------------------------------------
# Geometry
# --------------------------------------------------------------------------
# Card geometry follows the hand-tuned SVGs in manual/*.svg, widened and
# made taller: the viewBox is in millimeters of enclosure face.
VB_W, VB_H = 60.0, 98.0
PRINT_W_MM = 52.0

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
TOG_MARK_DX = 6.0   # marker sits this far right of the toggle center
TOG_DOT_R = 1.2

# Menu identity, originally from the hand-tuned cards that used to live at
# manual/1_wukong.svg (deleted 2026-09-02 once this generator superseded
# them; see git history if the originals are ever wanted):
# color AND line style, so the menus stay separable in grayscale and for
# color-blind readers, and so coincident pointers interleave instead of
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
    """Every mark on one knob, as a pointer line out of the center.

    Menu 2 and menu 3 wear the color of the LED on the side you hold to
    reach them (right orange, left blue), so the card and the pedal agree.

    Each menu keeps its TRUE angle: nothing is merged or averaged. Menus
    are told apart by color and by line style, and the styles are chosen
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
    1: ("F1 bandwidth", "32.5 Hz", 9, "7:56"),
    2: ("F1 amount", "1.0", 50, "12:00"),
    4: ("F2 bandwidth", "47.5 Hz", 11, "8:05"),
    5: ("F2 amount", "1.0", 50, "12:00"),
}
MENU3_SHARED = {
    1: ("F3 bandwidth", "62.5 Hz", 12, "8:10"),
    2: ("F3 amount", "1.0", 50, "12:00"),
}

CHARACTERS = [
    {
        "name": "Wukong", "page": "Set 1", "side": "RIGHT",
        "tag": "The default shout",
        "flavor": "Mid-placed formants with a narrow detune. The "
                   "straightest read of the effect: loud, open, clean. "
                   "Start here.",
        "m2": {0: ("F1", "858.4 Hz", 55, "12:29"),
               3: ("F2", "1234 Hz", 35, "10:30")},
        "m3": {0: ("F3", "3111.7 Hz", 54, "12:22"),
               3: ("Vib", "0 Hz", 0, "7:00"),
               4: ("Depth", "0 cents", 0, "7:00"),
               5: ("Detune", "11 cents", 18, "8:50")},
    },
    {
        "name": "Prince", "page": "Set 1", "side": "LEFT",
        "tag": "Clenched teeth",
        "flavor": "Formants well below Wukong under a wider detune, so "
                   "it lands darker and "
                   "rougher. Reads as effort rather than power.",
        "m2": {0: ("F1", "741.6 Hz", 45, "11:31"),
               3: ("F2", "1066 Hz", 27, "9:42")},
        "m3": {0: ("F3", "2688.3 Hz", 40, "10:58"),
               3: ("Vib", "0 Hz", 0, "7:00"),
               4: ("Depth", "0 cents", 0, "7:00"),
               5: ("Detune", "18 cents", 30, "10:00")},
    },
    {
        "name": "Rice", "page": "Set 2", "side": "RIGHT",
        "tag": "Clean and glassy",
        "flavor": "The highest F3 of the eight over the narrowest "
                   "detune in the factory set (tied with Ki-Ki), barely "
                   "spread. Bright and hard-edged. Cuts without sounding "
                   "strained.",
        "m2": {0: ("F1", "1000 Hz", 67, "1:40"),
               3: ("F2", "1437.5 Hz", 45, "11:28")},
        "m3": {0: ("F3", "3625 Hz", 71, "2:05"),
               3: ("Vib", "0 Hz", 0, "7:00"),
               4: ("Depth", "0 cents", 0, "7:00"),
               5: ("Detune", "8 cents", 13, "8:20")},
    },
    {
        "name": "Flute", "page": "Set 2", "side": "LEFT",
        "tag": "Low growl",
        "flavor": "The lowest F2 of the eight, second only to Master on "
                   "F1 and F3, under a wide detune. "
                   "Thick and throaty rather than piercing.",
        "m2": {0: ("F1", "697.6 Hz", 41, "11:09"),
               3: ("F2", "1002.8 Hz", 24, "9:24")},
        "m3": {0: ("F3", "2528.8 Hz", 34, "10:26"),
               3: ("Vib", "0 Hz", 0, "7:00"),
               4: ("Depth", "0 cents", 0, "7:00"),
               5: ("Detune", "26 cents", 43, "11:20")},
    },
    {
        "name": "Master", "page": "Set 3", "side": "RIGHT",
        "tag": "Old and graveled",
        "flavor": "The lowest F1 and F3 of the eight, the widest "
                   "detune of any card. Dark, "
                   "frayed, faintly ridiculous.",
        "m2": {0: ("F1", "640 Hz", 37, "10:40"),
               3: ("F2", "1080 Hz", 28, "9:46")},
        "m3": {0: ("F3", "2400 Hz", 30, "10:00"),
               3: ("Vib", "0 Hz", 0, "7:00"),
               4: ("Depth", "0 cents", 0, "7:00"),
               5: ("Detune", "30 cents", 50, "12:00")},
    },
    {
        "name": "Ki-Ki", "page": "Set 3", "side": "LEFT",
        "tag": "Small and furious",
        "flavor": "The highest F2 of the eight and, with Rice, the "
                   "narrowest detune, all three formants "
                   "crowded high with nothing blunting them. Shrill, "
                   "nasal, annoyed.",
        "m2": {0: ("F1", "950 Hz", 62, "1:15"),
               3: ("F2", "1800 Hz", 62, "1:11")},
        "m3": {0: ("F3", "3350 Hz", 62, "1:10"),
               3: ("Vib", "0 Hz", 0, "7:00"),
               4: ("Depth", "0 cents", 0, "7:00"),
               5: ("Detune", "8 cents", 13, "8:20")},
    },
    {
        "name": "Boo", "page": None, "side": None,
        "tag": "Rubbery and hollow",
        "flavor": "A wide F1-to-F2 gap with F3 close above F2, thickened by "
                   "a wide detune. A big soft body "
                   "rather than an edge.",
        "m2": {0: ("F1", "900 Hz", 58, "12:50"),
               3: ("F2", "1750 Hz", 60, "12:57")},
        "m3": {0: ("F3", "2900 Hz", 47, "11:40"),
               3: ("Vib", "0 Hz", 0, "7:00"),
               4: ("Depth", "0 cents", 0, "7:00"),
               5: ("Detune", "24 cents", 40, "11:00")},
    },
    {
        "name": "Fling", "page": None, "side": None,
        "tag": "Light and quick",
        "flavor": "F1 and F3 close to Wukong's with F2 lifted well above, "
                   "under a modest detune so the edge stays on the "
                   "formants. The most usable of the eight under a band.",
        "m2": {0: ("F1", "880 Hz", 57, "12:40"),
               3: ("F2", "1600 Hz", 52, "12:14")},
        "m3": {0: ("F3", "3050 Hz", 52, "12:10"),
               3: ("Vib", "0 Hz", 0, "7:00"),
               4: ("Depth", "0 cents", 0, "7:00"),
               5: ("Detune", "14 cents", 23, "9:20")},
    },
]

# Charge mode: global, one setting for all voices (voice_params.hpp
# factory_charge_config). Toggle value mapping is uniform Up=2 / Middle=1 /
# Down=0. Factory (store v7, 2026-09-03): gain on, Birit Spomb, slow decay
# on menu 2; rise, brighter, full on menu 3. Keep in step with
# voice_params.hpp and test_charge.cpp.
CHARGE = [
    # (toggle, menu 2 role, m2 options up/mid/down, factory m2 position,
    #          menu 3 role, m3 options,             factory m3 position)
    ("T1", "Gain", ["Above 9000!", "on", "off"], "Middle",
           "Pitch", ["rise 2 octaves", "fall 2 octaves", "off"], "Up"),
    ("T2", "Charge time", ["Birit Spomb ~6 s", "Hamekameka ~2.5 s",
                           "punch ~0.75 s"], "Up",
           "Tone", ["brighter", "darker", "off"], "Up"),
    ("T3", "Decay", ["fast", "slow", "off (instant)"], "Middle",
           "Size", ["full", "half", "off"], "Up"),
]

TOG_ROWS = ["Up", "Middle", "Down"]

# Blank write-in cards after the eight voices, for voices of your own.
N_TEMPLATES = 2

# Chord mode (firmware/hothouse/chord_map.hpp, chord_ui.hpp; the factory
# values are voice_params.hpp factory_chord()). Two knob layers, no menu 3:
# the main layer (left tap engages, right held opens the mouth) and the
# chord menu (left hold ~1 s). Ranges are the ChordParams field comments;
# factory values are what factory_chord() writes. Each entry is (label,
# range, factory value, travel %, clock), the same last three columns as a
# character card. Range ends are always 7:00 and 5:00, so only the clock
# times in between are spelled out.
# Clock positions follow chord_map.hpp's tapers (7:00 = 0, 5:00 = 1):
# linear for the levels, vowels and resonance, cubic for sensitivity,
# attack and release, so 1.6 is 12:51 and 17 ms is 1:53.
CHORD_MAIN = [
    ("Vocal vol", "0 to 2x", "2.0x", 100, "5:00"),
    ("Mix", "dry to full voice", "full voice", 100, "5:00"),
    ("Master vol", "0 to 2x", "1.2x", 60, "1:00"),
    ("Tone", "dark to bright, flat at center (12:00)", "flat", 50, "12:00"),
    ("Sensitivity", "0, fixed closed vowel, to 8", "1.6", 58, "12:51"),
    ("Drive", "1x to 40x, log taper", "40x", 100, "5:00"),
]
CHORD_MENU = [
    ("Closed vowel", "oo, oh (9:30), ah (12:00), eh (2:30), ee", "oo",
     0, "7:00"),
    ("Open vowel", "oo, oh (9:30), ah (12:00), eh (2:30), ee", "ee",
     100, "5:00"),
    ("Vocal size", "formants at 100% to 50%, an octave down",
     "100% formants", 0, "7:00"),
    ("Resonance", "soft to sharp", "0.53", 53, "12:18"),
    ("Attack", "1 ms to 50 ms mouth attack", "17 ms", 69, "1:53"),
    ("Release", "20 ms to 500 ms mouth release", "20 ms", 0, "7:00"),
]


def char_menu(ch, menu):
    """The six (label, value, travel, clock) entries for one menu."""
    if menu == 1:
        return list(MENU1)
    shared = MENU2_SHARED if menu == 2 else MENU3_SHARED
    varying = ch["m2"] if menu == 2 else ch["m3"]
    return [varying.get(i) or shared[i] for i in range(6)]


def char_toggles(ch):
    """Menu 1 toggle positions for a card: (label, value, [positions]).

    T2 is not part of the voice, it only picks the page, so it marks the
    factory Set. A voice with no factory slot leaves T2 unmarked.
    """
    page = ["Up", "Middle", "Down"][SETS.index(ch["page"])] if ch["page"] \
        else None
    return [("Octave", "0", ["Middle"]),
            ("Memory page", ch["page"] or "any Set", [page] if page else []),
            ("Gate", "medium", ["Middle"])]


# --------------------------------------------------------------------------
# The pedal-face template
# --------------------------------------------------------------------------
DETENT_DY = {"Up": -4.0, "Middle": 0.0, "Down": 4.0}
SETS = ["Set 1", "Set 2", "Set 3"]


def svg_face(knob_marks, toggle_marks, dim_knobs=False, width_mm=PRINT_W_MM,
             blank=False, home_fs=None):
    """knob_marks: 6 lists of (menu, degrees). toggle_marks: 3 lists of
    (menu, 'Up'|'Middle'|'Down'). home_fs: 'LEFT'|'RIGHT' draws that
    footswitch with a heavy outline, the factory slot. blank=True is the write-in face for a
    voice of your own: an hour tick round every knob to draw a pointer
    against, and a hollow dot at every toggle position to fill in."""
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
        if blank:
            # 7:00 to 5:00 in hours: the eleven positions a clock names.
            for hour in range(11):
                deg = -150.0 + hour * 30.0
                x0, y0 = pt(cx, cy, KNOB_R + 0.9, deg)
                x1, y1 = pt(cx, cy, KNOB_R + 2.0, deg)
                s.append('<line x1="%.3f" y1="%.3f" x2="%.3f" y2="%.3f" '
                         'stroke="#888" stroke-width="%s"/>'
                         % (x0, y0, x1, y1, "0.4" if hour == 5 else "0.25"))
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
            # the colors say which menu, so no label is needed.
            s.append(pie(cx + TOG_MARK_DX, cy + DETENT_DY[row], TOG_DOT_R,
                         sorted(menus)))
        if blank:
            for dy in DETENT_DY.values():
                s.append('<circle cx="%.3f" cy="%.3f" r="%.2f" fill="#fff" '
                         'stroke="#666" stroke-width="0.25"/>'
                         % (cx + TOG_MARK_DX, cy + dy, TOG_DOT_R))

    for (cx, cy) in LED_XY:
        s.append('<circle cx="%g" cy="%g" r="%g" fill="#fff" stroke="#888" '
                 'stroke-width="0.35"/>' % (cx, cy, LED_R))
    for (cx, cy), lab in zip(FS_XY, ["L", "R"]):
        home = home_fs is not None and home_fs[0] == lab
        s.append('<circle cx="%g" cy="%g" r="%g" fill="#fafafa" '
                 'stroke="%s" stroke-width="%s"/>'
                 % (cx, cy, FS_R, "#000" if home else "#888",
                    "1.2" if home else "0.4"))
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
    return svg_face(knob_marks, toggle_marks, home_fs=ch["side"])


def charge_face():
    """Charge config lives on the toggles while a menu is latched, so the
    knobs are shown grayed: they do nothing for charge."""
    toggle_marks = []
    for (_t, _r2, _o2, pos2, _r3, _o3, pos3) in CHARGE:
        toggle_marks.append([(2, pos2), (3, pos3)])
    return svg_face([[] for _ in range(6)], toggle_marks, dim_knobs=True,
                    width_mm=48.0)


def svg_knob_demo(marks, width_mm=22.0):
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
/* The booklet is A6: 105 x 148 mm. Everything below is sized for an
   89 x 132 mm text block. */
@page { size: 105mm 148mm; margin: 8mm; }
* { box-sizing: border-box; }
html, body { margin: 0; padding: 0; background: #fff; color: #111; }
body {
  font-family: "Charter", "Bitstream Charter", "Georgia", serif;
  font-size: 8.6pt; line-height: 1.36;
}
.page {
  width: 89mm; min-height: 128mm; page-break-after: always;
  position: relative; padding-bottom: 6mm;
}
.page:last-child { page-break-after: auto; }
h1 { font-size: 13pt; margin: 0 0 3mm; letter-spacing: -0.2px;
     line-height: 1.1; border-bottom: 0.8pt solid #111;
     padding-bottom: 1mm; }
h1.ctr { text-align: center; }
h1 .num { color: #999; font-weight: normal; margin-right: 2mm; }
h2 { font-size: 9.6pt; margin: 4.5mm 0 2.4mm;
     border-bottom: 0.8pt solid #111; padding-bottom: 0.8mm; }
h3 { font-size: 8.8pt; margin: 3.2mm 0 1mm; }
p { margin: 0 0 2.2mm; }
ul, ol { margin: 0 0 2.2mm; padding-left: 4.5mm; }
li { margin-bottom: 1mm; }
.lede { font-size: 9.4pt; }
.small { font-size: 7.7pt; color: #444; }
strong { font-weight: 600; }

table { width: 100%; border-collapse: collapse; font-size: 7.7pt;
        margin: 1.2mm 0 2.4mm; }
th, td { border-bottom: 0.3pt solid #bbb; padding: 0.8mm 1mm;
         text-align: left; vertical-align: top; }
th { border-bottom: 0.6pt solid #111; font-weight: 600; }
tr:last-child td { border-bottom: 0.6pt solid #111; }

.spec td { padding: 0.4mm 1mm; line-height: 1.25; }
.note { border-left: 2pt solid #E65100; padding: 1.4mm 1.4mm 1.4mm 2.6mm;
        margin: 2.6mm 0; font-size: 7.9pt; background: #fdf5ef; }
.note b { display: block; text-transform: uppercase; letter-spacing: 0.3px;
          font-size: 7.2pt; margin-bottom: 0.6mm; }

.flow { display: block; margin: 2mm auto 3mm; max-width: 100%; }
svg text.fl { font-family: "DejaVu Sans", sans-serif; font-size: 2.6px;
              text-anchor: middle; fill: #222; }
svg text.flk { font-family: "DejaVu Sans", sans-serif; font-size: 2.2px;
               font-weight: bold; text-anchor: middle; fill: #6A1B9A; }

.foot { position: absolute; bottom: 0; left: 0; right: 0;
        font-size: 7.6pt; color: #888; border-top: 0.4pt solid #ddd;
        padding-top: 1.2mm; display: flex; justify-content: space-between; }

.cover { text-align: center; padding-top: 50mm; }
.cover .word { font-size: 40pt; letter-spacing: -0.5px; margin: 0; }
.cover .sub { font-size: 12pt; color: #444; margin-top: 2.5mm; }
/* Print edition only: the cover art runs to the trim, no margin. */
@page coverart { margin: 0; }
.page.coverart { page: coverart; width: 105mm; height: 148mm;
                 min-height: 148mm; padding: 0; overflow: hidden; }
.coverart img { display: block; width: 105mm; height: 148mm;
                object-fit: cover; }
.thanks { text-align: center; padding-top: 42mm; }
.thanks img { width: 66mm; }
.thanks p { font-size: 12pt; margin-top: 10mm; }
.divider { text-align: center; padding-top: 30mm; }
.divider img { width: 66mm; }
.divider p { margin-top: 8mm; }
.cover .rule { width: 46mm; height: 1.4pt; background: #111;
               margin: 10mm auto; }

/* character cards */
.cardhead { display: flex; align-items: baseline; gap: 2mm; flex-wrap: wrap;
            border-bottom: 0.8pt solid #111; padding-bottom: 0.8mm; }
.cardhead h1 { margin: 0; border: none; padding-bottom: 0; }
.tag { font-size: 8.6pt; font-style: italic; color: #555; }
h1 .tag { font-size: 8pt; }
.slot { font-size: 7.7pt; color: #444; margin: 1.2mm 0 1.8mm; }
.figure { margin-top: 1.8mm; }
.mtab { width: 100%; font-size: 7.2pt; border-collapse: collapse;
        margin-bottom: 1.2mm; line-height: 1.3; }
.mtab caption { text-align: left; font-size: 7.7pt; font-weight: 600;
                padding: 0.6mm 0 0.3mm; }
.mtab th, .mtab td { border-bottom: 0.3pt solid #ddd; padding: 0.2mm 0.8mm;
                     text-align: left; }
.mtab .clk { text-align: right; font-family: "DejaVu Sans Mono", monospace;
             white-space: nowrap; width: 1%; }
.mtab .k { color: #888; width: 5mm; }
.mrow { display: flex; align-items: baseline; }
.mrow > span { flex: 1 1 auto; }
.mrow > .swatch { flex: 0 0 auto; }
.swatch { display: inline-block; width: 2.6mm; height: 2.6mm;
          border: 0.3pt solid #111; vertical-align: -0.3mm;
          margin-right: 1.2mm; border-radius: 50%; }

.toc { width: 100%; border-collapse: collapse; font-size: 8.4pt; }
.toc td { border: none; padding: 0.5mm 0; vertical-align: baseline; }
.toc .n { width: 7mm; color: #999; }
.toc .p { text-align: right; width: 8mm; color: #666; }
.toc .l2 td { font-size: 7.9pt; color: #444; }
.toc .l2 td:nth-child(2) { padding-left: 5mm; }

.legend { display: flex; flex-direction: column; gap: 1.2mm;
          font-size: 7.9pt; margin: 1.8mm 0 2.4mm; }
.legend > div { display: flex; align-items: center; gap: 2mm; }
.demos { display: flex; gap: 3mm; margin: 2.4mm 0; }
.demos figure { margin: 0; flex: 1; }
.demos figcaption { font-size: 7pt; color: #444; margin-top: 0.8mm;
                    line-height: 1.25; }
/* The white halo keeps the knob number readable where a pointer crosses. */
svg text.kn { font-family: "DejaVu Sans", sans-serif; font-size: 2.6px;
              text-anchor: middle; fill: #777; stroke: #fff;
              stroke-width: 0.7; paint-order: stroke fill; }
svg text.fs { font-family: "DejaVu Sans", sans-serif; font-size: 4px;
              text-anchor: middle; fill: #999; }
.mtab.blank td { height: 4.4mm; vertical-align: bottom; }
.mtab.blank .w { border-bottom: 0.3pt solid #999; width: 26mm; }
.mtab.blank .w + .w { width: 18mm; border-left: 2mm solid #fff; }
.mtab.blank th { font-size: 6.6pt; color: #888; font-weight: normal;
                 border-bottom: none; padding: 0 0.8mm; }
.writein { border-bottom: 0.4pt solid #999; display: inline-block;
           min-width: 50mm; height: 4.6mm; }
.lines div { border-bottom: 0.3pt solid #bbb; height: 6mm; }
.face, .demo { display: block; margin: 0 auto; }

@media screen {
  body { background: #e8e8e8; padding: 6mm 0; }
  .page { background: #fff; margin: 0 auto 4mm; padding: 8mm;
          width: 105mm; min-height: 148mm;
          box-shadow: 0 0.6mm 2mm rgba(0,0,0,0.25); }
  .foot { left: 8mm; right: 8mm; bottom: 4mm; }
  .page.coverart { padding: 0; height: 148mm; }
}
"""


SVG_TEXT_CSS = (
    # The white halo keeps the knob number readable where a pointer line
    # crosses it.
    'text.kn{font-family:sans-serif;font-size:2.6px;text-anchor:middle;'
    'fill:#888;stroke:#fff;stroke-width:0.7;paint-order:stroke fill}'
    'text.fs{font-family:sans-serif;font-size:4px;text-anchor:middle;'
    'fill:#aaa}')


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
        '<td class="clk">%d%%</td><td class="clk">%s</td></tr>'
        % (i + 1, lab, val, trav, clk)
        for i, (lab, val, trav, clk) in enumerate(entries))
    return ('<table class="mtab"><caption>%s%s</caption>%s</table>'
            % (swatch(menu), title, rows))


def chord_table(entries):
    """A chord layer in the character cards' table style: the factory
    value with its travel and clock in their own columns. Ranges run from
    7:00 to 5:00."""
    rows = "".join(
        '<tr><td class="k">K%d</td><td>%s</td><td>%s</td><td>%s</td>'
        '<td class="clk">%d%%</td><td class="clk">%s</td></tr>'
        % (i + 1, lab, rng, fac, trav, clk)
        for i, (lab, rng, fac, trav, clk) in enumerate(entries))
    return ('<table class="mtab"><tr><th></th><th>Knob</th>'
            '<th>Range, 7:00 to 5:00</th><th>Factory</th>'
            '<th colspan="2" style="text-align:right">Position</th></tr>'
            '%s</table>' % rows)


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

    s = ['<svg class="flow" viewBox="0 0 %g %g" width="58mm" '
         'xmlns="http://www.w3.org/2000/svg" role="img">' % (W, H)]
    s.append('<defs><marker id="ar" viewBox="0 0 6 6" refX="5.4" refY="3" '
             'markerWidth="3.4" markerHeight="3.4" orient="auto">'
             '<path d="M 0 0 L 6 3 L 0 6 z" fill="#777"/></marker></defs>')
    s.append('<text x="%.1f" y="3.4" class="fl">guitar in</text>' % SPINE)
    s.append('<path d="M %.1f 4.6 V 12.6" stroke="#777" stroke-width="0.4" '
             'fill="none"/>' % SPINE)
    s.append(path('M %.1f 7.5 H 30.4' % SPINE))
    s.append(box(31, 4.5, 23, "dry, untouched"))

    # The gate does not sit in front of the tracker: the tracker hears the
    # raw input, and the gate's envelope scales the finished voice
    # (fof_engine.hpp: fe_ and pt_ both take x; out = raw * amp).
    chain = [("pitch track", None), ("voice synth", None), ("gate", "T3"),
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

def charge_row(t, role, options, factory_pos):
    """One table row; the factory position's cell is bold so the tables
    double as the defaults reference."""
    cells = []
    for opt, pos in zip(options, TOG_ROWS):
        cells.append('<td><strong>%s</strong></td>' % opt if pos == factory_pos
                     else '<td>%s</td>' % opt)
    return '<tr><td><strong>%s</strong><br>%s</td>%s</tr>' % (
        t, role, "".join(cells))


CHARGE_ROWS_2 = "".join(
    charge_row(t, role, o, p) for (t, role, o, p, _r3, _o3, _p3) in CHARGE)

CHARGE_ROWS_3 = "".join(
    charge_row(t, role3, o3, p3) for (t, _r, _o, _p, role3, o3, p3) in CHARGE)


def data_uri(path, mime="image/png"):
    with open(path, "rb") as f:
        return "data:%s;base64,%s" % (mime, base64.b64encode(f.read())
                                      .decode("ascii"))


def build_pages(toc_rows="", cover_art=None, logo=None):
    """cover_art: a data URI for the print edition's picture cover, or None
    for the plain type cover every published copy uses. logo: a data URI
    for the wordmark on the back page."""
    P = []
    TOC.clear()
    _page_no[0] = 0        # both passes must number identically

    # ---- cover ----------------------------------------------------------
    if cover_art:
        _page_no[0] += 1   # counts as a page, carries no folio or rule
        P.append('<div class="page coverart"><img src="%s" alt="">'
                 '</div>' % cover_art)
    else:
        P.append(page(
            '<div class="cover">'
            '<p class="word">DBscreamZ</p>'
            '<p class="sub">Pitch-tracking formant scream synthesizer</p>'
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
        '<p class="lede">Play a note. A synthesized voice screams it back '
        'at you, tracking your pitch as you bend, slide and vibrato.</p>'
        '<p>The voice is not a sample and not a filter draped over your '
        'guitar. It is built from scratch as you play: a pitch tracker '
        'follows your fundamental, and a bank of formant grains rebuilds a '
        'vowel at that pitch. Three formants, F1, F2 and F3 [formant 1, 2 '
        'and 3], decide which '
        'vowel and how bright it is.</p>'
        '<p>Your dry signal is never processed. It is passed through '
        'untouched and crossfaded against the voice at the end, so Mix at '
        '7:00 with Master at 12:00 is your guitar, exactly as it went '
        'in. Master sets the level of both.</p>',
        "y", "What this is"))

    P.append(page(
        '<h2 style="margin-top:0">Signal flow</h2>'
        + flow_svg() +
        '<p class="small">The gate watches your input envelope and '
        'decides when the voice may sound. It never gates your dry '
        'signal.</p>'
        '<p class="small">K1 to K6 [knob 1 to knob 6] are the knobs and '
        'T1 to T3 [toggle 1 to toggle 3] the toggles, numbered left to '
        'right.</p>',
        "y", "Signal flow"))

    # ---- quick start ----------------------------------------------------
    toc('2', 'First sound')
    P.append(page(
        '<h1><span class="num">2</span>First sound</h1>'
        '<ol>'
        '<li>Guitar into <strong>IN</strong>, amp into <strong>OUT</strong>, '
        '9&nbsp;V DC [direct current] into the barrel jack. The pedal boots '
        '<strong>bypassed</strong>, both LEDs off.</li>'
        '<li><strong>T2</strong> (the middle toggle) <strong>up</strong> '
        '(Set&nbsp;1). Put <strong>T1</strong> and <strong>T3</strong> in '
        'their <strong>middle</strong> position.</li>'
        '<li>Tap the <strong>RIGHT</strong> footswitch. The right LED goes '
        'solid: Wukong is engaged.</li>'
        '<li>Play single notes, cleanly, one at a time. The voice follows '
        'your pitch.</li>'
        '<li>Nothing screaming? Turn <strong>knob 2 (Mix)</strong> '
        'clockwise and set <strong>T3</strong> (the gate) to its '
        '<strong>middle</strong> position.'
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
        'this page (Prince). Move T2 to its <strong>middle</strong> '
        'position for Set&nbsp;2 (Rice right, Flute left) or '
        '<strong>down</strong> for Set&nbsp;3 (Master right, Ki-Ki '
        'left).</p>'
        '<p>Edits are live at once, and lost at power off or when you tap '
        'a voice in, until you save (section 6).</p>',
        "y", "Quick start"))

    # ---- control surface ------------------------------------------------
    blank = svg_face([[] for _ in range(6)], [[] for _ in range(3)],
                     width_mm=50.0)
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
        '<p>Knobs run <strong>7:00</strong> fully counterclockwise, through '
        '<strong>12:00</strong> at center, to <strong>5:00</strong> fully '
        'clockwise. Every position in this booklet is a clock face.</p>'
        '<h2>What the knobs do</h2>'
        '<table><tr><th></th><th>Menu 1</th><th>Menu 2</th>'
        '<th>Menu 3</th></tr>' + KNOB_ROWS + '</table>'
        '<p class="small">Menu 3: <strong>K4</strong> (vibrato rate) is '
        'cube-tapered over 0&nbsp;Hz (7:00) to 50&nbsp;Hz (5:00), so '
        '6.25&nbsp;Hz sits at the center (<strong>12:00</strong>) and the musical singer\'s-vibrato range does not get '
        'crammed into the bottom of the travel. Every character ships '
        'with vibrato off, both <strong>K4</strong> and <strong>K5'
        '</strong> fully counterclockwise.</p>',
        "y", "The controls"))

    P.append(page(
        '<h2 style="margin-top:0">What the menu 1 knobs do</h2>'
        '<p><strong>Vocal vol</strong> (K1) is how loud the synthesized '
        'voice is on its own. <strong>Mix</strong> (K2) crossfades it '
        'against your untouched dry signal: 7:00 is the guitar alone, '
        '5:00 is the voice alone. <strong>Master</strong> (K3) is the '
        'level of the pair leaving the pedal. Vocal vol and Master are '
        'unity (12:00); Mix at 12:00 is an even blend.</p>'
        '<p><strong>Tone</strong> (K4) tilts the <em>voice</em> dark '
        'below 12:00 and bright above it, and never touches the dry. '
        'Anywhere from about 11:30 to 12:30 it is exactly flat.</p>'
        '<p><strong>Glide</strong> (K5) is how slowly the voice slides to '
        'each new note, 0&nbsp;ms (7:00) to 300&nbsp;ms (5:00). It is a '
        'slide that eases in: at 300&nbsp;ms (5:00) the voice is most of the way there after 300 ms and settles '
        'within about a second. The taper is steep: the first two thirds '
        'of the travel stays under 90&nbsp;ms (1:42), so the whole usable range of '
        'slurs sits below 2:00.</p>'
        '<p><strong>Vocal size</strong> (K6) scales all three formants '
        'together, which is acoustically vocal tract length: the same '
        'character shouting the same vowel out of a physically bigger '
        'body. At <strong>7:00</strong> it is the character exactly as '
        'written, which is what every card in section 11 assumes. At '
        '<strong>5:00</strong> every formant is halved, the deepest it '
        'goes.</p>',
        "y", "The controls"))

    P.append(page(
        '<h2 style="margin-top:0">What the toggles do</h2>'
        '<table><tr><th></th><th>Up</th><th>Middle</th><th>Down</th></tr>'
        '<tr><td><strong>T1</strong><br>Octave</td><td>+1</td><td>0</td>'
        '<td>&minus;1</td></tr>'
        '<tr><td><strong>T2</strong><br>Page</td><td>Set 1</td>'
        '<td>Set 2</td><td>Set 3</td></tr>'
        '<tr><td><strong>T3</strong><br>Gate</td><td>high</td>'
        '<td>medium</td><td>low</td></tr></table>'
        '<p>That is with no menu latched. While a menu <em>is</em> latched '
        'the toggles configure Charge mode instead, and nothing else. See '
        'section 7.</p>'
        '<p class="small">A toggle acts when you <em>move</em> it. A '
        'voice you tap in brings its own saved octave and gate, whatever '
        'the switches say, until you move one.</p>'
        '<p class="small">The gate decides how loud you must play before '
        'the voice speaks. High needs a firm attack and stays quiet '
        'between notes; low lets quiet playing through and hangs on '
        'longer.</p>',
        "y", "The controls"))

    # ---- reference ------------------------------------------------------
    toc('4', 'Every gesture')
    P.append(page(
        '<h1><span class="num">4</span>Every gesture</h1>'
        '<table>'
        '<tr><th>To do this</th><th>Do that</th></tr>'
        '<tr><td>Engage</td><td>Tap either footswitch</td></tr>'
        '<tr><td>Bypass</td><td>Tap the lit side again</td></tr>'
        '<tr><td>Switch voice</td><td>Tap the other side</td></tr>'
        '<tr><td>Latch menu 2<br>(F1, F2)</td>'
        '<td>Hold RIGHT ~1 s: it latches while you hold</td></tr>'
        '<tr><td>Latch menu 3<br>(F3, vibrato, detune)</td>'
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
        'Charge needs both switches going down at the same time. Holding '
        'one for a second and then adding the other is the <em>save</em> '
        'gesture. Bypassed, any 2 s with both down is update mode, and '
        'a stagger of a second or more also saves the held side.</div>'
        '<h2>What the LEDs mean</h2>'
        '<p class="small">Single color, both of them: the left LED is '
        'always blue, the right always orange. Nothing ever changes an '
        'LED\'s color, only whether it is off, solid or blinking.</p>'
        '<table>'
        '<tr><th>State</th><th>Left (blue)</th>'
        '<th>Right (orange)</th></tr>'
        '<tr><td>Bypassed</td><td>off</td><td>off</td></tr>'
        '<tr><td>Left slot engaged</td><td>solid</td><td>off</td></tr>'
        '<tr><td>Right slot engaged</td><td>off</td><td>solid</td></tr>'
        '<tr><td>Menu 2 latched</td><td>off</td><td>blinking</td></tr>'
        '<tr><td>Menu 3 latched</td><td>blinking</td><td>off</td></tr>'
        '<tr><td>Saved</td><td>3 blinks</td><td>3 blinks</td></tr>'
        '<tr><td>Charging</td><td colspan="2">alternating, speeding up as '
        'it builds, slowing as it winds down</td></tr>'
        '</table>',
        "y", "Reference"))

    # ---- menus ----------------------------------------------------------
    toc('5', 'The three menus')
    P.append(page(
        '<h1><span class="num">5</span>The three menus</h1>'
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
        'in it latches, while you are still holding: right LED blinks, '
        'left goes dark. Letting go changes nothing.</td></tr>'
        '<tr><td><div class="mrow">' + swatch(3) + '<span><strong>3</strong>'
        '</span></div></td>'
        '<td>Hold the <strong>LEFT</strong> footswitch the same way. '
        'Left LED blinks, right goes dark.</td></tr></table>'
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
        '<p><strong>Vibrato rate</strong> and <strong>Vibrato depth</strong> '
        'add a periodic wobble on top of the pitch you are already playing: '
        'rate is how fast it cycles, 0&nbsp;Hz (7:00) to 50&nbsp;Hz (5:00), '
        'and depth is how far it swings, 0 (7:00) to 100&nbsp;cents '
        '(5:00). Every character ships with both off (7:00).</p>'
        '<p><strong>Detune</strong> spreads the engine\'s three voices '
        'apart: the outer two move up to 60&nbsp;cents (5:00) either side '
        'of the '
        'middle one. Wider detune thickens and roughens the '
        'scream; at 0&nbsp;cents (7:00) the three collapse to the bare, '
        'focused character.</p>',
        "y", "The three menus"))

    # ---- memory ---------------------------------------------------------
    toc('6', 'Memory')
    P.append(page(
        '<h1><span class="num">6</span>Memory</h1>'
        '<p>T2 (the middle toggle) picks a <strong>page</strong>. Each page has '
        'two <strong>slots</strong>, one per footswitch. Six stored '
        'voices.</p>'
        '<table>'
        '<tr><th>T2</th><th>Page</th><th>Left</th><th>Right</th></tr>'
        '<tr><td>Up</td><td>Set 1</td><td>Prince</td><td>Wukong</td></tr>'
        '<tr><td>Mid</td><td>Set 2</td><td>Flute</td><td>Rice</td></tr>'
        '<tr><td>Down</td><td>Set 3</td><td>Ki-Ki</td><td>Master</td></tr>'
        '</table>'
        '<p class="small">Those six are what it ships with. Save over any '
        'of them: the recipes to dial them back are in section 11.</p>'
        '<h2>Saving</h2>'
        '<p>Everything you tweak is live but volatile. To save:</p>'
        '<ol>'
        '<li>Hold <strong>one</strong> footswitch past 1 second.</li>'
        '<li><strong>While still holding it</strong>, press the other.</li>'
        '</ol>'
        '<p>The <strong>held</strong> side picks the slot. Hold RIGHT and '
        'press LEFT and you have saved to the <strong>right</strong> slot '
        'of the current page. Both LEDs blink three times.</p>'
        '<p>Leave any latched menu before saving. Menus never save, and '
        'your edits survive the exit.</p>'
        '<p class="small">The held footswitch latches its own menu on the '
        'way past 1 second, the same as any hold. The second press drops '
        'that menu again and saves, so you finish where you began.</p>',
        "y", "Memory"))

    # ---- charge ---------------------------------------------------------
    toc('7', 'Charge mode')
    P.append(page(
        '<h1><span class="num">7</span>Charge mode</h1>'
        '<p>With a voice engaged and <strong>no menu latched</strong>, '
        'stomp <strong>both</strong> footswitches together and hold. The '
        'scream charges: gain swells, pitch sweeps, the voice grows, and '
        'the LEDs alternate faster and faster as it builds. Release and it '
        'winds down.</p>'
        '<p>At a <strong>full charge every setting you have switched on '
        'goes as far as its position takes it</strong>. For gain and size, '
        'up goes to the top of the range (gain all the way up, the voice at '
        'its deepest) and middle goes halfway from wherever the voice '
        'already sits to that top. Tone is a direction rather than '
        'an amount, so both of its positions go the whole way: darker is '
        'fully dark, brighter fully bright.</p>'
        '<p>Pitch glides toward two octaves away for '
        'as long as you hold: nearly there by the end of the slowest '
        'charge (Birit Spomb, ~6&nbsp;s), about one octave in by the end of '
        'the quickest (punch, ~0.75&nbsp;s). The two octaves count from '
        'wherever the octave switch sits, so a voice whose switch is '
        'already up rises three octaves above normal.</p>'
        '<p>It lasts exactly as long as you hold it. No latch, no '
        'timeout.</p>'
        '<p><strong>Engaged is the whole condition.</strong> The same '
        'two-stomp hold while <em>bypassed</em> is the firmware-update '
        'gesture instead, so charge can never reach it and it can never '
        'reach charge. See section 12.</p>'
        '<p class="small">Charge is configured on the '
        '<strong>toggles</strong>, while a menu is latched. Latch menu 2 '
        '(hold RIGHT) for gain, charge time and decay, menu 3 (hold LEFT) '
        'for pitch, tone and size. The knobs are not part of charge. The setting is '
        'stored when you leave the menu by tapping the blinking side; '
        'power off with a menu still latched and the change is lost.</p>',
        "y", "Charge mode"))

    P.append(page(
        '<h2 style="margin-top:0">Factory charge settings</h2>'
        '<div style="text-align:center">' + charge_face() + '</div>'
        '<p style="margin-top:1.5mm">Out of the box it is the full '
        'power-up. Orange (menu 2): gain on, Birit Spomb, slow decay. '
        'Blue (menu 3): pitch rising, brighter, full size. Each dot sits '
        'level with the position it wants; a half blue, half orange dot is '
        'both menus wanting the same one. The tables overleaf show the '
        'same in bold.</p>'
        '<p class="small">This configuration is <strong>global</strong>: '
        'one setting shared by every voice, never touched by saving or '
        'recalling. That is why it is not on the character cards.</p>',
        "y", "Charge mode"))

    P.append(page(
        '<h3 style="margin-top:0">' + swatch(2) + 'With menu 2 latched</h3>'
        '<table><tr><th></th><th>Up</th><th>Middle</th>'
        '<th>Down</th></tr>' + CHARGE_ROWS_2 + '</table>'
        '<h3>' + swatch(3) + 'With menu 3 latched</h3>'
        '<table><tr><th></th><th>Up</th><th>Middle</th>'
        '<th>Down</th></tr>' + CHARGE_ROWS_3 + '</table>'
        '<p class="small">Factory positions are marked in bold. A toggle '
        'sets charge when you <em>move</em> it, so to pick the position it '
        'already sits in, flick it away and back. For a charge that '
        '<em>falls</em> instead of rising, latch menu 3 and center T1; for '
        'a quicker build, latch menu 2 and center or drop T2. Your Set, '
        'octave and gate stay as they were; the switches no longer show '
        'them until you next move them with no menu latched.</p>',
        "y", "Charge mode"))

    # ---- chord mode -------------------------------------------------------
    toc('8', 'Chord mode')
    P.append(page(
        '<h1><span class="num">8</span>Chord mode</h1>'
        '<p class="lede">Hold both footswitches through power-up and the '
        'pedal spends that session as a single vowel filter driven '
        'straight off your guitar, chords included, instead of the six '
        'characters. There is no pitch tracker in this path: it cannot make '
        'an octave error, and at octave 0 it adds no delay. Toggle 1 shifts '
        'everything an octave down or up, and charge sweeps it two more.</p>'
        '<h2 style="margin-top:0">Entering</h2>'
        '<p>Hold both footswitches down and keep holding as you power the '
        'pedal on. Both LEDs flash three times to say chord mode is live. '
        'Power off and back on <strong>without</strong> holding the '
        'footswitches and the six characters are exactly as you left '
        'them.</p>'
        '<table><tr><th>Stomp</th><th>Does</th></tr>'
        '<tr><td>LEFT tap</td><td>Engage or bypass. In the chord menu, '
        'leaves it instead.</td></tr>'
        '<tr><td>LEFT hold ~1 s</td><td>Latch the chord menu. Left LED '
        'blinks. Tap LEFT again to leave.</td></tr>'
        '<tr><td>RIGHT, held</td><td>Open the mouth: right LED lit for as '
        'long as you hold it (dark in the chord menu). Momentary, never a '
        'menu, never a save.</td>'
        '</tr>'
        '<tr><td>Both together</td><td>Charge, engaged and outside the '
        'menu only: the same gain, pitch, tone and size overlay, set up in '
        'normal mode. Pitch sweeps two octaves from wherever toggle 1 '
        'sits.</td>'
        '</tr></table>',
        "y", "Chord mode"))

    P.append(page(
        '<h2 style="margin-top:0">Toggles and saving</h2>'
        '<p>Toggle 1 is the octave: up +1, middle 0, down -1, saved with '
        'the chord setting like the characters\' octave. Toggle 3 is the '
        'gate, same high/medium/low as the characters. Toggle 2 does '
        'nothing in chord mode. While shifted there is a slight delay '
        '(about 20 ms) and a little grain, more the further you go; at '
        'octave 0 there is none.</p>'
        '<p>Chord mode has one setting, not six slots, and it saves '
        'itself: a few seconds after you stop turning a knob, or at once '
        'when you leave the chord menu, and only when something actually '
        'changed. There is nothing to press. It lives in its own corner of '
        'memory; your saved characters are never touched by it.</p>'
        '<h2>The two knob layers</h2>'
        '<table><tr><th></th><th>Main (default)</th><th>Chord menu '
        '(hold LEFT)</th></tr>' +
        "".join('<tr><td><strong>K%d</strong></td><td>%s</td><td>%s</td>'
                '</tr>' % (i + 1, CHORD_MAIN[i][0], CHORD_MENU[i][0])
                for i in range(6)) +
        '</table>'
        '<p class="small">The vowel knobs each pick one of five vowels, '
        'oo, oh, ah, eh, ee. Play softly and you hear the closed vowel; '
        'the harder you pick, the further it moves toward the open vowel. '
        'Sensitivity (K5) sets how hard you must pick to reach it. Hold '
        'RIGHT and it goes straight to the open vowel.</p>',
        "y", "Chord mode"))

    P.append(page(
        '<h3 style="margin-top:0">Main layer, every position</h3>'
        + chord_table(CHORD_MAIN) +
        '<h3>Charge and the mouth</h3>'
        '<p>While holding RIGHT (mouth open), press LEFT as well to start '
        'a charge. The mouth closes while both are down. Let go of LEFT '
        'and the charge winds down, and the mouth opens again if RIGHT is '
        'still held.</p>'
        '<p>In the chord menu, a LEFT tap with RIGHT held does not count, '
        'so let go of RIGHT before tapping LEFT to leave the menu.</p>',
        "y", "Chord mode"))

    P.append(page(
        '<h3 style="margin-top:0">Chord menu layer, every position</h3>'
        + chord_table(CHORD_MENU) +
        '<p class="small">Factory gate: medium. Both layers and the gate '
        'are the pedal\'s one factory-chord setting: the values it ships '
        'with out of the box.</p>'
        '<p class="small">The LEDs: both flash three times on entry; left '
        'lit is on; right lit is the mouth open; left blinking is the '
        'chord menu; alternating is a charge.</p>',
        "y", "Chord mode"))

    # ---- troubleshooting ------------------------------------------------
    toc('9', 'When it misbehaves')
    P.append(page(
        '<h1><span class="num">9</span>When it misbehaves</h1>'
        '<h3>No scream, just my guitar</h3>'
        '<p>Check an LED is solid. Then <strong>knob 2 (Mix)</strong>: at '
        '7:00 you hear only dry signal. Then the <strong>gate</strong> '
        '(T3): on <em>high</em> with low-output pickups the voice may '
        'never open. Set T3 to its middle position.</p>'
        '<h3>A knob does nothing</h3>'
        '<p>Working as designed. After every menu change and every recall '
        'the knobs are inert until moved. Turn the knob a little further.</p>'
        '<h3>It chases the wrong note</h3>'
        '<p>Play one note at a time and mute what you are not using. Open '
        'strings ringing under a lead line are the usual culprit. Heavy '
        'drive in front makes tracking worse: put DBscreamZ before the drive in your chain.</p>'
        '<h3>I tried to save and got update mode</h3>'
        '<p>While bypassed, both footswitches down for 2 s starts update '
        'mode, even if one went down first. The save happens the moment '
        'the second goes down, so release both at once, or engage a voice '
        'first: update mode cannot start while one is engaged.</p>'
        '<h3>It sounds thin and buzzy</h3>'
        '<p>Formant bandwidths set too wide will do that. The cards give '
        'the factory values: F1 32.5&nbsp;Hz (7:56), F2 47.5&nbsp;Hz (8:05) '
        'and F3 62.5&nbsp;Hz (8:10).</p>'
        '<h3>The scream lags my playing</h3>'
        '<p>Check <strong>glide</strong> (knob 5). At 5:00 the voice takes '
        'most of a second to settle on each new note. At 7:00 it is '
        'instant.</p>',
        "y", "Troubleshooting"))

    # ---- the voices divider ---------------------------------------------
    # Opens the card half of the book. It also keeps every character card
    # on a left-hand page facing its table: drop it and the tables go
    # overleaf.
    P.append(page(
        '<div class="divider">'
        + ('<img src="%s" alt="DBscreamZ">' % logo if logo
           else '<p class="word">DBscreamZ</p>') +
        '<p>The rest of this book is the voices. Each of the eight '
        'characters is a card with every knob in all three menus and '
        'every toggle. Six come saved in the pedal, two to a Set; the '
        'other two are yours to dial in, then two blank cards '
        'for voices of your own.</p>'
        '</div>', "", ""))

    # ---- how to read a card ---------------------------------------------
    legend = "".join(
        '<div>%s <span><strong>%s</strong>, %s pointer<br>'
        '<span class="small">%s</span></span></div>'
        % (legend_mark(m), MENUS[m]["name"], MENUS[m]["style"],
           MENUS[m]["sub"]) for m in (1, 2, 3))
    toc('10', 'Reading a card')
    P.append(page(
        '<h1><span class="num">10</span>Reading a card</h1>'
        '<p>Each card is the pedal face with a pointer drawn out of every '
        'knob, one per menu, ending at the position that menu wants. '
        '<strong>Twenty-one positions:</strong> six knobs in each of three '
        'menus, plus three toggles. Set every one. None can be assumed: '
        'the knobs are wherever you left them.</p>'
        '<div class="legend">' + legend + '</div>'
        '<p class="small">Told apart by color <em>and</em> by line, so a '
        'photocopy still works. Menus 2 and 3 wear the color of the LED '
        'on the side you hold to reach them: orange for the right, blue '
        'for the left.</p>'
        '<h2>Toggles on a card</h2>'
        '<p>Toggle dots sit to the right of the switch, level with the '
        'position they want.</p>'
        '<p>Every character wants octave 0 and the medium gate: the '
        'middle position on T1 and T3.</p>'
        '<p>The T2 dot and the footswitch drawn with a heavier outline show '
        'the factory home: its Set and its slot. You can save a voice anywhere.</p>'
        '<p class="small">Two of the eight have no home and no T2 dot: '
        'dial them in and save them where you like. After them are two '
        'blank cards for voices of your own.</p>',
        "y", "Reading a card"))

    demo_a = [(m, clock_to_deg("12:00")) for m in (1, 2, 3)]
    demo_b = [(1, clock_to_deg("5:00")), (2, clock_to_deg("7:56")),
              (3, clock_to_deg("8:10"))]
    # Deliberately hardcoded, like demo_b above: NOT wired to CHARACTERS.
    # Vibrato shipped off for every character on 2026-09-02, which pins
    # every card's menu-3 knob-4/5 pointer to the 7:00 stop regardless of
    # what menu 1 or menu 2 want on that same knob, so no real knob on any
    # real card can show three genuinely close-but-distinct pointers any
    # more (the tightest non-identical triple in the actual data spans 29
    # clock-minutes, over double the 14 claimed here). Keep this a fabricated
    # worked example; wiring it back to a character's real values is what
    # broke this figure last time and will again the next time a knob's
    # factory value changes.
    demo_c = [(1, clock_to_deg("12:00")), (2, clock_to_deg("12:07")),
              (3, clock_to_deg("12:14"))]
    P.append(page(
        '<p style="margin-top:0">Every pointer sits at its exact position, '
        'including when menus agree. The line styles interleave, so where '
        'two or three menus want the same knob in the same place you get '
        'one pointer striped in their colors rather than three lines '
        'fighting over the same pixels.</p>'
        '<div class="demos">'
        '<figure>' + svg_knob_demo(demo_a) +
        '<figcaption><strong>All three agree.</strong> Knob 3 on every '
        'card: 12:00 in all three menus.</figcaption></figure>'
        '<figure>' + svg_knob_demo(demo_b) +
        '<figcaption><strong>One apart, two close.</strong> Knob 2 on every '
        'card.</figcaption></figure>'
        '<figure>' + svg_knob_demo(demo_c) +
        '<figcaption><strong>A tight cluster.</strong> A worked example, '
        'not any one card\'s knob: three positions inside 14 '
        'minutes.</figcaption></figure>'
        '</div>'
        '<div class="note"><b>The table is the exact word</b>'
        'Pointers are drawn true, but a knob is a blunt instrument and two '
        'pointers a few minutes apart look like one. Each card is followed '
        'by the same twenty-one positions as a table. When the drawing is '
        'ambiguous, read the table.</div>',
        "y", "Reading a card"))

    # ---- the eight characters -------------------------------------------
    for n, ch in enumerate(CHARACTERS):
        if n == 0:
            toc('11', 'The eight voices')
        toc('', ch["name"], level=2)
        slot = ("Factory: %s, %s slot" % (ch["page"], ch["side"])
                if ch["page"] else "No factory slot: save it to any page")
        P.append(page(
            '<div class="cardhead"><h1>%s</h1>'
            '<span class="tag">%s</span></div>'
            '<p class="slot">%s</p>'
            '<div class="figure">%s</div>'
            '<p style="margin-top:2mm">%s</p>'
            % (ch["name"], ch["tag"], slot, card_face(ch), ch["flavor"]),
            "y", ch["name"]))
        tabs = "".join(
            menu_table("Menu %d %s" % (m, ["", "(default)", "(hold RIGHT)",
                                           "(hold LEFT)"][m]),
                       m, char_menu(ch, m))
            for m in (1, 2, 3))
        trows = "".join(
            '<tr><td class="k">T%d</td><td>%s</td><td>%s</td>'
            '<td class="clk">%s</td></tr>'
            % (i + 1, lab, val, rows[0] if rows else "any")
            for i, (lab, val, rows) in enumerate(char_toggles(ch)))
        P.append(page(
            '<h2 style="margin:0 0 1mm">%s: every position</h2>'
            '%s<table class="mtab"><caption>%sToggles</caption>%s</table>'
            % (ch["name"], tabs, swatch(1), trows),
            "y", ch["name"]))

    # ---- your own voice ---------------------------------------------------
    # Same two pages as a character card, with everything left to fill in.
    # Labels come from the real menus, so they follow any change to them.
    for _ in range(N_TEMPLATES):
        toc('', 'Your own voice', level=2)
        P.append(page(
            '<div class="cardhead"><h1>Your own voice</h1></div>'
            '<p class="slot">Name <span class="writein"></span><br>'
            'Saved to Set <span class="writein" style="min-width:6mm">'
            '</span>, <span class="writein" style="min-width:10mm"></span>'
            ' slot</p>'
            '<div class="figure">%s</div>'
            '<p class="small" style="margin-top:1.5mm">Pointers: solid for '
            'menu 1, dashed 2, dotted 3. One dot per toggle.</p>'
            % svg_face([[] for _ in range(6)], [[] for _ in range(3)],
                       blank=True),
            "y", "Your own voice"))
        tabs = "".join(
            '<table class="mtab blank"><caption>%sMenu %d %s</caption>'
            '%s%s</table>'
            % (swatch(m), m, ["", "(default)", "(hold RIGHT)",
                              "(hold LEFT)"][m],
               '<tr><th></th><th></th><th>Value</th><th>Clock</th></tr>'
               if m == 1 else "",
               "".join('<tr><td class="k">K%d</td><td>%s</td>'
                       '<td class="w"></td><td class="w"></td></tr>'
                       % (i + 1, e[0])
                       for i, e in enumerate(char_menu(CHARACTERS[0], m))))
            for m in (1, 2, 3))
        trows = "".join(
            '<tr><td class="k">T%d</td><td>%s</td><td class="w"></td>'
            '<td class="w"></td></tr>' % (i + 1, lab)
            for i, (lab, _v, _r) in enumerate(char_toggles(CHARACTERS[0])))
        P.append(page(
            '<h2 style="margin:0 0 1mm">Your own voice: every position</h2>'
            '%s<table class="mtab blank"><caption>%sToggles</caption>%s'
            '</table>' % (tabs, swatch(1), trows),
            "y", "Your own voice"))

    # ---- specs ----------------------------------------------------------
    toc('12', 'Specifications')
    P.append(page(
        '<h1><span class="num">12</span>Specifications</h1>'
        '<table class="spec">'
        '<tr><td>Platform</td><td>Cleveland Music Co. Hothouse, Daisy '
        'Seed3, 125B enclosure</td></tr>'
        '<tr><td>Processing</td><td>48 kHz, 128-sample blocks (2.7 ms), '
        '480 MHz</td></tr>'
        '<tr><td>Voice</td><td>FOF [formant wave function] formant-grain '
        'synthesis, three '
        'formants</td></tr>'
        '<tr><td>Tracking</td><td>Cycfi Q bitstream autocorrelation, '
        'monophonic</td></tr>'
        '<tr><td>Bypass</td><td>Buffered (digital pass-through), 10 ms '
        'crossfade both ways</td>'
        '</tr>'
        '<tr><td>Power</td><td>9 V DC, 2.1 mm barrel, center negative</td>'
        '</tr>'
        '<tr><td>Memory</td><td>6 voice slots, the global charge '
        'configuration and the chord mode setting, kept across power '
        'cycles</td></tr>'
        '</table>'
        '<h2>Firmware update</h2>'
        '<p>From <strong>bypass</strong>, press both footswitches together '
        'and hold about 2 seconds. Press them at once: one held a second '
        'or more first also saves over that side\'s slot. The pedal '
        'appears over USB [universal serial bus] in DFU [Device Firmware '
        'Upgrade] mode. '
        # The URL may break only after its slash: a break at the hyphen in
        # db-scream-z reads as hyphenation, and "db-screamz" is a 404.
        'Flash it at <span style="white-space:nowrap">withakerik.github.io/'
        '</span><wbr><span style="white-space:nowrap">db-scream-z/'
        'flash.html</span> in '
        'Chrome or Edge, or with dfu-util.</p>'
        '<h2>Credits</h2>'
        '<p class="small">Formant voice synthesis derived from MonkSynth / '
        'Delay Lama. Pitch detection by cycfi/q, Boost Software License '
        '1.0. Hardware from clevelandmusicco/HothouseExamples, open source '
        'hardware under CC BY-SA 4.0 [Creative Commons Attribution-ShareAlike '
        '4.0]. Firmware: GPL-3.0 [General Public License, version 3]. '
        'The voices are synthesized from '
        'measured formant coefficients; no audio is redistributed.</p>'
        '<p class="small">Personal, non-commercial build. The names here '
        'are ours and refer to nothing.</p>',
        "y", "Specifications"))

    # ---- back page --------------------------------------------------------
    P.append(page(
        '<div class="thanks">'
        + ('<img src="%s" alt="DBscreamZ">' % logo if logo
           else '<p class="word">DBscreamZ</p>') +
        '<p>Thanks for the great idea Tucker!</p>'
        '</div>', "", ""))

    return P


def pages_with_toc(cover_art=None, logo=None):
    """Build once to find where each section lands, then again with the
    contents filled in. The contents page exists in both passes, so the
    numbers it prints are the numbers that get printed."""
    build_pages(cover_art=cover_art, logo=logo)
    rows = "".join(
        '<tr class="l%d"><td class="n">%s</td><td>%s</td>'
        '<td class="p">%d</td></tr>' % (level, num, title, pg)
        for num, title, level, pg in TOC)
    return build_pages(rows, cover_art=cover_art, logo=logo)


BROWSERS = ("google-chrome", "google-chrome-stable", "chromium",
            "chromium-browser", "chrome", "microsoft-edge")


def render_pdf(html_path, pdf_path, expected_pages):
    """Print the booklet through headless Chrome, then check the page
    count. A page whose content outgrows the A6 sheet does not
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


def build_html(cover_art=None, logo=None):
    return ('<!DOCTYPE html>\n<html lang="en">\n<head>\n'
            '<meta charset="utf-8">\n'
            '<title>DBscreamZ &mdash; manual and preset field guide</title>\n'
            '<style>%s</style>\n</head>\n<body>\n%s\n</body>\n</html>\n'
            % (CSS, "\n".join(pages_with_toc(cover_art, logo))))


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


def booklet_order(n):
    """Saddle-stitch imposition for n pages: a list of sheets, each
    ((front_left, front_right), (back_left, back_right)) as 0-based page
    indices into a run padded to a multiple of 4. Fold the stack in half
    and the pages read 1..n in order. Padding goes in before the last page,
    so the back page stays the outside back cover."""
    total = -(-n // 4) * 4
    sheets = []
    for i in range(total // 4):
        sheets.append(((total - 1 - 2 * i, 2 * i),
                       (2 * i + 1, total - 2 - 2 * i)))
    return total, sheets


# A US Letter sheet, read landscape, carries two A6 booklet pages side by
# side. The PDF pages themselves are portrait, with the spread turned on
# its side (see build_booklet_html).
SHEET_W, SHEET_H = 279.4, 215.9      # mm
PAGE_W, PAGE_H, PAGE_M = 105.0, 148.0, 8.0   # A6, and the @page margin
SPREAD_W, SPREAD_H = 2 * PAGE_W, PAGE_H
BLEED = 0.0      # cover art stops exactly at the trim, where the cut lines meet
                 # it: an overhang past the lines read as lines cutting into
                 # the page (rejected 2026-10-07). Keep it 0.

BOOKLET_CSS = """
@page { size: %(sh)smm %(sw)smm; margin: 0; }
.leaf { width: %(sh)smm; height: %(sw)smm; position: relative;
        overflow: hidden; page-break-after: always; }
.leaf:last-child { page-break-after: auto; }
.sheet { width: %(sw)smm; height: %(sh)smm; position: absolute;
         left: %(rot).3fmm; top: %(rotn).3fmm; overflow: hidden; }
.front .sheet { transform: rotate(90deg); }
.back .sheet { transform: rotate(-90deg); }
.spread { position: absolute; left: %(x0).3fmm; top: %(y0).3fmm;
          width: %(pw)smm; height: %(ph)smm; display: flex; }
.cell { width: %(cw)smm; height: %(ph)smm; padding: %(cm)smm;
        overflow: hidden; }
.cell .page { page-break-after: auto; }
.cell.bleed { padding: 0; overflow: visible; position: relative; }
.cell .page.coverart { page: auto; overflow: visible; }
.cell .coverart img { position: absolute; top: -%(bl)smm;
                      width: %(bw)smm; height: %(bh)smm; }
.cell.bleed.r .coverart img { left: 0; }
.cell.bleed.l .coverart img { right: 0; }
.cut { position: absolute; border: 0 solid #555; }
.cut.h { height: 0; border-top-width: 0.4pt; }
.cut.v { width: 0; border-left-width: 0.4pt; }
.cut.fold { border-style: dashed; border-color: #999; }
"""


def booklet_marks(bleed_l=False, bleed_r=False):
    """Cut lines along every trim edge and a dashed fold line at the
    spine, each running from the paper edge right up to where the printing
    starts: the trim on a plain page, the edge of the bleed on cover art
    (the lines never cross into a page). A lost few millimeters at the
    sheet edge (print shops do not print to the edge) still leaves a line
    to lay a ruler on, and each line still reaches its page once a
    neighbouring margin has been cut away. bleed_l and bleed_r say which
    page of the spread is full-bleed art."""
    x0, y0 = (SHEET_W - SPREAD_W) / 2, (SHEET_H - SPREAD_H) / 2
    x1, y1 = x0 + SPREAD_W, y0 + SPREAD_H
    edge = 0.3   # stop a hair inside the sheet: past it Chrome spills a page
    bl, br = BLEED * bleed_l, BLEED * bleed_r

    def h(y, xa, xb, cls=""):
        return ('<div class="cut h%s" style="top:%.2fmm;left:%.2fmm;'
                'width:%.2fmm"></div>' % (cls, y, xa, xb - xa))

    def v(x, ya, yb, cls=""):
        return ('<div class="cut v%s" style="left:%.2fmm;top:%.2fmm;'
                'height:%.2fmm"></div>' % (cls, x, ya, yb - ya))

    marks = []
    for y in (y0, y1):
        marks += [h(y, edge, x0 - bl), h(y, x1 + br, SHEET_W - edge)]
    for x, b in ((x0, bl), (x1, br)):
        marks += [v(x, edge, y0 - b), v(x, y1 + b, SHEET_H - edge)]
    mid, b = SHEET_W / 2, max(bl, br)
    marks += [v(mid, edge, y0 - b, " fold"),
              v(mid, y1 + b, SHEET_H - edge, " fold")]
    return "".join(marks)


def build_booklet_html(pages):
    """The manual imposed for a duplex printer: two A6 pages a side, sheet
    by sheet front then back, so printing the PDF double sided, cutting on
    the lines and folding the stack gives the book.

    Each landscape spread is set on a portrait page, fronts turned a
    quarter clockwise and backs a quarter counterclockwise. Printed portrait
    and double sided the paper turns over on its long edge, like any book,
    which is the one duplex setting every printer and print shop (FedEx
    Office's "double-sided" included) agrees on. Turning the backs the
    other way makes up for that flip being across the spine rather than
    along it, so each back lands behind its front the right way up."""
    total, sheets = booklet_order(len(pages))
    blank = '<div class="page"></div>'
    run = pages[:-1] + [blank] * (total - len(pages)) + pages[-1:]

    def cell(i, side):
        # Full-bleed art runs past the trim on the outer edges only: the
        # spine edge is a fold, and past it is the facing page.
        bleed = " bleed " + side if is_art(i) else ""
        return '<div class="cell%s">%s</div>' % (bleed, run[i])

    def is_art(i):
        return 'class="page coverart"' in run[i]

    sides = []
    for sheet in sheets:
        for face, side in zip(("front", "back"), sheet):
            marks = booklet_marks(is_art(side[0]), is_art(side[1]))
            sides.append('<div class="leaf %s"><div class="sheet">'
                         '<div class="spread">%s%s</div>%s</div></div>'
                         % (face, cell(side[0], "l"), cell(side[1], "r"),
                            marks))
    css = CSS + BOOKLET_CSS % dict(
        sw=SHEET_W, sh=SHEET_H, pw=SPREAD_W, ph=SPREAD_H, cw=PAGE_W,
        cm=PAGE_M, bl=BLEED, bw=PAGE_W + BLEED, bh=PAGE_H + 2 * BLEED,
        rot=(SHEET_H - SHEET_W) / 2, rotn=(SHEET_W - SHEET_H) / 2,
        x0=(SHEET_W - SPREAD_W) / 2, y0=(SHEET_H - SPREAD_H) / 2)
    return ('<!DOCTYPE html>\n<html lang="en">\n<head>\n'
            '<meta charset="utf-8">\n'
            '<title>DBscreamZ &mdash; manual, booklet imposition</title>\n'
            '<style>%s</style>\n</head>\n<body>\n%s\n</body>\n</html>\n'
            % (css, "\n".join(sides))), len(sides)


def write_booklet(pdir, stem, pages):
    """<stem>-booklet.pdf: the A6 pages imposed two up on Letter, with cut
    and fold lines."""
    html, n_sides = build_booklet_html(pages)
    src = os.path.join(pdir, "%s-booklet.html" % stem)
    with open(src, "w") as f:
        f.write(html)
    render_pdf(src, os.path.join(pdir, "%s-booklet.pdf" % stem), n_sides)
    os.remove(src)
    print("  print double sided, PORTRAIT, flip on long edge (the normal "
          "book setting), 100%% scale; %d sheets: cut on the solid lines, "
          "stack in order, fold on the dashed line" % (n_sides // 2))


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "manual", "index.html")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    # The wordmark is the one tracked, published image (images/logo.png).
    logo = data_uri(os.path.join(root, "images", "logo.png"))
    html = build_html(logo=logo)
    with open(out, "w") as f:
        f.write(html)

    # Standalone cards, for anyone laying the booklet out elsewhere. Written
    # to manual/cards/ so the hand-tuned originals in manual/ stay put.
    cards = os.path.join(root, "manual", "cards")
    os.makedirs(cards, exist_ok=True)
    for name in os.listdir(cards):   # a renamed character leaves no orphan
        if name.endswith(".svg"):
            os.remove(os.path.join(cards, name))
    for n, ch in enumerate(CHARACTERS):
        svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %g %g" '
               'width="%gmm" height="%gmm"><style>%s</style>%s</svg>'
               % (VB_W, VB_H, VB_W, VB_H, SVG_TEXT_CSS,
                  card_face(ch).split(">", 1)[1].rsplit("</svg>", 1)[0]))
        with open(os.path.join(cards, "%d_%s.svg"
                               % (n + 1, ch["name"].lower())), "w") as f:
            f.write(svg)
    print("wrote %s/*.svg (%d cards)" % (cards, len(CHARACTERS)))

    n_pages = html.count('class="page')
    print("wrote %s (%d pages, %.1f KB)" % (out, n_pages, len(html) / 1024.0))
    print(overlap_report())

    render_pdf(out, os.path.join(root, "manual", "DBscreamZ-manual.pdf"),
               n_pages)
    scale_pdf(os.path.join(root, "manual", "DBscreamZ-manual.pdf"),
              os.path.join(root, "manual", "DBscreamZ-manual-letter.pdf"))

    # Into the ignored manual-print/, not manual/: everything in manual/
    # is published, and the imposed sheets are for printing. Emptied first
    # so a file an older layout wrote cannot be mistaken for a fresh one.
    pdir = os.path.join(root, "manual-print")
    os.makedirs(pdir, exist_ok=True)
    for name in sorted(os.listdir(pdir)):
        if name.endswith((".pdf", ".html")):
            os.remove(os.path.join(pdir, name))
            print("removed old %s" % os.path.join(pdir, name))
    write_booklet(pdir, "DBscreamZ-manual", pages_with_toc(logo=logo))
    print_edition(root, pdir, logo)


def print_edition(root, pdir, logo):
    """The same manual behind the character cover art, written to the
    ignored manual-print/. Never published: see the module docstring."""
    art = os.path.join(root, "images", "DBscreamZ_Whole.png")
    if not os.path.exists(art):
        print("WARNING: print edition SKIPPED, %s is missing (local-only "
              "artwork, absent from a fresh worktree; run from the main "
              "checkout)" % art)
        return
    html = build_html(cover_art=data_uri(art), logo=logo)
    out = os.path.join(pdir, "index.html")
    with open(out, "w") as f:
        f.write(html)
    n_pages = html.count('class="page')
    print("wrote %s (%d pages, %.1f KB)" % (out, n_pages, len(html) / 1024.0))
    pdf = os.path.join(pdir, "DBscreamZ-manual-print.pdf")
    render_pdf(out, pdf, n_pages)
    scale_pdf(pdf, os.path.join(pdir, "DBscreamZ-manual-print-letter.pdf"))
    write_booklet(pdir, "DBscreamZ-manual-print",
                  pages_with_toc(data_uri(art), logo))


if __name__ == "__main__":
    main()
