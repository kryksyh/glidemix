# Rubber Band for DJ Effects Pitch Shift (macOS arm64)

Prebuilt static libraries, copied from Mixxx 2.6's vcpkg build environment
(`mixxx-deps-2.6-arm64-osx-aa78b5a`, triplet `arm64-osx-min1100`):

| Library | Version | License |
|---|---|---|
| Rubber Band (`librubberband.a`, headers) | 4.0.0 | GPL-2.0-or-later, `licenses/rubberband.txt` |
| libsamplerate (`libsamplerate.a`) | 0.2.2 | BSD-2-Clause, `licenses/libsamplerate.txt` |
| FFTW (`libfftw3.a`) | 3.3.10 | GPL-2.0-or-later, `licenses/fftw3.txt` |

Linked by `src/effects/builtin_collection/CMakeLists.txt` (`MIXTAPE_RUBBERBAND_ROOT`).
Other platforms: build Rubber Band from source (it can build with its
built-in FFT and resampler) and point `MIXTAPE_RUBBERBAND_ROOT` at it.
