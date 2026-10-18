#
# Audacity: A Digital Audio Editor
#
declare_module(effects_builtin_collection)

set(MODULE_QRC builtin_effects_collection.qrc)

set(MODULE_SRC
    # common
    ${CMAKE_CURRENT_LIST_DIR}/builtineffectscollectionmodule.cpp
    ${CMAKE_CURRENT_LIST_DIR}/builtineffectscollectionmodule.h

    ${CMAKE_CURRENT_LIST_DIR}/internal/builtincollectionloader.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/builtincollectionloader.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/builtinviewlauncher.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/builtinviewlauncher.h

    ${CMAKE_CURRENT_LIST_DIR}/common/params.h
    ${CMAKE_CURRENT_LIST_DIR}/common/builtineffectsettingmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/builtineffectsettingmodel.h
    ${CMAKE_CURRENT_LIST_DIR}/common/generatoreffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/generatoreffect.h
    ${CMAKE_CURRENT_LIST_DIR}/common/generatoreffectmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/generatoreffectmodel.h
    ${CMAKE_CURRENT_LIST_DIR}/common/measureunits.h

    ${CMAKE_CURRENT_LIST_DIR}/common/valuewarper/valuewarper.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/valuewarper/valuewarper.h
    ${CMAKE_CURRENT_LIST_DIR}/common/valuewarper/valuewarpertypes.h
    ${CMAKE_CURRENT_LIST_DIR}/common/valuewarper/warpingtransformer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/common/valuewarper/warpingtransformer.h

    # built-in effects
    ${CMAKE_CURRENT_LIST_DIR}/amplify/amplifyeffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/amplify/amplifyeffect.h
    ${CMAKE_CURRENT_LIST_DIR}/amplify/amplifyviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/amplify/amplifyviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/loudness/normalizeloudnesseffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/loudness/normalizeloudnesseffect.h
    ${CMAKE_CURRENT_LIST_DIR}/loudness/normalizeloudnessviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/loudness/normalizeloudnessviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/clickremoval/clickremovaleffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/clickremoval/clickremovaleffect.h
    ${CMAKE_CURRENT_LIST_DIR}/clickremoval/clickremovalviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/clickremoval/clickremovalviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/dynamics/compressor/compressoreffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/compressor/compressoreffect.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/compressor/compressorviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/compressor/compressorviewmodel.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/compressor/compressorsettingmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/compressor/compressorsettingmodel.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/limiter/limiterviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/limiter/limiterviewmodel.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/limiter/limitereffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/limiter/limitereffect.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/limiter/limitersettingmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/limiter/limitersettingmodel.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/dynamicseffectsmacros.h

    ${CMAKE_CURRENT_LIST_DIR}/normalize/normalizeeffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/normalize/normalizeeffect.h
    ${CMAKE_CURRENT_LIST_DIR}/normalize/normalizeviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/normalize/normalizeviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/removedcoffset/removedcoffseteffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/removedcoffset/removedcoffseteffect.h

    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/meters/abstractdbmetermodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/meters/abstractdbmetermodel.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/meters/compressiondbmetermodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/meters/compressiondbmetermodel.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/meters/outputdbmetermodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/meters/outputdbmetermodel.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/painters/abstractsequencepainter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/painters/abstractsequencepainter.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/painters/areasequencepainter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/painters/areasequencepainter.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/painters/linesequencepainter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/painters/linesequencepainter.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/abstractdynamicseffectinstancemodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/abstractdynamicseffectinstancemodel.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/dynamicstimeline.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/dynamicstimeline.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/timelinesourcemodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/timelinesourcemodel.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/stopwatch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/stopwatch.h
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/dynamicsplaystatemodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dynamics/timeline/dynamicsplaystatemodel.h

    ${CMAKE_CURRENT_LIST_DIR}/tonegen/chirpeffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tonegen/chirpeffect.h
    ${CMAKE_CURRENT_LIST_DIR}/tonegen/toneeffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tonegen/toneeffect.h

    ${CMAKE_CURRENT_LIST_DIR}/reverb/reverbeffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/reverb/reverbeffect.h
    ${CMAKE_CURRENT_LIST_DIR}/reverb/reverbviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/reverb/reverbviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/basstreble/basstrebleeffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/basstreble/basstrebleeffect.h
    ${CMAKE_CURRENT_LIST_DIR}/basstreble/basstrebleviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/basstreble/basstrebleviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/paulstretch/paulstretcheffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/paulstretch/paulstretcheffect.h
    ${CMAKE_CURRENT_LIST_DIR}/paulstretch/paulstretchviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/paulstretch/paulstretchviewmodel.h
    ${CMAKE_CURRENT_LIST_DIR}/tonegen/toneviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tonegen/toneviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/dtmfgen/dtmfgenerator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dtmfgen/dtmfgenerator.h
    ${CMAKE_CURRENT_LIST_DIR}/dtmfgen/dtmfviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dtmfgen/dtmfviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/silencegen/silencegenerator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/silencegen/silencegenerator.h
    ${CMAKE_CURRENT_LIST_DIR}/silencegen/silenceviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/slidingstretch/slidingstretcheffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/slidingstretch/slidingstretcheffect.h
    ${CMAKE_CURRENT_LIST_DIR}/slidingstretch/slidingstretchviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/slidingstretch/slidingstretchviewmodel.h
    ${CMAKE_CURRENT_LIST_DIR}/slidingstretch/slidingstretchsettingmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/slidingstretch/slidingstretchsettingmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/noisegen/noisegenerator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/noisegen/noisegenerator.h
    ${CMAKE_CURRENT_LIST_DIR}/noisegen/noiseviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/noisegen/noiseviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/noisereduction/noisereductioneffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/noisereduction/noisereductioneffect.h
    ${CMAKE_CURRENT_LIST_DIR}/noisereduction/noisereductionviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/noisereduction/noisereductionviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/fade/fadeeffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/fade/fadeeffect.h

    ${CMAKE_CURRENT_LIST_DIR}/equalization_common/equalizationenvelopeutils.cpp
    ${CMAKE_CURRENT_LIST_DIR}/equalization_common/equalizationenvelopeutils.h

    ${CMAKE_CURRENT_LIST_DIR}/graphiceq/equalizationbandsliders.cpp
    ${CMAKE_CURRENT_LIST_DIR}/graphiceq/equalizationbandsliders.h
    ${CMAKE_CURRENT_LIST_DIR}/graphiceq/graphiceq.cpp
    ${CMAKE_CURRENT_LIST_DIR}/graphiceq/graphiceq.h
    ${CMAKE_CURRENT_LIST_DIR}/graphiceq/graphiceqbandsmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/graphiceq/graphiceqbandsmodel.h
    ${CMAKE_CURRENT_LIST_DIR}/graphiceq/graphiceqviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/graphiceq/graphiceqviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/filtercurveeq/filtercurveeq.cpp
    ${CMAKE_CURRENT_LIST_DIR}/filtercurveeq/filtercurveeq.h
    ${CMAKE_CURRENT_LIST_DIR}/filtercurveeq/filtercurvemodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/filtercurveeq/filtercurvemodel.h
    ${CMAKE_CURRENT_LIST_DIR}/filtercurveeq/filtercurveeqviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/filtercurveeq/filtercurveeqviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/invert/inverteffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/invert/inverteffect.h

    ${CMAKE_CURRENT_LIST_DIR}/reverse/reverseeffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/reverse/reverseeffect.h

    ${CMAKE_CURRENT_LIST_DIR}/repair/repaireffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/repair/repaireffect.h

    ${CMAKE_CURRENT_LIST_DIR}/truncatesilence/truncatesilenceeffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/truncatesilence/truncatesilenceeffect.h
    ${CMAKE_CURRENT_LIST_DIR}/truncatesilence/truncatesilenceviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/truncatesilence/truncatesilenceviewmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/changepitch/changepitcheffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/changepitch/changepitcheffect.h
    ${CMAKE_CURRENT_LIST_DIR}/changepitch/changepitchviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/changepitch/changepitchviewmodel.h
    )

