#!/bin/bash
#
# GlideMix: the engine's checks, for before every release: builds the test
# program and runs it. No audio files or windows needed, and no sound comes
# out; the last check opens the audio device silently.
#
#   buildscripts/check_engine.sh [build-dir] [name filter]

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"

ninja -C "$BUILD" mixtape-engine-tests >/dev/null
TESTS="$(find "$BUILD" -name mixtape-engine-tests -type f -perm -u+x | head -1)"
if "$TESTS" ${2:+"$2"}; then
    echo "Engine checks passed."
else
    echo "ENGINE CHECKS FAILED: do not release."
    exit 1
fi
