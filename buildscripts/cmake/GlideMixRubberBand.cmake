# GlideMix: Rubber Band 4.0.0 (GPL-2.0-or-later) built from source on the
# platforms that have no vendored prebuilt libraries (everything but macOS
# arm64: Windows MSVC, Linux).
#
# Uses Rubber Band's single-file build (single/RubberBandSingle.cpp), which
# needs neither FFTW nor libsamplerate: it selects the built-in FFT and the
# built-in BQ resampler, and runs without threads.
#
# Provides the static target glidemix_rubberband (alias GlideMix::RubberBand);
# its PUBLIC include dir is the one holding rubberband/RubberBandStretcher.h.
# Include this file only when the vendored macOS libraries are not used.
#
# To build offline, point FETCHCONTENT_SOURCE_DIR_RUBBERBAND at a checkout of
# the v4.0.0 tag.

if (TARGET glidemix_rubberband)
    return()
endif()

include(FetchContent)

# Pinned to the commit the v4.0.0 tag points at (git verifies the content).
FetchContent_Declare(rubberband
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG 1d95888bec3ae0a17c0c4af791810d5a63f6bc35 # v4.0.0
    GIT_SHALLOW FALSE
    )
# Rubber Band has no CMakeLists.txt, so this only fetches the sources.
FetchContent_MakeAvailable(rubberband)

add_library(glidemix_rubberband STATIC "${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp")
add_library(GlideMix::RubberBand ALIAS glidemix_rubberband)
target_include_directories(glidemix_rubberband PUBLIC "${rubberband_SOURCE_DIR}")
target_compile_features(glidemix_rubberband PRIVATE cxx_std_17)
set_target_properties(glidemix_rubberband PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (WIN32)
    # As in Rubber Band's own meson.build and Visual C++ project.
    target_compile_definitions(glidemix_rubberband PRIVATE NOMINMAX _USE_MATH_DEFINES)
    if (MSVC)
        target_compile_definitions(glidemix_rubberband PRIVATE __MSVC__)
        # Third-party code: no warnings of ours; /O2 in Release comes from CMake.
        target_compile_options(glidemix_rubberband PRIVATE /w)
    endif()
elseif (APPLE)
    target_link_libraries(glidemix_rubberband PUBLIC "-framework Accelerate")
else()
    target_compile_definitions(glidemix_rubberband PRIVATE HAVE_POSIX_MEMALIGN)
    target_compile_options(glidemix_rubberband PRIVATE -w)
    # std::atomic::is_lock_free (R3 engine) needs libatomic with clang on some
    # Linux toolchains; link it only when a plain link fails (as meson.build does).
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
        #include <atomic>
        struct S { void *a, *b; };
        int main() { std::atomic<S> s; return s.is_lock_free() ? 0 : 1; }"
        GLIDEMIX_RB_ATOMIC_NO_LIB)
    if (NOT GLIDEMIX_RB_ATOMIC_NO_LIB)
        target_link_libraries(glidemix_rubberband PUBLIC atomic)
    endif()
endif()
