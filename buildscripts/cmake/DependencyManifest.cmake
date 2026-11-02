require_dep(expat)

if (AU_USE_LIBCURL)
    if (NOT OS_IS_WIN)
        require_dep(openssl SYSTEM)
    endif()
    require_dep(libcurl SYSTEM)
endif()

require_dep(ogg)
require_dep(vorbis)
require_dep(flac)
require_dep(opus)
require_dep(opusfile)
require_dep(lame)

# mpg123 asm optimizations on windows require yasm
if (OS_IS_WIN AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "[Aa][Rr][Mm]64")
    require_tool(yasm)
endif()

require_dep(mpg123)
require_dep(wavpack)
require_dep(libsndfile)
# TagLib -- embedded cover-art extraction for the MixHouse DJ library (Phase 8)
# libid3tag -- ID3 tags in MP3 and MP2 exports
# GlideMix: muse_deps at our pin has neither recipe; ours live in buildscripts/deps
# and are put in place here, so a fresh clone builds.
foreach(_recipe taglib libid3tag)
    if (NOT EXISTS "${EXTDEPS_DIR}/recipes/${_recipe}/spec.cmake")
        file(COPY "${CMAKE_CURRENT_LIST_DIR}/../deps/recipes/${_recipe}" DESTINATION "${EXTDEPS_DIR}/recipes")
    endif()
endforeach()
require_dep(taglib)
# zlib is also required by muse, but libid3tag needs it resolved first
require_dep(zlib)
require_dep(libid3tag)
if (OS_IS_WIN)
    require_source_dep(asiosdk)
    require_dep(portaudio REBUILD)
else()
    require_dep(portaudio)
endif()
require_dep(libpng)
require_dep(wxwidgets)


if (AU_MODULE_EFFECTS_LV2)
    require_source_dep(lv2sdk)
endif()
if (AU_MODULE_EFFECTS_VST)
    require_source_dep(vst3sdk)
endif()
require_source_dep(loop-tempo-estimator)
require_source_dep(tft)
require_source_dep(rapidjson)
require_source_dep(nyquist)
require_source_dep(pffft)
require_source_dep(sqlite)
require_source_dep(twolame)
require_source_dep(soxr)

if (AU_USE_SOUNDTOUCH)
    require_source_dep(soundtouch)
endif()

if (AU_USE_SBSMS)
    require_source_dep(sbsms)
endif()
