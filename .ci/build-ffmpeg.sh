#!/bin/sh -eu
# Build the small static FFmpeg that release packages link in, for Ogg Vorbis
# music and Ogg Theora cinematics. The first argument is linux, win32 or win64,
# the second the install prefix. The FFmpeg license is copied to
# PREFIX/LICENSE-FFmpeg.txt for the packages.
#
# configure-ffmpeg.sh chooses the decoders and disables everything that would
# need another library, so the result only depends on the C and math
# libraries.

target=$1
prefix=$(realpath -m "$2")
src=$(pwd)

# Pin FFmpeg so that a release can be rebuilt from the same sources.
ffmpeg_tag=n8.1.3

git clone --quiet --depth=1 --branch "$ffmpeg_tag" \
    https://github.com/FFmpeg/FFmpeg.git "$prefix/src"

# configure-ffmpeg.sh runs ../configure, so build in a directory inside the
# checkout.
mkdir "$prefix/src/build"
cd "$prefix/src/build"
"$src/.ci/configure-ffmpeg.sh" "--$target" "$prefix"
make -j"$(nproc)" install

cp "$prefix/src/COPYING.LGPLv2.1" "$prefix/LICENSE-FFmpeg.txt"
