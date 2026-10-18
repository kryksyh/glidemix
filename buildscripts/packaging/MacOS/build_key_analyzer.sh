#!/bin/bash
#
# GlideMix: builds the key analyzer (OpenKeyScan, MIT) as a standalone
# program -- Python, PyTorch and the model inside, nothing needed on the
# user's Mac -- for make_glidemix_alpha.sh to put in the app.
#
#   buildscripts/packaging/MacOS/build_key_analyzer.sh [analyzer-source] [out-dir]
#
# The source is a checkout of openkeyscan-analyzer with its .venv (Python
# 3.12, the pinned torch 2.2.2). Its own build script also signs as its
# author and copies into their app, so this uses the same spec without that.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SRC="${1:-$HOME/Developer/openkeyscan-analyzer}"
OUT="${2:-$ROOT/dist/helpers}"
WORK="$OUT/key-analyzer-build"
PY="$SRC/.venv/bin/python"

[ -x "$PY" ] || { echo "No Python at $PY (make the analyzer's .venv first)"; exit 1; }
[ -f "$SRC/checkpoints/openkeyscan3.pt" ] || { echo "No model in $SRC/checkpoints"; exit 1; }

echo "== PyInstaller"
"$PY" -m pip install --quiet "pyinstaller>=6" macholib

# The spec without the author's signing identity (ours signs the whole app).
mkdir -p "$WORK"
# Its scripts are named relative to the spec, so they are made absolute.
sed -e "/codesign_identity=/d" -e "/entitlements_file=/d" \
    -e "s|\['openkeyscan_analyzer_server.py'\]|['$SRC/openkeyscan_analyzer_server.py']|" \
    -e "s|\['runtime_hook_scipy.py'\]|['$SRC/runtime_hook_scipy.py']|" \
    "$SRC/openkeyscan_analyzer.spec" > "$WORK/glidemix_key_analyzer.spec"

echo "== Build"
# The spec finds the model and ffmpeg relative to the working directory.
(cd "$SRC" && TARGET_ARCH=arm64 "$PY" -m PyInstaller --noconfirm --log-level WARN \
    --distpath "$WORK/dist" --workpath "$WORK/build" "$WORK/glidemix_key_analyzer.spec")

DIST="$WORK/dist/openkeyscan-analyzer"
echo "== Trim"
"$PY" "$SRC/cleanup_video_deps.py" "$DIST" >/dev/null || true
rm -f "$DIST/_internal/libssl.3.dylib" "$DIST/_internal/libcrypto.3.dylib"

rm -rf "$OUT/openkeyscan-analyzer"
mv "$DIST" "$OUT/openkeyscan-analyzer"
rm -rf "$WORK"
echo "== Done: $OUT/openkeyscan-analyzer ($(du -sh "$OUT/openkeyscan-analyzer" | cut -f1))"

# FFmpeg for importing and exporting M4A/AAC, WMA and the like: the complete
# shared build that PyAV brings (the trim above drops some of its video
# libraries from the analyzer's copy). Its libraries find each other through
# @loader_path; Audacity's loader asks for libavformat.<major>.dylib.
echo "== FFmpeg"
AV_LIBS="$("$PY" -c 'import av, os; print(os.path.join(os.path.dirname(av.__file__), ".dylibs"))')"
rm -rf "$OUT/ffmpeg"
mkdir -p "$OUT/ffmpeg"
cp "$AV_LIBS"/*.dylib "$OUT/ffmpeg/"
for lib in "$OUT/ffmpeg"/*.dylib; do
    chmod u+w "$lib"
    install_name_tool -add_rpath @loader_path "$lib" 2>/dev/null || true
    # Changing a library breaks its signature, and macOS kills a process
    # that loads it: sign again (the app's own signing redoes it anyway).
    codesign --force --sign - "$lib" >/dev/null 2>&1
done
(cd "$OUT/ffmpeg"
 for base in avformat avcodec avutil; do
     full=$(ls lib$base.*.*.*.dylib | head -1)
     major=$(echo "$full" | cut -d. -f2)
     ln -sf "$full" "lib$base.$major.dylib"
 done)
echo "== Done: $OUT/ffmpeg ($(du -sh "$OUT/ffmpeg" | cut -f1), PyAV $("$PY" -c 'import av; print(av.__version__)'))"
