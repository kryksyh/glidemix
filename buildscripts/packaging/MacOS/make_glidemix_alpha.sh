#!/bin/bash
#
# GlideMix (beta): a self-contained, ad-hoc signed app in a DMG, for testers.
#
#   buildscripts/packaging/MacOS/make_glidemix_alpha.sh [build-dir] [out-dir]
#
# Builds the release, installs it (Qt's deploy step copies Qt and the other
# libraries into the app), signs everything ad-hoc (free, no Apple account:
# enough for Apple Silicon to run it; testers open it once with "Open Anyway")
# and makes GlideMix-Beta-v<version>.dmg with an install note.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
BUILD="${1:-$ROOT/build-release}"
OUT="${2:-$ROOT/dist}"
STAGE="$OUT/stage"
v() { grep -E "set\(MUSE_APP_VERSION_$1 " "$ROOT/version.cmake" | grep -oE '"[0-9]+"' | tr -d '"'; }
VERSION="$(v MAJOR).$(v MINOR).$(v PATCH)"

echo "== Build ($BUILD)"
# The app's install rule uses the configured prefix, so the stage is set here.
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_INSTALL_PREFIX="$STAGE" >/dev/null
ninja -C "$BUILD" GlideMix

echo "== Install into $STAGE"
rm -rf "$STAGE"
cmake --install "$BUILD"
APP="$STAGE/GlideMix.app"
[ -d "$APP" ] || { echo "no app at $APP"; exit 1; }

echo "== Key analyzer"
# Built once (build_key_analyzer.sh, about 550 MB) and reused; it brings its
# own Python and libraries, so the library copying below leaves it alone.
KEY_ANALYZER="$OUT/helpers/openkeyscan-analyzer"
if [ ! -x "$KEY_ANALYZER/openkeyscan-analyzer" ] || [ ! -d "$OUT/helpers/ffmpeg" ]; then
    "$HERE/build_key_analyzer.sh" "${GLIDEMIX_KEY_ANALYZER_SRC:-$HOME/Developer/openkeyscan-analyzer}" "$OUT/helpers"
fi
cp -R "$KEY_ANALYZER" "$APP/Contents/Resources/"
KEY_DIR="$APP/Contents/Resources/openkeyscan-analyzer"

echo "== Analysis models"
# Beat This! (the beat grid) and the phrase model, found by mixHouseModelPath().
# Without them a release silently falls back to the older detectors.
MODELS="$APP/Contents/Resources/models"
mkdir -p "$MODELS"
cp "$ROOT/mixhouse_deps/beat_this/beat_this.onnx" "$MODELS/"
cp "$ROOT/src/mixhouse/models/glidemix_phrases.onnx" "$ROOT/src/mixhouse/models/beat_this_small.onnx" "$MODELS/"
ls -la "$MODELS"

echo "== FFmpeg"
# Found by the FFmpeg import/export module before any on the Mac itself.
FFMPEG_DIR="$APP/Contents/Frameworks/ffmpeg"
mkdir -p "$APP/Contents/Frameworks"
cp -R "$OUT/helpers/ffmpeg" "$FFMPEG_DIR"

echo "== Copy in libraries Qt's deploy step missed"
# Anything a binary loads through @rpath must be in Contents/Frameworks.
# Look for it in the build and in mixhouse_deps; repeat until nothing is new.
FW="$APP/Contents/Frameworks"
mkdir -p "$FW"
for _pass in 1 2 3 4 5; do
    added=0
    while IFS= read -r lib; do
        [ -e "$FW/$lib" ] && continue
        found=$(find "$BUILD" "$ROOT/mixhouse_deps" -name "$lib" \( -type f -o -type l \) 2>/dev/null | head -1)
        if [ -z "$found" ]; then
            echo "Cannot find $lib"
            exit 1
        fi
        echo "  + $lib"
        cp -L "$found" "$FW/$lib"
        chmod u+w "$FW/$lib"
        install_name_tool -id "@rpath/$lib" "$FW/$lib" 2>/dev/null || true
        added=1
    done < <(find "$APP/Contents" \( -path "$KEY_DIR" -o -path "$FFMPEG_DIR" \) -prune -o -type f \( -name "*.dylib" -o -perm +111 \) -print0 \
        | xargs -0 otool -L 2>/dev/null | grep -oE "@rpath/[^ /]+\.dylib" | sed 's|@rpath/||' | sort -u)
    [ "$added" = 0 ] && break