# GlideMix: Mixxx's built-in effects (GPL, Mixxx 2.6 src/effects/backends/builtin),
# their filter design library fidlib (LGPL 2.1) and CAPS Reverb (GPL). The Mixxx
# sources compile against small stand-ins for Mixxx's engine headers in
# mixxx/dsp; only mixxxeffectsdsp.cpp sees both sides of that boundary's API.
set(MIXXX_DSP_DIR ${CMAKE_CURRENT_LIST_DIR}/mixxx/dsp)
set(MIXXX_DSP_SRC
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffectsdsp.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/autopaneffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/balanceeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/bessel4lvmixeqeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/bessel8lvmixeqeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/biquadfullkilleqeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/bitcrushereffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/compressoreffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/glidemixdjeffects.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/distortioneffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/echoeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/filtereffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/flangereffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/glitcheffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/graphiceqeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/linkwitzriley8eqeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/loudnesscontoureffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/metronomeeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/moogladder4filtereffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/parametriceqeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/phasereffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/reverbeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/threebandbiquadeqeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/tremoloeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/whitenoiseeffect.cpp
    ${MIXXX_DSP_DIR}/effects/backends/builtin/metronomeclick.cpp
    ${MIXXX_DSP_DIR}/util/sample.cpp
    ${MIXXX_DSP_DIR}/engine/filters/enginefilterbessel4.cpp
    ${MIXXX_DSP_DIR}/engine/filters/enginefilterbessel8.cpp
    ${MIXXX_DSP_DIR}/engine/filters/enginefilterbiquad1.cpp
    ${MIXXX_DSP_DIR}/engine/filters/enginefilterlinkwitzriley4.cpp
    ${MIXXX_DSP_DIR}/engine/filters/enginefilterlinkwitzriley8.cpp
    ${MIXXX_DSP_DIR}/engine/filters/enginefiltermoogladder4.cpp
    ${MIXXX_DSP_DIR}/lib/reverb/Reverb.cc
    ${MIXXX_DSP_DIR}/lib/fidlib/fidlib.c
    )
