# SPDX-License-Identifier: GPL-3.0-only
#
# MixHouse DJ 2 -- the DJ "brain" brought into Audacity.
#
# This module reads Mixxx's library DATABASE, not its code: no Mixxx headers, no
# ControlObject, no QtWidgets. That keeps the first brain component free of the
# engine port, which is a separate and much larger job.

declare_module(mixhouse)

set(MODULE_SRC
    ${CMAKE_CURRENT_LIST_DIR}/mixhousemodule.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousemodule.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhousetypes.h
    ${CMAKE_CURRENT_LIST_DIR}/glidemixpaths.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhouseaudiosource.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousehdwaveform.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousehdwaveform.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousehdwaveforms.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousehdwaveforms.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhouseaudiosource.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhousenativepath.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhousebeats.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousebeats.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhouselibrary.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouselibraryschema.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhouselibraryconfiguration.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhousetempomap.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhousetransitions.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhousetransitionspec.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousetransitionspec.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhousetransitionconfiguration.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousetransitionconfiguration.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousetransitionconfiguration.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhousekeymatch.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhouseclipcolors.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseclipcolors.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseclipcolors.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhousestems.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousestems.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousestems.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhouseexternallibrary.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseexternallibrary.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imixhouserekordboxphrases.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouserekordboxphrases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouserekordboxphrases.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhouserekordboxxml.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouserekordboxxml.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouserekordboxxml.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhouselexicon.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouselexicon.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouselexicon.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousekeymatch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousekeymatch.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousetransitions.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousetransitions.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousetempomap.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousetempomap.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouselibraryconfiguration.cpp
    ${CMAKE_CURRENT_LIST_DIR}/imixhouselicence.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousetracklist.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousetracklist.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouselicence.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouselicence.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouselibraryconfiguration.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhouserekordboxxmlwriter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhouserekordboxxmlwriter.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhousesmartplaylist.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousesmartplaylist.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhousesearchquery.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousesearchquery.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhousewaveform.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousephrasedetector.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousefiletags.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousefiletags.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhousephrasedetector.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhousewaveform.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhouseanalyzer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousephrasemodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousephrasemodel.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhouseanalyzer.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/openkeyscanclient.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/openkeyscanclient.h
    ${CMAKE_CURRENT_LIST_DIR}/mixhousecoverart.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mixhousecoverart.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouselibrary.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouselibrary.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouselibraryscanner.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouselibraryscanner.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseanalyzerworker.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseanalyzerworker.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseanalysiswriter.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseuiactions.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseuiactions.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseactionscontroller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseactionscontroller.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousepreviewplayer.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhousegrideditor.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousegrideditor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousegrideditor.h
    )

# Library click-to-preview audio -- see mixhousepreviewplayer.h.
if (OS_IS_MAC)
    set(MIXHOUSE_PREVIEW_PLAYER_SRC ${CMAKE_CURRENT_LIST_DIR}/internal/platform/macos/mixhousepreviewplayer.mm)
    set_source_files_properties(${MIXHOUSE_PREVIEW_PLAYER_SRC} PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
        SKIP_UNITY_BUILD_INCLUSION ON
        SKIP_PRECOMPILE_HEADERS ON)
elseif (OS_IS_WIN)
    set(MIXHOUSE_PREVIEW_PLAYER_SRC ${CMAKE_CURRENT_LIST_DIR}/internal/platform/win/mixhousepreviewplayer.cpp)
    set_source_files_properties(${MIXHOUSE_PREVIEW_PLAYER_SRC} PROPERTIES
        SKIP_UNITY_BUILD_INCLUSION ON
        SKIP_PRECOMPILE_HEADERS ON)
else()
    set(MIXHOUSE_PREVIEW_PLAYER_SRC ${CMAKE_CURRENT_LIST_DIR}/internal/platform/stub/mixhousepreviewplayer.cpp)
endif()
list(APPEND MODULE_SRC ${MIXHOUSE_PREVIEW_PLAYER_SRC})

# Beat-to-bar alignment of library tracks loaded into the project. Talks to
# Audacity's au3 clip model directly (like importexport's TempoDetection), so
# it's kept out of the unity build where au3 and muse headers would collide.
set(MIXHOUSE_ALIGNER_SRC
    ${CMAKE_CURRENT_LIST_DIR}/imixhousetrackaligner.h
    ${CMAKE_CURRENT_LIST_DIR}/imixhousebeatgrids.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousebeatgrids.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousebeatgrids.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousetrackaligner.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousetrackaligner.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseprojectaligner.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseprojectaligner.h)
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseprojectaligner.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousebeatgrids.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/internal/mixhouseclipcolors.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousestems.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/internal/mixhousetempomap.cpp PROPERTIES
    SKIP_UNITY_BUILD_INCLUSION ON)
list(APPEND MODULE_SRC ${MIXHOUSE_ALIGNER_SRC})