done

echo "== Check nothing points back to this Mac"
LEAKS=$(find "$APP/Contents" -type f \( -name "*.dylib" -o -perm +111 \) -print0 \
    | xargs -0 otool -L 2>/dev/null | grep -E "^\s+/(Users|opt|usr/local)/" | sort -u || true)
if [ -n "$LEAKS" ]; then
    echo "These libraries are not inside the app:"
    echo "$LEAKS"
    exit 1
fi

echo "== Remove debug symbols"
find "$APP/Contents" -type d -name "*.dSYM" -prune -exec rm -rf {} +

echo "== Sign ad-hoc"
# Inside out: libraries and plug-ins first, then the app with its entitlements
# (microphone, and loading other people's audio plug-ins).
find "$APP/Contents" -type f \( -name "*.dylib" -o -name "*.so" \) -print0 \
    | xargs -0 -P 4 -I {} codesign --force --sign - '{}'
# Sparkle (macOS updates): its helpers first, inside out, as Sparkle asks.
SPK="$APP/Contents/Frameworks/Sparkle.framework/Versions/B"
if [ -d "$SPK" ]; then
    codesign --force --options runtime --sign - "$SPK/XPCServices/Installer.xpc"
    codesign --force --options runtime --preserve-metadata=entitlements --sign - "$SPK/XPCServices/Downloader.xpc"
    codesign --force --options runtime --sign - "$SPK/Autoupdate"
    codesign --force --options runtime --sign - "$SPK/Updater.app"
fi
find "$APP/Contents/Frameworks" -maxdepth 1 -name "*.framework" -print0 2>/dev/null \
    | xargs -0 -I {} codesign --force --sign - '{}'
# The key analyzer is a program of its own: hardened, with what PyTorch needs.
while IFS= read -r -d '' f; do
    if file "$f" | grep -q Mach-O; then
        codesign --force --sign - "$f"
    fi
done < <(find "$KEY_DIR" -type f -perm +111 ! -name "*.dylib" ! -name "*.so" -print0)
codesign --force --options runtime --entitlements "$HERE/key_analyzer_entitlements.plist" \
    --sign - "$KEY_DIR/openkeyscan-analyzer"
codesign --force --deep --options runtime \
    --entitlements "$HERE/macosx_entitlements.plist" \
    --sign - "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

echo "== DMG"
DMG_DIR="$OUT/dmg"
rm -rf "$DMG_DIR"
mkdir -p "$DMG_DIR"
cp -R "$APP" "$DMG_DIR/"
ln -s /Applications "$DMG_DIR/Applications"
cat > "$DMG_DIR/How to open GlideMix.txt" <<'TXT'
GlideMix Beta -- how to open it the first time

This preview is not signed with an Apple Developer ID yet, so macOS asks
you to confirm once:

1. Drag GlideMix into Applications.
2. Double-click GlideMix. macOS says it cannot verify it: click Done.
3. Open System Settings > Privacy & Security, scroll down to
   "GlideMix was blocked..." and click Open Anyway.
4. Confirm with your password. From then on it opens normally.

The first time, macOS may also ask whether GlideMix may use the
microphone (only needed for recording) and your Documents or Music
folders (where your songs and projects are). Allow the folders your
music is in.

Stem separation: Preferences > Stems > Install stems engine
(a free download of about 1 GB).

Feedback and bugs: tell Chris.
TXT
DMG="$OUT/GlideMix-Beta-v$VERSION.dmg"
rm -f "$DMG"
hdiutil create -volname "GlideMix Beta v$VERSION" -srcfolder "$DMG_DIR" -ov -format UDZO "$DMG" >/dev/null
rm -rf "$DMG_DIR"
echo "== Done: $DMG ($(du -h "$DMG" | cut -f1))"
