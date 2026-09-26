#!/usr/bin/env python3
"""Count octave jumps in a per-block f0 trace (the `block,f0,amp` CSV that
firmware/host/render.cpp --trace and tools/ref_render.js write).

    python3 tools/octave_jumps.py trace.csv [--duration SECONDS]

Why this exists alongside compare_metrics.py: that script's "octave jump
rate" counts every block pair more than 0.45 of an octave apart, so it
also counts real melodic leaps of a tritone or more (its own comment says
so). That makes it a sanity bound, not a measure of the tracker's octave
ERRORS. This script counts only jumps that land on an octave:

  octave jump   two consecutive blocks whose f0 ratio is within 50 cents
                of 2 or of 1/2, i.e. | |1200*log2(b/a)| - 1200 | <= 50.
                A guitarist does play octaves, so this is still an upper
                bound on errors, but a much tighter one.
  octave blip   an octave jump that comes back: within 100 ms (37 blocks)
                the trace returns to within 50 cents of the pre-jump f0.
                Nobody plays an octave and back in under 100 ms on a DI
                of held notes and riffs, so a blip is as close to a pure
                tracker error as a trace alone can show.

Blocks with f0 <= 0 are skipped, matching compare_metrics.py. Duration
defaults to blocks * 128 / 48000.
"""

import argparse
import csv
import math

BLOCK_S = 128 / 48000
OCTAVE_TOL_CENTS = 50.0
BLIP_WINDOW_BLOCKS = 37  # 100 ms at 375 blocks/s


def load_f0(path):
    with open(path, newline="") as f:
        return [float(r["f0"]) for r in csv.DictReader(f)]


def cents(a, b):
    return 1200.0 * math.log2(b / a)


def is_octave(c):
    return abs(abs(c) - 1200.0) <= OCTAVE_TOL_CENTS


def count(f0):
    jumps = []  # block index of the block AFTER the jump
    changes = 0
    for i in range(1, len(f0)):
        a, b = f0[i - 1], f0[i]
        if a <= 0 or b <= 0:
            continue
        if a != b:
            changes += 1
        if is_octave(cents(a, b)):
            jumps.append(i)
    blips = 0
    for i in jumps:
        before = f0[i - 1]
        for j in range(i + 1, min(len(f0), i + 1 + BLIP_WINDOW_BLOCKS)):
            if f0[j] > 0 and abs(cents(before, f0[j])) <= OCTAVE_TOL_CENTS:
                blips += 1
                break
    return len(jumps), blips, changes


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("trace")
    ap.add_argument("--duration", type=float, default=None,
                    help="seconds (default: blocks * 128 / 48000)")
    args = ap.parse_args()
    f0 = load_f0(args.trace)
    dur = args.duration if args.duration else len(f0) * BLOCK_S
    jumps, blips, changes = count(f0)
    print(f"{args.trace}: octave jumps {jumps} ({jumps / dur:.3f}/s), "
          f"blips {blips} ({blips / dur:.3f}/s), f0 changes {changes} "
          f"over {dur:.2f} s")


if __name__ == "__main__":
    main()