set(MODULE_LINK
    Qt::Sql
    Qt::Network        # licence activation (Polar)
    Qt::Quick          # QQuickImageProvider, for the cover-art image:// provider
    SndFile::sndfile   # wav/aiff/flac/ogg/opus, already in the build
    mpg123::libmpg123  # mp3, already in the build
    TagLib::tag        # embedded cover-art extraction (ID3v2/Xiph/MP4)
    mixhouse_qmdsp     # real mixxx's own BPM/key analysis engine (lib/qm-dsp), copied verbatim
    au3wrap            # DomAccessor/DomConverter, for beat-to-bar alignment
    au3-wave-track     # WaveClip raw tempo / stretch
    au3-mixer          # tempo lane playback warp (Mixer::WarpOptions::DefaultWarp)
    au3-stretching-sequence  # tempo lane pitch compensation (PlaybackSpeedCurve)
    mixhouse_mp3guessenc     # rekordbox MP3 timing offsets (thirdparty/mp3guessenc)
    au3-mixtape-engine       # the Beatgrid editor's deck
    )
if (OS_IS_MAC)
    list(APPEND MODULE_LINK "-framework AVFoundation" "-framework AudioToolbox" "-framework CoreAudio")
endif()
if (OS_IS_WIN)
    # Media Foundation: library preview and the m4a/aac/alac decoder.
    list(APPEND MODULE_LINK mfplat mfreadwrite mfuuid ole32)
endif()

# Beat This! neural downbeat detection (thirdparty/beat_this_cpp,
# README.mixhouse.md). Needs a prebuilt ONNX Runtime 1.18 in mixhouse_deps/:
# on macOS the dylib the parked MixHouse DJ fork used; on Windows Microsoft's
# onnxruntime-win-x64-1.18.0 zip unpacked as is (lib/onnxruntime.lib + .dll).
if (OS_IS_MAC AND EXISTS "${CMAKE_SOURCE_DIR}/mixhouse_deps/onnxruntime-1.18.0/lib/libonnxruntime.1.18.0.dylib")
    set(MIXHOUSE_HAVE_BEAT_THIS ON)
    list(APPEND MODULE_LINK mixhouse_beatthis)
elseif (OS_IS_WIN AND EXISTS "${CMAKE_SOURCE_DIR}/mixhouse_deps/onnxruntime-win-x64-1.18.0/lib/onnxruntime.lib"
        AND EXISTS "${CMAKE_SOURCE_DIR}/mixhouse_deps/onnxruntime-win-x64-1.18.0/lib/onnxruntime.dll")
    set(MIXHOUSE_HAVE_BEAT_THIS ON)
    list(APPEND MODULE_LINK mixhouse_beatthis)
elseif (OS_IS_WIN)
    message(STATUS "MixHouse: mixhouse_deps/onnxruntime-win-x64-1.18.0 not found -- downbeat detection disabled")
else()
    message(STATUS "MixHouse: mixhouse_deps/onnxruntime-1.18.0 not found -- downbeat detection disabled")
endif()

add_subdirectory(thirdparty/qm-dsp)
add_subdirectory(thirdparty/mp3guessenc)
if (MIXHOUSE_HAVE_BEAT_THIS)
    add_subdirectory(thirdparty/beat_this_cpp)
endif()

setup_module()
if (MIXHOUSE_HAVE_BEAT_THIS)
    target_compile_definitions(mixhouse PRIVATE MIXHOUSE_HAVE_BEAT_THIS)
    target_compile_definitions(mixhouse PRIVATE
        MIXHOUSE_PHRASE_MODEL_DEV_PATH="${CMAKE_CURRENT_LIST_DIR}/models/glidemix_phrases.onnx"
        MIXHOUSE_BEAT_THIS_SMALL_MODEL_PATH="${CMAKE_CURRENT_LIST_DIR}/models/beat_this_small.onnx")
    if (OS_IS_WIN)
        # Beside GlideMix.exe: the runtime, and the models where
        # mixHouseModelPath() looks (<exe>/models/). The installer copies the
        # whole install prefix.
        install(FILES "${MIXHOUSE_ONNXRUNTIME_DIR}/lib/onnxruntime.dll" DESTINATION bin)
        install(FILES "${MIXHOUSE_BEAT_THIS_MODEL}" "${CMAKE_CURRENT_LIST_DIR}/models/glidemix_phrases.onnx"
                      "${CMAKE_CURRENT_LIST_DIR}/models/beat_this_small.onnx"
                DESTINATION bin/models)
    endif()
endif()
# Development builds may also use the helpers' developer checkouts in
# ~/Developer; a Release build (what testers get) only what a user can have.
if (NOT GLIDEMIX_TESTER_BUILD)   # see appshell_qml's GLIDEMIX_RELEASE_BUILD
    target_compile_definitions(mixhouse PRIVATE $<$<NOT:$<CONFIG:Release>>:GLIDEMIX_DEV_HELPERS>)
endif()

add_subdirectory(qml/Audacity/MixHouse)

# Developer tool: the app's track analysis from the command line, for measuring
# the detectors (tools/glidemix_analyze.cpp). Not part of the app or Release.
if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    add_executable(glidemix-analyze tools/glidemix_analyze.cpp)
    target_include_directories(glidemix-analyze PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/..
                               $<TARGET_PROPERTY:mixhouse,INCLUDE_DIRECTORIES>)
    target_compile_definitions(glidemix-analyze PRIVATE $<TARGET_PROPERTY:mixhouse,COMPILE_DEFINITIONS>)
    target_link_libraries(glidemix-analyze PRIVATE mixhouse Qt::Core Qt::Gui)
endif()