list(APPEND MODULE_SRC
    ${MIXXX_DSP_SRC}
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffectsdsp.h
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffect.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffect.h
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffects.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffects.h
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffectlist.h
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffectids.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffectids.h
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffectsimport.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffectsimport.h
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffectviewmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixxx/mixxxeffectviewmodel.h
    )
# Pitch Shift runs on Rubber Band 4.0 (GPL), statically linked with
# libsamplerate (BSD) and FFTW 3 (GPL) -- prebuilt for macOS arm64 in
# mixxx/thirdparty (from Mixxx's 2.6 vcpkg buildenv); without them Pitch
# Shift is left out.
set(MIXTAPE_RUBBERBAND_ROOT "${CMAKE_CURRENT_LIST_DIR}/mixxx/thirdparty/rubberband-macos-arm64"
    CACHE PATH "Prefix holding include/rubberband and lib/librubberband.a (plus fftw3, samplerate)")
# fidlib's target macro: T_MSVC under Visual C++ (as Mixxx's own build does).
if (MSVC)
    set(MIXXX_DSP_DEFINITIONS "MIXXX;T_MSVC")
else()
    set(MIXXX_DSP_DEFINITIONS "MIXXX;T_LINUX")
endif()
set(MIXXX_DSP_INCLUDES "${MIXXX_DSP_DIR};${MIXXX_DSP_DIR}/lib/reverb;${MIXXX_DSP_DIR}/lib/fidlib")
if (APPLE AND EXISTS "${MIXTAPE_RUBBERBAND_ROOT}/lib/librubberband.a")
    list(APPEND MIXXX_DSP_SRC ${MIXXX_DSP_DIR}/effects/backends/builtin/pitchshifteffect.cpp)
    list(APPEND MODULE_SRC ${MIXXX_DSP_DIR}/effects/backends/builtin/pitchshifteffect.cpp)
    list(APPEND MIXXX_DSP_DEFINITIONS MIXTAPE_HAS_RUBBERBAND)
    list(APPEND MIXXX_DSP_INCLUDES "${MIXTAPE_RUBBERBAND_ROOT}/include")
    set(MIXTAPE_RUBBERBAND_LIBS
        "${MIXTAPE_RUBBERBAND_ROOT}/lib/librubberband.a"
        "${MIXTAPE_RUBBERBAND_ROOT}/lib/libsamplerate.a"
        "${MIXTAPE_RUBBERBAND_ROOT}/lib/libfftw3.a"
        "-framework Accelerate")
    message(STATUS "GlideMix: DJ Effects Pitch Shift uses Rubber Band from ${MIXTAPE_RUBBERBAND_ROOT}")
