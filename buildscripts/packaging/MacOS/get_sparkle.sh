#!/bin/bash
# GlideMix: fetches Sparkle (macOS updates) into thirdparty-mac/sparkle.
# Pinned version, checked against its SHA-256.
set -euo pipefail
VERSION=2.9.6
SHA256=52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DEST="$ROOT/thirdparty-mac/sparkle"
mkdir -p "$DEST"
cd "$DEST"
curl -fsSL -o "Sparkle-$VERSION.tar.xz" "https://github.com/sparkle-project/Sparkle/releases/download/$VERSION/Sparkle-$VERSION.tar.xz"
echo "$SHA256  Sparkle-$VERSION.tar.xz" | shasum -a 256 -c -
tar -xf "Sparkle-$VERSION.tar.xz"
echo "Sparkle $VERSION in $DEST"
