set(DEP_VERSION 2.3.2)

# TagLib is only used here for its metadata/tag reading -- specifically embedded
# cover-art extraction (ID3v2 APIC, FLAC/Xiph picture blocks, MP4 covr atom).
# It parses container/tag structures itself and does NOT link against the
# actual codec libraries (ogg/vorbis/flac/opus), so it needs no DEP_DEPENDS.

set(DEP_SOURCE_URL    "https://github.com/taglib/taglib/releases/download/v2.3.2/taglib-2.3.2.tar.gz")
set(DEP_SOURCE_SHA256 "3ca2d8afaa7f1cf7f6ed10e511ebc368bfacd6dcaa3dbfa690b89e502e8963dc")

set(DEP_CMAKE_ARGS
    -DBUILD_SHARED_LIBS=ON
    -DBUILD_EXAMPLES=OFF
    -DBUILD_BINDINGS=OFF
    -DBUILD_TESTING=OFF
    -DWITH_ZLIB=OFF
    -DWITH_ASF=OFF
    -DWITH_DSF=OFF
    -DWITH_MATROSKA=OFF
    -DWITH_MOD=OFF
    -DWITH_SHORTEN=OFF
    -DWITH_TRUEAUDIO=OFF
    -DWITH_APE=ON
    -DWITH_MP4=ON
    -DWITH_RIFF=ON
    -DWITH_VORBIS=ON
)

set(DEP_LICENSE_FILES COPYING.LGPL COPYING.MPL)