elseif (NOT APPLE)
    # Windows / Linux: Rubber Band built from source (built-in FFT and resampler).
    include(${CMAKE_CURRENT_LIST_DIR}/../../../buildscripts/cmake/GlideMixRubberBand.cmake)
    list(APPEND MIXXX_DSP_SRC ${MIXXX_DSP_DIR}/effects/backends/builtin/pitchshifteffect.cpp)
    list(APPEND MODULE_SRC ${MIXXX_DSP_DIR}/effects/backends/builtin/pitchshifteffect.cpp)
    list(APPEND MIXXX_DSP_DEFINITIONS MIXTAPE_HAS_RUBBERBAND)
    list(APPEND MIXXX_DSP_INCLUDES "${rubberband_SOURCE_DIR}")
    set(MIXTAPE_RUBBERBAND_LIBS glidemix_rubberband)
    message(STATUS "GlideMix: DJ Effects Pitch Shift uses Rubber Band built from source")
endif()
set_source_files_properties(${MIXXX_DSP_SRC} PROPERTIES
    INCLUDE_DIRECTORIES "${MIXXX_DSP_INCLUDES}"
    COMPILE_DEFINITIONS "${MIXXX_DSP_DEFINITIONS}"
    SKIP_PRECOMPILE_HEADERS ON
    SKIP_UNITY_BUILD_INCLUSION ON)
if (MSVC)
    # MSVC has no -ffinite-math by default to undo; /w silences fidlib as -w does.
    set_source_files_properties(${MIXXX_DSP_DIR}/lib/fidlib/fidlib.c PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(${MIXXX_DSP_DIR}/lib/fidlib/fidlib.c PROPERTIES COMPILE_OPTIONS "-fno-finite-math-only;-w")
endif()

# Note: NOTE_TRACK_API would only be needed with USE_MIDI, which AU4 does not enable.
set(MODULE_LINK
        au3wrap
        au3-basic-ui
        au3-command-parameters
        au3-components
        au3-dynamic-range-processor
        au3-fft
        au3-label-track
        au3-math
        au3-module-manager # via effects_base/internal/au3/au3effectsutils.h
        au3-mixer
        au3-preferences
        au3-strings
        au3-track
        au3-utility
        au3-wave-track
        au3-wave-track-fft
        shared
        ${MIXTAPE_RUBBERBAND_LIBS}
)
set(MODULE_LINK_PUBLIC au3-effects au3-builtin-effects)

set(MODULE_USE_UNITY OFF)

setup_module()

# GlideMix: the effect list and loader also see whether Pitch Shift is built.
if (MIXTAPE_RUBBERBAND_LIBS)
    target_compile_definitions(effects_builtin_collection PRIVATE MIXTAPE_HAS_RUBBERBAND)
endif()
