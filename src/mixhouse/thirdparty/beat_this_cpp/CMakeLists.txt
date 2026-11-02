# beat_this_cpp -- neural beat/downbeat tracker, see README.mixhouse.md.
# Compiled the same way the parked MixHouse DJ fork compiled it (its
# top-level CMakeLists.txt "BEAT_THIS_CPP" block): the five library sources,
# C++20, -fno-fast-math, linked against a prebuilt ONNX Runtime.

set(MIXHOUSE_ONNXRUNTIME_DIR "${CMAKE_SOURCE_DIR}/mixhouse_deps/onnxruntime-1.18.0"
    CACHE PATH "Prebuilt ONNX Runtime used by MixHouse downbeat detection")
set(MIXHOUSE_BEAT_THIS_MODEL "${CMAKE_SOURCE_DIR}/mixhouse_deps/beat_this/beat_this.onnx"
    CACHE FILEPATH "Beat This! ONNX model used by MixHouse downbeat detection")

add_library(mixhouse_onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(mixhouse_onnxruntime PROPERTIES
    IMPORTED_LOCATION "${MIXHOUSE_ONNXRUNTIME_DIR}/lib/libonnxruntime.1.18.0.dylib"
    INTERFACE_INCLUDE_DIRECTORIES "${MIXHOUSE_ONNXRUNTIME_DIR}/include")

add_library(mixhouse_beatthis STATIC EXCLUDE_FROM_ALL
    Source/beat_this_api.cpp
    Source/MelSpectrogram.cpp
    Source/InferenceProcessor.cpp
    Source/Postprocessor.cpp
    Source/DBNPostprocessor.cpp
)
set_target_properties(mixhouse_beatthis PROPERTIES
    UNITY_BUILD OFF
    DISABLE_PRECOMPILE_HEADERS ON)
target_compile_features(mixhouse_beatthis PRIVATE cxx_std_20)
target_compile_options(mixhouse_beatthis PRIVATE -fno-fast-math -w)
# One ORT thread per model: several single-threaded models beat two
# default-threaded ones (README.mixhouse.md, measured).
target_compile_definitions(mixhouse_beatthis PRIVATE BEAT_THIS_INTRA_OP_THREADS=1)
target_include_directories(mixhouse_beatthis
    PUBLIC  ${CMAKE_CURRENT_LIST_DIR}/Source
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/pocketfft ${CMAKE_CURRENT_LIST_DIR}/r8brain)
target_link_libraries(mixhouse_beatthis PUBLIC mixhouse_onnxruntime)
target_compile_definitions(mixhouse_beatthis INTERFACE
    MIXHOUSE_BEAT_THIS_MODEL_PATH="${MIXHOUSE_BEAT_THIS_MODEL}")
