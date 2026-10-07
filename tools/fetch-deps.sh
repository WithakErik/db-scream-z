#!/usr/bin/env bash
#
# Clone the firmware's pinned dependencies into firmware/third_party/.
#
#   tools/fetch-deps.sh          fill firmware/third_party/ of this checkout
#   tools/fetch-deps.sh DIR      fill DIR instead
#
# This file is the single source of truth for the pins. The CI build
# (.github/workflows/pages.yml) runs it, so moving a pin here is all it
# takes to move the published firmware. firmware/third_party/PIN.txt (local
# notes, not published) keeps the notes on why each pin is where it is.
#
# A dependency already present at its pin is left exactly as it is,
# including any local build output. One present at a DIFFERENT commit is an
# error: a local clone may carry deliberate state, so this never re-checks
# anything out on its own. A clone interrupted by a failed fetch or submodule
# update leaves only NAME.partial; the next run discards it.

set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST=${1:-$REPO/firmware/third_party}

# libDaisy: the seed3-updates branch (open PR #710); Seed3 support is not
# on master. See PIN.txt.
LIBDAISY_URL=https://github.com/electro-smith/libDaisy.git
LIBDAISY_PIN=e1f740a5a7fe38e6d6d41281bf99fe7a2bb3827b

# HothouseExamples: only src/hothouse.{h,cpp} is used, so no submodules.
HOTHOUSE_URL=https://github.com/clevelandmusicco/HothouseExamples.git
HOTHOUSE_PIN=ff2b5ac0874fd03557eece62dbb3c784562cb7ee

# cycfi/q and the infra submodule its tree records at that commit.
Q_URL=https://github.com/cycfi/q.git
Q_PIN=6816b1a9df194c724e25463e1dfadd552a8fd9d7
INFRA_PIN=2dff97a4b107eced78e426152f5001a2331cb1cf

# present NAME PIN: 0 if DEST/NAME is already at PIN, 1 if absent, exits on
# a mismatch.
present() {
  local dir=$DEST/$1 pin=$2 have
  [ -e "$dir" ] || return 1
  have=$(git -C "$dir" rev-parse HEAD 2>/dev/null || echo "not-a-git-checkout")
  if [ "$have" = "$pin" ]; then
    echo "ok    $1 already at $pin"
    return 0
  fi
  echo "FAIL: $dir is at $have but the pin is $pin."
  echo "Move it aside or check out the pin by hand; this script will not."
  exit 1
}

# clone NAME URL PIN [SUBMODULE_ARGS...]: shallow clone of exactly PIN, detached,
# with optional submodule update. Works atomically in NAME.partial; moves to
# NAME only after all steps succeed. Remaining args passed as "git -C partial [ARGS]".
clone() {
  local name=$1 url=$2 pin=$3
  local partial=$DEST/$name.partial

  # Clean up any stale partial from a previous failed run
  rm -rf "$partial"

  echo "fetch $name at $pin"
  git init -q "$partial"
  git -C "$partial" remote add origin "$url"
  git -C "$partial" fetch -q --depth 1 origin "$pin"
  git -C "$partial" -c advice.detachedHead=false checkout -q FETCH_HEAD

  # Optional: run submodule update in .partial before moving to final location.
  # Remaining args are passed as git subcommands.
  shift 3
  if [ $# -gt 0 ]; then
    git -C "$partial" "$@"
  fi

  # Move into place only after everything succeeds
  mv "$partial" "$DEST/$name"
}

mkdir -p "$DEST"

if ! present libDaisy "$LIBDAISY_PIN"; then
  clone libDaisy "$LIBDAISY_URL" "$LIBDAISY_PIN" submodule update -q --init --depth 1
fi

if ! present HothouseExamples "$HOTHOUSE_PIN"; then
  clone HothouseExamples "$HOTHOUSE_URL" "$HOTHOUSE_PIN"
fi

if ! present q "$Q_PIN"; then
  # infra only, not recursive: its own nested submodules are not used by
  # the pitch detector path (PIN.txt).
  clone q "$Q_URL" "$Q_PIN" submodule update -q --init --depth 1 infra
fi
have=$(git -C "$DEST/q/infra" rev-parse HEAD)
if [ "$have" != "$INFRA_PIN" ]; then
  echo "FAIL: q/infra is at $have but the pin is $INFRA_PIN."
  exit 1
fi
echo "ok    q/infra at $INFRA_PIN"

echo "all dependencies at their pins in $DEST"
