#!/bin/bash
# GlideMix Trainer launcher (what "GlideMix Trainer.app" runs in Terminal).
#   run_trainer.sh <phrases|fingerprints> <low|med|high> <folder> [folder ...]
# Sets up the trainer's own Python the first time, keeps the Mac awake while
# it runs, and shows a notification when it ends.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$HOME/Library/Application Support/GlideMix Trainer"
VENV="$WORK/venv"
UV="$(command -v uv || echo /opt/homebrew/bin/uv)"
task="$1"; level="$2"; shift 2

mkdir -p "$WORK"
if [ ! -x "$VENV/bin/python" ]; then
    echo "First run: setting up the trainer's Python (a few minutes)..."
    "$UV" venv -q --python 3.12 "$VENV" || exit 1
fi
"$UV" pip install -q --python "$VENV/bin/python" numpy torch onnx onnxruntime pyrekordbox || exit 1

caffeinate -i -s "$VENV/bin/python" "$HERE/glidemix_trainer.py" "$task" --level "$level" --folders "$@"
status=$?
if [ $status -eq 0 ]; then
    msg="Finished. Report: $(tail -1 "$WORK/trainer.log" | cut -c11-)"
else
    msg="Stopped (exit $status). Run it again to continue where it left off."
fi
osascript -e "display notification \"${msg//\"/}\" with title \"GlideMix Trainer\"" 2>/dev/null
echo; echo "$msg"
exit $status
