#!/usr/bin/env bash
#
# Build the DBscreamZ firmware and flash it to a Hothouse + Daisy Seed3
# over DFU [Device Firmware Upgrade].
#
#   tools/flash.sh              build, then flash
#   tools/flash.sh --build      build only, do not flash
#
# Put the Seed3 in DFU first: hold BOOT, tap RESET, release BOOT.
#
# Why this script exists rather than a documented command line: libDaisy's
# `program-dfu` target has NO build prerequisite. Run it on its own, or
# after a build that failed, and it silently flashes whatever .bin happens
# to be sitting in build/ from a previous session. This script deletes the
# binary BEFORE building, so a failed build leaves nothing to flash by
# accident, and it refuses to flash at all unless a board is actually in
# DFU mode.

set -u

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
FW="$REPO/firmware/hothouse"
BIN="$FW/build/dbscreamz_pedal.bin"

BUILD_ONLY=0
[ "${1:-}" = "--build" ] && BUILD_ONLY=1

# The toolchain. cycfi/q uses std::bit_cast, which is C++20 and landed in
# GCC 11, so the usual distro arm-none-eabi-gcc (10.x on Debian/Ubuntu)
# fails deep inside q/detail/table_lookup.hpp rather than up front.
# Override by exporting GCC_PATH, otherwise we look for a toolchain and
# fall back to whatever is on PATH if it is new enough.
gcc_major() {
  "$1/arm-none-eabi-g++" -dumpversion 2>/dev/null | cut -d. -f1
}

if [ -n "${GCC_PATH:-}" ]; then
  :
else
  for cand in "$HOME"/toolchains/arm-gnu-toolchain-*/bin; do
    if [ -x "$cand/arm-none-eabi-g++" ]; then GCC_PATH="$cand"; break; fi
  done
fi

if [ -z "${GCC_PATH:-}" ]; then
  sys=$(command -v arm-none-eabi-g++ 2>/dev/null)
  if [ -n "$sys" ]; then GCC_PATH=$(dirname "$sys"); fi
fi

if [ -z "${GCC_PATH:-}" ] || [ ! -x "$GCC_PATH/arm-none-eabi-g++" ]; then
  echo "FAIL: no arm-none-eabi-g++ found."
  echo "Install an Arm GNU toolchain (GCC 11 or newer) and either put it on"
  echo "PATH or export GCC_PATH=/path/to/toolchain/bin"
  exit 1
fi

major=$(gcc_major "$GCC_PATH")
if [ -z "$major" ] || [ "$major" -lt 11 ]; then
  echo "FAIL: $GCC_PATH/arm-none-eabi-g++ is GCC ${major:-unknown}."
  echo "cycfi/q needs std::bit_cast, which requires GCC 11 or newer."
  echo "Export GCC_PATH at a newer toolchain's bin directory and re-run."
  exit 1
fi

cd "$FW" || { echo "FAIL: no $FW"; exit 1; }

echo "==> removing any previous binary, so a failed build cannot be flashed"
rm -f "$FW"/build/*.bin

echo "==> building with GCC $major from $GCC_PATH"
if ! make GCC_PATH="$GCC_PATH"; then
  echo
  echo "BUILD FAILED. Nothing was flashed and no .bin exists to flash."
  exit 1
fi

if [ ! -f "$BIN" ]; then
  echo
  echo "BUILD produced no $BIN. Not flashing."
  exit 1
fi

echo
echo "==> built $(wc -c < "$BIN") bytes"

if [ "$BUILD_ONLY" -eq 1 ]; then
  echo "--build given, stopping before the flash."
  exit 0
fi

echo "==> looking for a board in DFU mode"
if ! command -v dfu-util >/dev/null 2>&1; then
  echo
  echo "FAIL: dfu-util is not installed. The build succeeded; install"
  echo "dfu-util and re-run to flash."
  exit 1
fi

if ! dfu-util -l 2>/dev/null | grep -qi "0483:df11"; then
  echo
  echo "NO BOARD IN DFU MODE. Nothing was flashed."
  echo "On the Seed3: hold BOOT, tap RESET, release BOOT. Then re-run."
  echo "Check with: dfu-util -l | grep -i 0483    (you want 0483:df11)"
  echo "0483:4009 is the factory USB image, which means not DFU yet."
  exit 1
fi

echo "==> flashing"
make program-dfu
rc=$?

echo
if [ $rc -eq 0 ]; then
  echo "FLASH OK."
else
  echo "make exited $rc."
  echo "If 'File downloaded successfully' and 'Download done.' appear above,"
  echo "THE FLASH SUCCEEDED. dfu-util 0.9 reports an error here because the"
  echo ":leave option resets the board before its final status query, so it"
  echo "queries a device that has already gone. Judge the result by the byte"
  echo "count and 'Download done.', not by this exit code."
fi

echo
echo "The Seed will now disappear from lsusb. That is expected: this"
echo "firmware never initialises the USB device stack, so vanishing is"
echo "evidence the application is running, not that the board is dead."
echo "To get back to DFU at any time: hold BOOT, tap RESET, release BOOT."
echo
echo "Knob positions for every character are in docs/BOOKLET.md."
