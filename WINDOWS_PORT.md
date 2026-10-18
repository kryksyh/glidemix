# GlideMix — Windows port handover

GlideMix is a DJ timeline mixer built on Audacity 4 (Qt 6.10 / QML, the Muse
framework). Branch `mixhouse-dj2`; the `muse` submodule is our fork at
`cristitm76/glidemix-muse` (same branch). The macOS alpha is working and tested;
this note lists what is Mac-only and what has a Windows counterpart.

The code-only groundwork for Windows is on the branch `windows-port` (made
from `mixhouse-dj2`). None of it has been compiled with MSVC yet: it was
written on the Mac, and the macOS side was checked to be unchanged (see
"How the macOS side was checked" below). Expect a first Windows build to
turn up a handful of small compile errors in the new `#ifdef Q_OS_WIN` code.

## Getting the code

```
git clone --recursive -b windows-port https://github.com/cristitm76/glidemix.git
```

Upstream Audacity is kept as the `upstream` remote. Build like Audacity 4 on
Windows (Visual Studio 2022, CMake, Ninja, Qt 6.10.1 msvc2022_64). The app
target is `GlideMix`; `cmake --install` puts `bin\GlideMix.exe` and the Qt
runtime in the install prefix.

## Not in the repo (fetch or build per platform)

