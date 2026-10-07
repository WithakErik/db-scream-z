#!/usr/bin/env bash
# ci_site.test.sh - tools/ci_site.sh against a throwaway git repo and a
# synthetic binary. Never builds firmware.
#
#   tools/tests/ci_site.test.sh

set -u
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CI_SITE="$HERE/../ci_site.sh"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0
check() {  # check DESCRIPTION COMMAND...
  local what=$1; shift
  if "$@"; then echo "ok   $what"; else echo "FAIL $what"; fails=$((fails + 1)); fi
}

# A source repo shaped like the published one.
SRC=$T/src
mkdir -p "$SRC/.github/workflows" "$SRC/firmware/hothouse/build" "$SRC/vendor"
echo '<!doctype html>' > "$SRC/index.html"
echo 'x' > "$SRC/flash.html"
echo 'x' > "$SRC/vendor/dfu.js"
echo '' > "$SRC/.nojekyll"
echo 'name: x' > "$SRC/.github/workflows/pages.yml"
echo 'src' > "$SRC/firmware/hothouse/main.cpp"
git -C "$SRC" init -q
git -C "$SRC" add index.html flash.html vendor/dfu.js .nojekyll \
  .github/workflows/pages.yml firmware/hothouse/main.cpp
git -C "$SRC" -c user.email=t@t -c user.name=t commit -q -m one
echo 'untracked' > "$SRC/firmware/hothouse/build/stale.o"
COMMIT=$(git -C "$SRC" rev-parse HEAD)

BIN=$T/fw.bin
head -c 5000 /dev/urandom > "$BIN"

OUT=$T/site
"$CI_SITE" "$SRC" "$BIN" "$OUT" > "$T/log" 2>&1
check "exits 0 on a good binary" test $? -eq 0
check "tracked files are copied" test -f "$OUT/index.html" -a -f "$OUT/vendor/dfu.js" -a -f "$OUT/firmware/hothouse/main.cpp"
check ".nojekyll is kept" test -f "$OUT/.nojekyll"
check ".github is not published" test ! -e "$OUT/.github"
check "untracked build output is not published" test ! -e "$OUT/firmware/hothouse/build"
check "binary copied byte for byte" cmp -s "$BIN" "$OUT/firmware/latest/dbscreamz_pedal.bin"

J=$OUT/firmware/latest/version.json
field() { python3 -c "import json,sys; print(json.load(open('$J'))['$1'])"; }
check "version.json commit" test "$(field commit)" = "$COMMIT"
check "version.json size" test "$(field size)" = "5000"
check "version.json sha256" test "$(field sha256)" = "$(sha256sum "$BIN" | cut -d' ' -f1)"
check "version.json date is the commit date" test "$(field date)" = "$(git -C "$SRC" log -1 --format=%cI)"
check "size is an integer in the JSON" python3 -c "import json; assert isinstance(json.load(open('$J'))['size'], int)"

for bad in empty big missing; do
  case $bad in
    empty) : > "$T/bad.bin" ;;
    big) head -c 131073 /dev/zero > "$T/bad.bin" ;;
    missing) rm -f "$T/bad.bin" ;;
  esac
  rm -rf "$T/out2"
  "$CI_SITE" "$SRC" "$T/bad.bin" "$T/out2" > /dev/null 2>&1
  rc=$?
  check "$bad binary is refused" test $rc -ne 0
  check "$bad binary writes no site" test ! -e "$T/out2"
done

head -c 131072 /dev/zero > "$T/max.bin"
rm -rf "$T/out3"
"$CI_SITE" "$SRC" "$T/max.bin" "$T/out3" > /dev/null 2>&1
check "exactly 128 KB is accepted" test $? -eq 0

echo
if [ "$fails" -eq 0 ]; then echo "ci_site: all passed"; else echo "ci_site: $fails failed"; exit 1; fi
