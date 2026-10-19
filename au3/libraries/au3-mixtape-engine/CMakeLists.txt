#[[
GlideMix playback engine: arrangement snapshots, one Rubber Band
voice per clip, a renderer per transport.
]]

set( TARGET au3-mixtape-engine )

set( SOURCES
   MixtapeArrangement.cpp
   MixtapeArrangement.h
   MixtapeClipVoice.cpp
   MixtapeClipVoice.h
   MixtapeDeck.cpp
   MixtapeDeck.h
   MixtapeEffects.cpp
   MixtapeEffects.h
   MixtapeLoudness.cpp
   MixtapeLoudness.h
   MixtapeRenderedSequence.cpp
   MixtapeDemo.h
   MixtapeDemo.cpp
   MixtapeDemoVoice.h
   MixtapeRenderedSequence.h
   MixtapeRenderer.cpp
   MixtapeRenderer.h
   MixtapeTransport.cpp
   MixtapeTransport.h
   MixtapeWorkers.cpp
   MixtapeWorkers.h
)
set( LIBRARIES
PUBLIC
   au3-audio-devices
   au3-math
   au3-mixer
   au3-project
   au3-realtime-effects
   au3-track
   au3-wave-track
PRIVATE
   portaudio::portaudio
   wxBase
)
audacity_library( ${TARGET} "${SOURCES}" "${LIBRARIES}"
   "" ""
)

# Rubber Band 4, vendored with the DJ Effects for macOS arm64; Windows and Linux
# build it from source (buildscripts/cmake/GlideMixRubberBand.cmake). Intel Mac
# builds the engine without time stretching.
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
   set(MIXTAPE_ENGINE_RUBBERBAND_ROOT
      "${CMAKE_CURRENT_LIST_DIR}/../../../src/effects/builtin_collection/mixxx/thirdparty/rubberband-macos-arm64")
endif()
if(MIXTAPE_ENGINE_RUBBERBAND_ROOT AND EXISTS "${MIXTAPE_ENGINE_RUBBERBAND_ROOT}/lib/librubberband.a")
   target_include_directories(${TARGET} PRIVATE "${MIXTAPE_ENGINE_RUBBERBAND_ROOT}/include")
   target_compile_definitions(${TARGET} PRIVATE MIXTAPE_ENGINE_HAS_RUBBERBAND)
   target_link_libraries(${TARGET} PUBLIC
      "${MIXTAPE_ENGINE_RUBBERBAND_ROOT}/lib/librubberband.a"
      "${MIXTAPE_ENGINE_RUBBERBAND_ROOT}/lib/libsamplerate.a"
      "${MIXTAPE_ENGINE_RUBBERBAND_ROOT}/lib/libfftw3.a")
   if(APPLE)
      target_link_libraries(${TARGET} PUBLIC "-framework Accelerate")
   endif()
elseif(NOT APPLE)
   # Windows / Linux: Rubber Band built from source (built-in FFT and resampler).
   include("${CMAKE_CURRENT_LIST_DIR}/../../../buildscripts/cmake/GlideMixRubberBand.cmake")
   target_compile_definitions(${TARGET} PRIVATE MIXTAPE_ENGINE_HAS_RUBBERBAND)
   target_link_libraries(${TARGET} PUBLIC glidemix_rubberband)
else()
   message(WARNING "Mixtape engine: no Rubber Band for this platform -- clips play unstretched")
endif()

option(MIXTAPE_BUILD_ENGINE_TESTS "Build the Mixtape playback engine tests" ON)
if(MIXTAPE_BUILD_ENGINE_TESTS)
   add_executable(mixtape-engine-tests
      tests/MixtapeEngineTests.cpp
      ../au3-stretching-sequence/tests/MockSampleBlock.cpp
   )
   target_link_libraries(mixtape-engine-tests PRIVATE au3-mixtape-engine au3-wave-track au3-project-file-io wxBase)
   if(APPLE)
      target_link_libraries(mixtape-engine-tests PRIVATE
         "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreServices" "-framework CoreFoundation")
   endif()
   apply_wxbase_restrictions(mixtape-engine-tests)
endif()