- ONNX Runtime (Beat This! beat grid and the GlideMix Phrase Detector). macOS: `mixhouse_deps/onnxruntime-1.18.0`.
  Windows: unpack Microsoft's `onnxruntime-win-x64-1.18.0.zip` as
  `mixhouse_deps/onnxruntime-win-x64-1.18.0` (it has `lib/onnxruntime.lib`,
  `lib/onnxruntime.dll`, `include/`). If it is missing the build skips Beat
  This! with a status message, as on the Mac. `cmake --install` puts
  `onnxruntime.dll` beside `GlideMix.exe` and both models in `bin\models\`.
  The Beat This! model is `mixhouse_deps/beat_this/beat_this.onnx` (copy it from
  the Mac); the phrase model is in the repo.
- Key analyzer: OpenKeyScan (MIT), `~/Developer/openkeyscan-analyzer` on the
  Mac. `buildscripts/packaging/MacOS/build_key_analyzer.sh` builds it with
  PyInstaller; the project has its own `build.bat` for Windows. On Windows the
  app looks for `bin\openkeyscan-analyzer\openkeyscan-analyzer.exe` beside
  `GlideMix.exe`, then `%APPDATA%\Glidemix\Helpers\openkeyscan-analyzer\`
  (`findKeyAnalyzer` in `openkeyscanclient.cpp`).
- FFmpeg: the Mac build bundles PyAV's shared FFmpeg in
  `Contents/Frameworks/ffmpeg`. On Windows the app looks in `<exe dir>\ffmpeg`
  first (`BundledFFmpegDir()` in
  `au3/modules/import-export/mod-ffmpeg/lib-ffmpeg-support/FFmpegFunctions.cpp`):
  put the shared FFmpeg DLLs there (`avformat-*.dll`, `avcodec-*.dll`,
  `avutil-*.dll`, `swresample-*.dll`, one of the versions Audacity supports).
- Rubber Band (time stretching / keylock in the mixtape engine, and the DJ
  Effects Pitch Shift): macOS arm64 uses the vendored prebuilt libraries in
  `src/effects/builtin_collection/mixxx/thirdparty/rubberband-macos-arm64`
  (unchanged). **Windows and Linux build Rubber Band 4.0.0 from source** —
  see "Rubber Band" below.

## Done on `windows-port`

Paths — one helper, `src/mixhouse/glidemixpaths.h` (header-only, so any
module can include it as `mixhouse/glidemixpaths.h`):

| What | macOS (unchanged) | Windows |
|---|---|---|
| GlideMix data | `~/Library/Application Support/Glidemix` | `%APPDATA%\Glidemix` |
| Mixxx import (db, `mixxx.cfg`, `effects.xml`) | `~/Library/Containers/org.mixxx.mixxx/.../Mixxx` | `%LOCALAPPDATA%\Mixxx` |
| rekordbox analysis | `~/Library/Pioneer/rekordbox/share/PIONEER/USBANLZ` | `%APPDATA%\Pioneer\rekordbox\share\PIONEER\USBANLZ` |
| Lexicon | `~/Library/Application Support/lexicon/main.db` | `%APPDATA%\lexicon\main.db` |

Used by: library DB and settings folder, HD waveforms, `Transitions.json`,
rekordbox index cache, stems cache and engine, key analyzer add-on, effect
chain presets, Mixxx effects import. The `use-old-engine` marker
(`mixtapeplayer.cpp`) and the `dump-audio` debug marker
(`MixtapeTransport.cpp`, plain C++, reads `%APPDATA%` itself) are under
`%APPDATA%\Glidemix` on Windows. The Mixtape Studio migration that used to be
in `main.cpp` is no longer there, so nothing to skip.

Objective-C++ files — already had stubs wired by `OS_IS_MAC` / `PLATFORM_ID`:
`MicrophoneAccess.cpp` (returns true), `src/app/internal/platform/stub/`
(`appearanceappicon.cpp`, `backgroundprocess.cpp`: nothing to do on Windows —
the taskbar icon comes from the .rc, and the plugin-registration process has
no window), and the preview player stub. Qt Multimedia is **not** in the build,
so the preview player stays the stub (now clearly marked): clicking a track's
overview in the library plays nothing on Windows.

Other:
- Reveal in file browser: `explorer.exe /select,<native path>`.
- Stems: PowerShell installer (same steps, STEP lines and checks as the bash
  one; Windows PowerShell 5.1, uv's Windows zip, `.venv\Scripts\python.exe`,
  run via `-EncodedCommand` with the destination in `GLIDEMIX_STEMS_DEST`);
  the GPU device is `cuda` instead of `mps`.
- FFmpeg: bundled-folder lookup (above), and the "bundled first, whatever its
  version" pass now also runs on Windows.
- Beat This!: CMake accepts the Windows ONNX Runtime layout (above); the
  library is compiled with `/fp:precise /w` under MSVC. The macOS-only
  `sysctl` RAM query in `mixhouseanalyzer.cpp` is behind `__APPLE__`
  (Windows uses the minimum pool of 2 models).
- DJ effects: fidlib gets `T_MSVC` and `/w` under MSVC (as Mixxx's build does)
  instead of `T_LINUX` and the GCC-only `-fno-finite-math-only`.
- Library "folder too broad to scan" guard: drive roots, `X:\Users` and the
  home folder on Windows.
- Packaging: `buildscripts/packaging/Windows/glidemix.iss` (Inno Setup 6,
  per-user install to `%LOCALAPPDATA%\Programs\GlideMix`, optional FFmpeg,
  key analyzer and ONNX Runtime, never launches the app by itself) and
  `buildscripts/packaging/Windows/glidemix_clean_uninstall.ps1` (runs the
  installer's uninstaller, recycles the app folder, `%APPDATA%\Glidemix`,
  `%LOCALAPPDATA%\Glidemix`, clears `HKCU\Software\Glidemix` after exporting
  it, lists what is left; Documents is left alone). Both share the installer
  AppId `{332C5FF2-1D0E-427E-B5B2-1D433E93F6D8}` — keep it fixed.

Where the app's own settings land on Windows (Muse framework, not changed):
`QSettings` in INI format, `%APPDATA%\Glidemix\Glidemix.ini`; logs and other
app-local data in `%LOCALAPPDATA%\Glidemix\Glidemix`; projects in
`Documents\Glidemix`.

## Rubber Band

`buildscripts/cmake/GlideMixRubberBand.cmake` fetches Rubber Band 4.0.0 with
FetchContent (git, pinned to the commit of tag `v4.0.0`,
`1d95888bec3ae0a17c0c4af791810d5a63f6bc35`) and builds its single-file
translation unit, `single/RubberBandSingle.cpp`, as the static target
`glidemix_rubberband`. That build uses Rubber Band's built-in FFT and
resampler, so there is no FFTW and no libsamplerate, and it is
single-threaded (`NO_THREADING`). MSVC gets `NOMINMAX`, `_USE_MATH_DEFINES`,
`__MSVC__` (as in Rubber Band's meson.build and `otherbuilds` vcxproj); Linux
gets `HAVE_POSIX_MEMALIGN` and links libatomic if a plain link needs it.

It is used from `au3/libraries/au3-mixtape-engine/CMakeLists.txt`
(`MIXTAPE_ENGINE_HAS_RUBBERBAND`) and `src/effects/builtin_collection/CMakeLists.txt`
(`MIXTAPE_HAS_RUBBERBAND`, Pitch Shift) on every non-Apple platform. The Apple
branches are untouched. Configure needs network access to github.com, or set
`FETCHCONTENT_SOURCE_DIR_RUBBERBAND` to a checkout of the tag.

Checked on Linux only (gcc 13 and clang 18, a standalone project that
stretches a sine by 1.0/1.25/0.8 and checks the output length). Still to
check on Windows: that it compiles under MSVC 2022 (`/w` hides warnings, only
errors matter), that stretching sounds right and is fast enough in Release
(built-in FFT is slower than FFTW/vDSP; look at CPU with several decks),
and that the GPL text (Rubber Band's `COPYING`) ships with the installer —
nothing in CMake installs the macOS licence files either.

## Still to do on a Windows machine

1. First MSVC build; fix whatever the new Windows branches trip over. Most
   likely spots: `mixhousestems.cpp` (PowerShell launch), `FFmpegFunctions.cpp`
   (`BundledFFmpegDir`, `GetModuleFileNameW`), `glidemixpaths.h`.
2. Check Rubber Band on the real Windows build (see "Rubber Band" below).
3. M4A/AAC/ALAC analysis: `src/mixhouse/mixhouseaudiosource.cpp` only falls
   back to AudioToolbox on macOS, so on Windows those tracks are not analysed
   (the build is fine). Add an FFmpeg-based decoder (mod-ffmpeg already loads
   FFmpeg at run time) or Media Foundation.
4. Library preview player: a real Windows implementation (Media Foundation,
   or a small player on `MixHouseAudioSource` + PortAudio). The stub is
   `src/mixhouse/internal/platform/stub/mixhousepreviewplayer.cpp`.
5. Stems on Windows: run the installer end to end. Check that uv's zip layout
   is found, that rewriting `pyvenv.cfg` after the move is enough (it should
   be: a Windows venv finds its base Python through `home`), and that
   Cancel works — `QProcess::kill()` ends powershell.exe but not a running
   uv/python child (on the Mac killing bash has the same limitation).
   PyPI's torch is CPU-only on Windows, so "GPU" fails and Automatic falls
   back to the CPU; a CUDA build needs `--index-url https://download.pytorch.org/whl/cu1xx`.
