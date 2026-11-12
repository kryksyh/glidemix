"""Archive the installed build without losing macOS symlinks or executable modes."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
install = root / "build.install"
output = root / "build.artifacts"
output.mkdir(exist_ok=True)
release = json.loads((root / "upstream-release.json").read_text())
platform = os.environ["ARTIFACT_PLATFORM"]
name = f"glidemix-{release['tag']}-{platform}"
executables = {
    "macos-arm64": "GlideMix.app/Contents/MacOS/GlideMix",
    "macos-x86_64": "GlideMix.app/Contents/MacOS/GlideMix",
    "windows-x86_64": "bin/GlideMix.exe",
    "linux-x86_64": "bin/glidemix",
}
shutil.copy2(root / "upstream-release.json", install)
shutil.copy2(root / "LICENSE.txt", install)
if platform.startswith("macos"):
    bundle = install / "GlideMix.app"
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(bundle)], check=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)
# Check that the installed executable can load its runtime dependencies.
subprocess.run([str(install / executables[platform]), "--version"], check=True, timeout=30)
if platform.startswith("macos"):
    archive = output / f"{name}.zip"
    subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(bundle), str(archive)], check=True)
elif platform.startswith("windows"):
    archive = Path(shutil.make_archive(str(output / name), "zip", install))
else:
    archive = Path(shutil.make_archive(str(output / name), "gztar", install))
with archive.open("rb") as stream:
    digest = hashlib.file_digest(stream, "sha256").hexdigest()
(output / f"{archive.name}.sha256").write_text(f"{digest}  {archive.name}\n")
