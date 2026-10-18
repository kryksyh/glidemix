# mp3guessenc 0.27.4 (LGPL 2.1, COPYING), copied verbatim from Mixxx's lib/,
# which patched in mp3guessenc_timing_shift_case(). Tells which MP3 encoder
# case a file is, so rekordbox's beat grids, cues and phrases can be moved
# onto our decoder's clock (Mixxx: RekordboxFeature, PR #2119).
add_library(mixhouse_mp3guessenc STATIC EXCLUDE_FROM_ALL
    mp3guessenc.c
    tags.c
    decode.c
    bit_utils.c
)
set_target_properties(mixhouse_mp3guessenc PROPERTIES
    UNITY_BUILD OFF
    DISABLE_PRECOMPILE_HEADERS ON
    POSITION_INDEPENDENT_CODE ON)
if (WIN32)
    target_compile_definitions(mixhouse_mp3guessenc PRIVATE __WINDOWS__ _CRT_SECURE_NO_WARNINGS)
endif()
if (NOT MSVC)
    target_compile_options(mixhouse_mp3guessenc PRIVATE -w)
endif()
target_include_directories(mixhouse_mp3guessenc SYSTEM PUBLIC ${CMAKE_CURRENT_LIST_DIR})