6. Key analyzer: build OpenKeyScan with its `build.bat`, check the JSON
   protocol over stdin/stdout behaves the same under Windows (line endings).
7. Non-ASCII paths: `sf_open(path.toUtf8())` and `mpg123_open_fixed(utf8)` in
   `mixhouseaudiosource.cpp` / `mixhousestems.cpp` — test a track in a folder
   with accents; libsndfile on Windows may need `sf_wchar_open`.
8. rekordbox: confirm the ANLZ folder location on a real rekordbox 6/7
   install, and that PPTH paths (Windows-style, with drive letters) match
   GlideMix's file paths in `mixhouserekordboxphrases.cpp`.
9. Installer: compile `glidemix.iss` with `iscc`, check the icon
   (`share/icons/AppIcon/AU4_AppIcon.ico` — may still be Audacity's) and that
   the MSVC runtime DLLs are in the install prefix (CMake's
   `InstallRequiredSystemLibraries`). Unsigned builds meet SmartScreen.
10. Test the clean-uninstall script before every test package, as on the Mac.

## Unsure / worth checking

- Lexicon on Windows: assumed to keep `main.db` in `%APPDATA%\lexicon` (it is
  an Electron app and that is Electron's default, matching the Mac location).
- Mixxx on Windows keeps its settings in `%LOCALAPPDATA%\Mixxx` (2.x). An
  older or portable Mixxx elsewhere will not be found.
- `explorer.exe /select,` with QProcess quoting the path as a separate
  argument is the Qt Creator approach; verify with a path containing spaces
  and commas.
- The clean-uninstall script sends big folders (the stems engine can be over
  1 GB) to the Recycle Bin; Windows asks to delete permanently when something
  is too big for it.
- `MIXHOUSE_BEAT_THIS_MODEL_PATH` is an absolute build-time path (as on the
  Mac); a packaged build would need the model shipped and found at run time.

## How the macOS side was checked

Every Windows change is either behind `#ifdef Q_OS_WIN` / `_WIN32` /
`__WXMSW__` / `if (OS_IS_WIN)` / `if (MSVC)` with the old code untouched in
the other branch, or goes through `glidemixpaths.h`, whose macOS branch builds
the same strings as before. On the Mac: each changed C++ file was
syntax-checked with the real build's compile flags (`ninja -t compdb` from
`build/`), and a small test program compared every path the helper returns
with the old hard-coded string — all identical.

## Other Mac specifics (unchanged)

- Packaging: `buildscripts/packaging/MacOS/make_glidemix_alpha.sh` (DMG, ad-hoc
  signing) and `glidemix_clean_uninstall.sh`.

## Working rules with Chris

- Change one thing at a time, verify, commit (small commits, clear messages).
- Every test package: clean uninstall first, install fresh, do not launch it.
- Never access iCloud, external disks, mail or messages; no purchases.
- Be blunt on technical calls.

## First Windows build (2026-10-04, NUC01: i3-1115G4, 32 GB, Windows 11 26200)

GlideMix.exe builds, installs and starts (first-run setup shown).

Toolchain: VS 2022 Build Tools (VCTools + Windows 11 SDK 26100), Git, CMake,
Ninja, Python 3.12 (winget); Qt 6.10.1 msvc2022_64 + qt5compat, qtnetworkauth,
qtshadertools, qtscxml via `aqtinstall` (no Qt account).

    cmake -S . -B C:\src\build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.10.1\msvc2022_64 -DMUSE_COMPILE_USE_CCACHE=OFF ^
      -DMUSE_ENABLE_UNIT_TESTS=OFF -DCMAKE_INSTALL_PREFIX=C:/src/install
    cmake --build C:\src\build --target audacity   (the app target is "audacity")
    cmake --install C:\src\build                    (Qt, QML and DLLs beside the exe)

Fixed on the way: TagLib recipe tracked in the repo; AudioUnit-only code;
music-start constants outside the Beat This! block; portmixer links winmm;
appfactory included the real cloud header while the stub was linked.

**Code signing is required.** Windows 11 Smart App Control refuses an unsigned
GlideMix.exe outright (CodeIntegrity event 3033/3077, no "run anyway"). Testing
needs Smart App Control off; shipping needs an Authenticode signature (Azure
Trusted Signing ~USD 10/month, or a code-signing certificate). Decide before the
Windows beta.

Still to check on Windows: audio playback and devices, Perform, keylock
(Rubber Band from source), library scan of the Desktop music folders, stems
install (PowerShell), Beat This! (needs the Windows ONNX Runtime in mixhouse_deps).

## First Windows test package (2026-10-06, NUC01 i3-1115G4)

`GlideMix-Alpha-v1.0.0-Early-Preview-x64-setup.exe`: iscc with
`/DBuildDir=C:\src\install /DFFmpegDir=C:\src\ffmpeg\bin
/DKeyAnalyzerDir=C:\src\openkeyscan-analyzer\dist\openkeyscan-analyzer`.
FFmpeg: BtbN `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip` (avformat 62,
as the Mac ships); the 7.1 URL in the old tools script no longer exists.

Analysis against the Mac, same 20 tracks (Hy2rogen Demo Songs, one per
genre): BPM 20/20, bar 1 20/20, phrase count 20/20, key 18/20 identical.
~25 s per track, two at a time (small machines use half their threads, at
idle priority). The earlier "BPM at the lowest" and crackling reports came
from the build without ONNX Runtime and four analysis threads.
