# beat_this_cpp in MixHouse DJ 2

Neural beat + downbeat tracker ("Beat This!", ISMIR 2024) run through ONNX
Runtime. Used by `mixhouseanalyzer.cpp` for the downbeat (bar phase) only;
BPM and the beatgrid itself still come from Queen Mary DSP (mixxx's default).

Copied from the parked MixHouse DJ (MixxxMeister) fork's fetched build deps,
the exact sources that fork compiled:

| Part | Upstream | Commit | License |
|---|---|---|---|
| `Source/` (no `main.cpp` CLI) | github.com/mosynthkey/beat_this_cpp | 07ab790a9ec2 | MIT (`LICENSE`) |
| `pocketfft/pocketfft_hdronly.h` | github.com/mreineck/pocketfft (branch `cpp`) | c90e55b3d529 | BSD-3 (`pocketfft/LICENSE.md`) |
| `r8brain/` headers + `fft/fft4g.h` | github.com/avaneev/r8brain-free-src | 26f847bf0585 | MIT (`r8brain/LICENSE`) |

Not vendored (binary, ~105MB, lives outside `src/` in `mixhouse_deps/`):
- ONNX Runtime 1.18.0 prebuilt macOS dylib + headers → `mixhouse_deps/onnxruntime-1.18.0/`
- model `beat_this.onnx` (sha256 c5c1466e08abdb03…) → `mixhouse_deps/beat_this/`

## Local patches

1. `beat_this_api.h`: constructor declared with `bool use_dbn = false` to match
   its own `.cpp` definition (upstream header/source mismatch; same patch the
   fork applied in CMake).
2. `beat_this_api.cpp`: removed `#define MINIAUDIO_IMPLEMENTATION` and
   `#include "miniaudio.h"` -- unused by this file (only the CLI decodes
   files), and it would drag an 18MB single-header audio library into the app.
3. `beat_this_api.cpp`: `SetIntraOpNumThreads(BEAT_THIS_INTRA_OP_THREADS)` when
   defined (CMake sets 1). Measured throughput on 30s windows, same machine,
   while the app was also analyzing: 2 models x ORT default threads 26.5/min;
   2x2 30.6; 4x1 34.7; 4x2 42.9; 6x1 46.3; 8x1 53.0 (12.4GB peak). ORT's default
   pool mostly spins; the analyzer now runs more single-threaded models.

## Measured on Chris's Mac (M1 Max, 64GB), "#Selfie NRG" (3:22, 128 BPM)

- ~5.5s per track, ~3.75 cores, **~1.65GB peak memory** (inherent to the model;
  disabling ORT's CPU arena only got 1.58GB and was slower) -- so the analyzer
  runs a RAM-sized pool of single-threaded models (see patch 3).
- DBN post-processing: 418/427 beats within 60ms of the Queen Mary grid, and
  105/105 downbeats agree on the same bar phase. Minimal (default)
  post-processing: only 92/107 agree -- DBN is used.
- Compile with `-fno-fast-math`: the fork found `-ffast-math` makes the model
  detect zero beats.
