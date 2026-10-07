#!/usr/bin/env bash
#
# Assemble the GitHub Pages site for the CI deploy.
#
#   tools/ci_site.sh SRC BIN OUT
#
# SRC  the checkout (CI: the public repo at the pushed commit)
# BIN  the firmware binary CI just built
# OUT  the directory to create (CI: _site), must not exist yet
#
# The site is every TRACKED file of SRC, which is exactly what the old
# branch deploy served, minus .github/. Untracked files, including the
# fetched dependencies and every build directory, never reach it. Then
# firmware/latest/ gets the binary and version.json, which flash.html
# reads (pedal/static/flash-core.js loadRelease).
#
# Tested by tools/tests/ci_site.test.sh.

set -euo pipefail

if [ $# -ne 3 ]; then
  echo "usage: tools/ci_site.sh SRC BIN OUT"
  exit 1
fi
SRC=$1 BIN=$2 OUT=$3
MAX=131072   # STM32H750 internal flash

if [ ! -f "$BIN" ]; then
  echo "FAIL: no binary at $BIN"
  exit 1
fi
size=$(wc -c < "$BIN" | tr -d ' ')
if [ "$size" -eq 0 ]; then
  echo "FAIL: $BIN is empty"
  exit 1
fi
if [ "$size" -gt "$MAX" ]; then
  echo "FAIL: $BIN is $size bytes, over the $MAX bytes of internal flash"
  exit 1
fi
if [ -e "$OUT" ]; then
  echo "FAIL: $OUT already exists"
  exit 1
fi

commit=$(git -C "$SRC" rev-parse HEAD)
date=$(git -C "$SRC" log -1 --format=%cI)
sha=$(sha256sum "$BIN" | cut -d' ' -f1)

mkdir -p "$OUT"
git -C "$SRC" archive --format=tar HEAD | tar -x -C "$OUT"
rm -rf "$OUT/.github"

mkdir -p "$OUT/firmware/latest"
cp "$BIN" "$OUT/firmware/latest/dbscreamz_pedal.bin"
printf '{"commit": "%s", "date": "%s", "size": %s, "sha256": "%s"}\n' \
  "$commit" "$date" "$size" "$sha" > "$OUT/firmware/latest/version.json"

echo "site in $OUT: firmware ${commit:0:7}, $size bytes, sha256 $sha"
