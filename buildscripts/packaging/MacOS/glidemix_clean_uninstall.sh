#!/bin/bash
#
# GlideMix: a clean, deep uninstall -- everything an installed GlideMix
# leaves on a Mac goes to the Trash (so it can be put back), and its
# settings are cleared. For testing a package as a brand-new user.
#
#   buildscripts/packaging/MacOS/glidemix_clean_uninstall.sh
#
# Removed: /Applications/GlideMix.app; ~/Library/Application Support/Glidemix
# (library, analysis, logs, the stems engine and stems); ~/Library/Caches/
# Glidemix; the com.glidemix.* preferences; saved window state. Crash reports
# and macOS privacy permissions stay (the latter are System Settings').

set -uo pipefail

if pgrep -x GlideMix >/dev/null; then
    echo "GlideMix is running: quit it first."
    exit 1
fi

trash() {
    [ -e "$1" ] || return 0
    osascript -e "tell application \"Finder\" to delete (POSIX file \"$1\" as alias)" >/dev/null && echo "  trashed $1"
}

echo "== Files"
trash "/Applications/GlideMix.app"
trash "$HOME/Library/Application Support/Glidemix"
trash "$HOME/Library/Caches/Glidemix"
trash "$HOME/Library/Caches/com.glidemix.app"
trash "$HOME/Library/Caches/com.glidemix.app.sparkle"   # Sparkle's downloads
trash "$HOME/Library/Saved Application State/com.glidemix.app.savedState"
trash "/Users/Shared/glidemix-m4a-test"

echo "== Settings"
for domain in com.glidemix.app com.glidemix.Glidemix; do
    defaults delete "$domain" >/dev/null 2>&1 && echo "  cleared $domain"
    trash "$HOME/Library/Preferences/$domain.plist"
done

echo "== Left over"
LEFT=$(find "$HOME/Library" -maxdepth 3 -iname "*glidemix*" 2>/dev/null \
       | grep -v -e DiagnosticReports -e CrashReporter -e "Mobile Documents")
if [ -n "$LEFT" ]; then
    echo "$LEFT"
else
    echo "  nothing"
fi
