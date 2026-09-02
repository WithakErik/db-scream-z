#!/usr/bin/env python3
"""Assemble the public repo from an explicit allowlist, and only that.

This working tree holds things that must never be published: reference
audio, enclosure artwork, render artifacts, and the agent working notes.
Pushing this directory wholesale would leak them, and .gitignore alone is
a soft guarantee: one `git add -f`, one rule
edit, one new directory nobody thought about, and something escapes.

So publishing does not push this repo. It copies a named set of files
into a staging tree, re-scans that tree against a deny list, and refuses
to continue if anything matches. Nothing reaches the staging tree unless
a human wrote its path into MANIFEST below.

The published layout keeps the browser lab at the repo ROOT, because
GitHub Pages serves the site from there and the live demo depends on it.
Source directories sit alongside; Pages ignores them.

Usage:
  python3 tools/publish.py            build the staging tree and report
  python3 tools/publish.py --diff     also diff it against the remote
  python3 tools/publish.py --push     also commit and push  (asks first)
"""

import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGE = os.path.join(ROOT, ".publish")
REMOTE = "https://github.com/WithakErik/db-scream-z.git"

# --------------------------------------------------------------------------
# What gets published. dest <- src, both relative to the repo root.
# A directory entry copies the tree, minus anything the deny list catches.
# --------------------------------------------------------------------------
MANIFEST = [
    # --- GitHub Pages root: the pedal emulator, flattened. The live page is
    # served from here, so these paths must stay at the top level.
    #
    # dbscreamz_lab/ is deliberately NOT published. It is frozen at v12 as
    # the reference the firmware is validated against and as a raw parameter
    # surface for local work; the thing the public gets is the pedal.
    ("index.html",           "pedal/static/index.html"),
    ("app.js",               "pedal/static/app.js"),
    ("faceplate.js",         "pedal/static/faceplate.js"),
    ("ui-controller.js",     "pedal/static/ui-controller.js"),
    ("param-map.js",         "pedal/static/param-map.js"),
    ("knob-pickup.js",       "pedal/static/knob-pickup.js"),
    ("charge.js",            "pedal/static/charge.js"),
    ("voice-params.js",      "pedal/static/voice-params.js"),
    # app.js imports this at module scope, so leaving it out does not
    # degrade the page, it blanks it: the import 404s and nothing runs.
    ("beast-presets.js",     "pedal/static/beast-presets.js"),
    ("audio.js",             "pedal/static/audio.js"),
    ("fof-processor.js",     "pedal/static/fof-processor.js"),
    ("post-processor.js",    "pedal/static/post-processor.js"),
    # Only these three clips. The ref_*.wav files in the lab's audio
    # directory are reference audio and are never published; the deny list
    # enforces it.
    ("audio/guitar_intro.wav", "pedal/static/audio/guitar_intro.wav"),
    ("audio/guitar_long.wav",  "pedal/static/audio/guitar_long.wav"),
    ("audio/guitar_short.wav", "pedal/static/audio/guitar_short.wav"),

    # --- front matter
    ("README.md",            "README.md"),
    # The two pieces of artwork that ship, both character-free. They come
    # from different places on purpose: images/ holds the masters and is
    # otherwise gitignored, pedal/static/images/ holds what the emulator
    # loads when the demo is served locally.
    #
    # logo.png is published from the master because README.md renders it
    # from the repo root as well as the flattened page. faceplate.jpg has no
    # reader outside the panel, so it is published from the demo's copy and
    # its master (images/faceplate.jpeg, 3.1 MB) never leaves this machine.
    ("images/logo.png",      "images/logo.png"),
    ("images/faceplate.jpg", "pedal/static/images/faceplate.jpg"),

    # --- source worth publishing
    ("firmware/",            "firmware/"),
    ("tools/",               "tools/"),
    ("manual/",              "manual/"),
    ("docs/BOOKLET.md",      "docs/BOOKLET.md"),
    ("FIRMWARE.md",          "FIRMWARE.md"),
    ("PROTOTYPE.md",         "PROTOTYPE.md"),
    ("HANDOFF.md",           "HANDOFF.md"),

    # --- the hardware mirror already published (CC BY-SA, upstream)
    ("hardware/BOM.csv",     "hardware/hothouse/fabrication/BOM.csv"),
    ("hardware/CPL.csv",     "hardware/hothouse/fabrication/CPL.csv"),
    ("hardware/LICENSE",     "hardware/hothouse/LICENSE"),
    # CC BY-SA 4.0 requires the attribution travel with the files. This
    # was published by hand once and never added here, so the first
    # manifest-driven publish would have deleted it.
    ("hardware/ATTRIBUTION.md", "hardware/hothouse/ATTRIBUTION.md"),
    ("hardware/hothouse-drill-template.pdf",
     "hardware/hothouse/fabrication/hothouse-drill-template.pdf"),
    ("hardware/hothouse-no-branding.zip",
     "hardware/hothouse/fabrication/hothouse-no-branding.zip"),
    ("hardware/hothouse-io-no-branding.zip",
     "hardware/hothouse/fabrication/hothouse-io-no-branding.zip"),
    ("hardware/hothouse-switching-no-branding.zip",
     "hardware/hothouse/fabrication/hothouse-switching-no-branding.zip"),
]

