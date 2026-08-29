#!/usr/bin/env python3
"""Fail if the emulator's engine has drifted from the frozen v12 lab.

pedal/static/fof-processor.js is a VERBATIM copy of the file the deployed
v12 lab runs, because that file is the ear-approved reference the firmware
is validated against. The copy exists so the lab directory never has to be
touched; it is not a fork, and nothing should ever be edited in it.

This is the check that says so out loud.

    python3 tools/check_engine_copy.py

Exit status 0 when the copies match, 1 when they do not.
"""

import hashlib
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (copy, original) - both relative to the repo root.
PAIRS = [
    ('pedal/static/fof-processor.js', 'dbscreamz_lab/static/fof-processor.js'),
    ('pedal/static/presets.json',     'dbscreamz_lab/static/presets.json'),
]


def digest(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    bad = 0
    for copy, orig in PAIRS:
        c = os.path.join(ROOT, copy)
        o = os.path.join(ROOT, orig)
        for p in (c, o):
            if not os.path.isfile(p):
                print(f'MISSING  {p}')
                bad += 1
        if bad:
            continue
        dc, do = digest(c), digest(o)
        if dc == do:
            print(f'ok       {copy}  ({dc[:12]})')
        else:
            bad += 1
            print(f'DRIFTED  {copy}')
            print(f'         copy     {dc}')
            print(f'         original {do}')
            subprocess.run(['diff', '-u', o, c])

    if bad:
        print('\nThe emulator must run the same engine as the v12 lab.')
        print('Copy the original over the copy, or if the change belongs in')
        print('the engine, it belongs in the lab first and the whole')
        print('validation chain has to be re-run.')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
