# Queen Mary DSP -- copied verbatim from the parked Mixxx fork's lib/qm-dsp
# (GPL-2.0, see COPYING in this directory). This is mixxx's own REAL, DEFAULT
# analysis engine for BPM/beatgrid (DetectionFunction + TempoTrackV2/DownBeat)
# and musical key (GetKeyMode) -- not the optional neural "Beat This!" ONNX
# path (that one needs ~150-250MB of fetched dependencies and is OFF by
# default even in real mixxx; this is what actually runs for every user who
# hasn't opted into that).
#
# The exact file list and compile definitions below are copied verbatim from
# the real Mixxx fork's own top-level CMakeLists.txt (its "Queen Mary DSP"
# QueenMaryDsp target) -- not re-derived. Mixxx itself compiles only this
# subset of the vendored library; the rest (mfcc, wavelet, some rateconversion/
# segmentation files, hmm.c, pca.c) sits alongside it unused, same as in the
# real source tree, kept here rather than deleted so this stays a genuine
# copy of the real library, not a trimmed-down excerpt.

add_library(mixhouse_qmdsp STATIC EXCLUDE_FROM_ALL
    base/Pitch.cpp
    dsp/chromagram/Chromagram.cpp
    dsp/chromagram/ConstantQ.cpp
    dsp/keydetection/GetKeyMode.cpp
    dsp/mfcc/MFCC.cpp   # GlideMix: the "Sounds like" fingerprint's timbre part
    dsp/onsets/DetectionFunction.cpp
    dsp/onsets/PeakPicking.cpp
    dsp/phasevocoder/PhaseVocoder.cpp
    dsp/rateconversion/Decimator.cpp
    dsp/signalconditioning/DFProcess.cpp
    dsp/signalconditioning/FiltFilt.cpp
    dsp/signalconditioning/Filter.cpp
    dsp/signalconditioning/Framer.cpp
    dsp/tempotracking/DownBeat.cpp
    dsp/tempotracking/TempoTrack.cpp
    dsp/tempotracking/TempoTrackV2.cpp
    dsp/tonal/ChangeDetectionFunction.cpp
    dsp/tonal/TCSgram.cpp
    dsp/tonal/TonalEstimator.cpp
    dsp/transforms/FFT.cpp
    ext/kissfft/kiss_fft.c
    ext/kissfft/tools/kiss_fftr.c
    maths/Correlation.cpp
    maths/KLDivergence.cpp
    maths/MathUtilities.cpp
)

# Real mixxx's own exact compile definitions for this target -- kiss_fft
# defaults to float precision; mixxx (and every analyzer built on this
# library) expects double.
target_compile_definitions(mixhouse_qmdsp PRIVATE kiss_fft_scalar=double)
if(UNIX)
    target_compile_definitions(mixhouse_qmdsp PRIVATE USE_PTHREADS)
elseif(MSVC)
    target_compile_definitions(mixhouse_qmdsp PRIVATE _USE_MATH_DEFINES)
endif()

target_include_directories(mixhouse_qmdsp SYSTEM PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)
