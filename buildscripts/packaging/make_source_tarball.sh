#!/bin/bash
# GlideMix: the source code of one release, as one archive (Ardour style):
#   buildscripts/packaging/make_source_tarball.sh [version] [out-dir]
# Complete GPL source (GlideMix, Audacity and the muse framework submodule),
# no git history. Building it is the user's own work: no build guide, no
# prebuilt dependencies, and none of our keys (signing, updates) inside.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${1:-$(sed -n 's/^set(MUSE_APP_VERSION_MAJ_MIN "\(.*\)")/\1/p' "$ROOT/version.cmake").$(sed -n 's/^set(MUSE_APP_VERSION_PATCH "\(.*\)")/\1/p' "$ROOT/version.cmake")}"
OUT="${2:-$ROOT/dist}"
NAME="GlideMix-$VERSION-source"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

git -C "$ROOT" archive --format=tar --prefix="$NAME/" HEAD > "$TMP/main.tar"
git -C "$ROOT/muse" archive --format=tar --prefix="$NAME/muse/" HEAD > "$TMP/muse.tar"
DEPS_COMMIT="$(git -C "$ROOT" ls-tree HEAD muse_deps | awk '{print $3}')"
mkdir -p "$TMP/x"
tar -xf "$TMP/main.tar" -C "$TMP/x"
tar -xf "$TMP/muse.tar" -C "$TMP/x"
# Our own build notes and CI recipes stay out (the GPL asks for the source
# and the build scripts we use, not a guide): the Windows port handover,
# Audacity's build guide and CI scripts, internal review notes and work logs.
( cd "$TMP/x/$NAME" && rm -rf CL[A]UDE.md WINDOWS_PORT.md BUILDING.md ci_build.bat ci_build.cmake .github buildscripts/ci muse/.github muse/buildscripts/ci \
    docs/mixhouse-plugin-automation-review.md DEVLOG-*.md \
    docs/HANDOFF.md docs/tools-*.md docs/mixmeister-*.md docs/glidemixer-*.md docs/linux-build.md docs/library-speed-plan.md docs/match-related-plan.md )
cat > "$TMP/x/$NAME/BUILDING-GLIDEMIX.txt" <<TXT
GlideMix $VERSION - source code

This is the complete source code of GlideMix $VERSION, under the GNU GPL
(see LICENSE.txt). You may build, change and share it under that licence.

Building it yourself can be a challenging and complex process, especially
on macOS and Windows. We don't provide help for this process and can't
support the end result. The third-party build dependencies are not included:
they come from https://github.com/musescore/muse_deps (commit $DEPS_COMMIT),
which goes into the muse_deps folder.

If you just want to use GlideMix, the ready-to-run
version is at https://github.com/cristitm76/glidemix-releases

"GlideMix" and the GlideMix logo are not covered by the GPL: a build you
share must use another name and icon.
TXT
# None of our tooling's names in the public source (Audacity's own credits
# name the composer Risset, so those lines stay).
if grep -rIi "cl[a]ude" "$TMP/x/$NAME" | grep -vi "risset" | cut -c1-200 | grep .; then
    echo "The source still names our tooling: not packed."
    exit 1
fi
mkdir -p "$OUT"
tar -C "$TMP/x" -cJf "$OUT/$NAME.tar.xz" "$NAME"
ls -lh "$OUT/$NAME.tar.xz"