# Written into the staging tree rather than copied.
GENERATED = {
    ".nojekyll": "",   # tells Pages to serve the files as they are
}

# --------------------------------------------------------------------------
# What must never appear in the staging tree, checked by path AFTER the copy.
# If any of these match, publishing aborts. Add to this list freely; a false
# positive costs one edit, a false negative is a leak.
# --------------------------------------------------------------------------
DENY = [
    (r"(^|/)artifacts/",        "render artifacts and grain dumps"),
    (r"(^|/)\.superpowers/",    "agent working notes"),
    (r"(^|/)docs/superpowers/", "agent plans and specs"),
    (r"(^|/)ref_[^/]*\.wav$",   "reference audio, never publish"),
    (r"(^|/)third_party/",      "vendored upstream sources"),
    (r"(^|/)build/",            "build output"),
    (r"(^|/)__pycache__/",      "python bytecode"),
    (r"(^|/)tests/bin/",        "compiled test binaries"),
    (r"\.(o|xcf|bundle)$",      "objects, layered artwork, git bundles"),
    (r"(^|/)\.git/",            "nested git metadata"),
    (r"(^|/)images/(?!(logo\.png|faceplate\.jpg)$)",
     "images/ is artwork; only logo.png and faceplate.jpg ship"),
    (r"(^|/)\.env",             "environment files"),
    (r"(^|/)names\.txt$",       "scratch notes"),
    # Milestone reports are working notes: verification runs, on-device test
    # plans and local paths. They stay on this machine.
    (r"(^|/)MILESTONE[^/]*\.md$", "milestone reports, local only"),
]

# Any .wav other than these three is a mistake.
WAV_ALLOW = {"audio/guitar_intro.wav", "audio/guitar_long.wav",
             "audio/guitar_short.wav"}


def rel_files(base):
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames[:] = [d for d in dirnames if d != ".git"]
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            yield os.path.relpath(full, base), full


def copy_tree(src, dst):
    for rel, full in rel_files(src):
        if any(re.search(p, rel.replace(os.sep, "/")) for p, _ in DENY):
            continue
        out = os.path.join(dst, rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copy2(full, out)


def build():
    if os.path.isdir(STAGE):
        shutil.rmtree(STAGE)
    os.makedirs(STAGE)
    missing = []
    for dest, src in MANIFEST:
        s = os.path.join(ROOT, src)
        d = os.path.join(STAGE, dest)
        if src.endswith("/"):
            if not os.path.isdir(s):
                missing.append(src)
                continue
            copy_tree(s, d)
        else:
            if not os.path.isfile(s):
                missing.append(src)
                continue
            os.makedirs(os.path.dirname(d) or STAGE, exist_ok=True)
            shutil.copy2(s, d)
    for name, body in GENERATED.items():
        with open(os.path.join(STAGE, name), "w") as f:
            f.write(body)
    return missing


def audit():
    """Re-scan the staging tree. This is the check that matters: it does
    not care what the manifest intended, only what is actually there."""
    problems = []
    for rel, _ in rel_files(STAGE):
        p = rel.replace(os.sep, "/")
        for pat, why in DENY:
            if re.search(pat, p):
                problems.append((p, why))
        if p.lower().endswith(".wav") and p not in WAV_ALLOW:
            problems.append((p, "unlisted audio file"))
    return problems


def main():
    missing = build()
    files = sorted(p for p, _ in rel_files(STAGE))
    total = sum(os.path.getsize(os.path.join(STAGE, f)) for f in files)

    print("staging tree: %s" % STAGE)
    print("  %d files, %.1f MB" % (len(files), total / 1e6))
    top = {}
    for f in files:
        key = f.split("/")[0] if "/" in f else "(root)"
        top[key] = top.get(key, 0) + 1
    for k in sorted(top):
        print("    %-14s %3d" % (k, top[k]))

    if missing:
        print("\nMISSING (in the manifest but not on disk):")
        for m in missing:
            print("  " + m)

    problems = audit()
    if problems:
        print("\nREFUSING TO PUBLISH. The staging tree contains:")
        for p, why in problems:
            print("  %-50s %s" % (p, why))
        sys.exit(1)
    print("\naudit clean: nothing matches the deny list")

    if "--diff" in sys.argv or "--push" in sys.argv:
        print("\nrun these to compare against the remote, then push:")
        print("  cd %s" % STAGE)
        print("  git init -q -b main && git remote add origin %s" % REMOTE)
        print("  git fetch -q origin && git add -A")
        print("  git diff --cached --name-status origin/main | grep '^D'")
        print("  git diff --cached --stat origin/main | tail -20")
        # The staging tree is a fresh init, so it shares no history with the
        # remote and a plain push is rejected as non-fast-forward. Re-parent
        # onto origin/main instead of force-pushing: --soft keeps the index,
        # so the next commit is an ordinary child carrying exactly this diff
        # and the published history survives. NEVER force-push here; a blob
        # pushed by mistake is not removable by one (2026-08-27).
        print("  git reset --soft origin/main")
        print("  git commit -m '...' && git push origin main")
        print("\nCheck the deletion list first: anything the manifest does")
        print("not name is deleted from the published repo, which is how")
        print("hardware/ATTRIBUTION.md nearly went.")
        print("\nNot run from here: pushing is yours to trigger.")


if __name__ == "__main__":
    main()
