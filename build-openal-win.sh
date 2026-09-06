#!/usr/bin/env bash
set -Eeuo pipefail

if (($# != 2)); then
    echo "usage: $0 <mingw-triplet> <install-prefix>" >&2
    exit 2
fi

triplet=$1
prefix=$2
source_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_dir="$source_root/subprojects/openal-soft-1.23.1"
build_dir="$prefix/build"

if [[ ! -f "$source_dir/CMakeLists.txt" ]]; then
    meson subprojects download openal-soft
fi

if [[ ! -f "$source_dir/CMakeLists.txt" ]]; then
    echo "OpenAL Soft source is unavailable: $source_dir" >&2
    exit 1
fi

for tool in gcc g++ ar ranlib windres; do
    if ! command -v "$triplet-$tool" >/dev/null; then
        echo "missing MinGW tool: $triplet-$tool" >&2
        exit 1
    fi
done

cc=$(command -v "$triplet-gcc")
cxx=$(command -v "$triplet-g++")
ar=$(command -v "$triplet-ar")
ranlib=$(command -v "$triplet-ranlib")
windres=$(command -v "$triplet-windres")

mkdir -p "$prefix"

cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER="$cc" \
    -DCMAKE_CXX_COMPILER="$cxx" \
    -DCMAKE_RC_COMPILER="$windres" \
    -DCMAKE_AR="$ar" \
    -DCMAKE_RANLIB="$ranlib" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_CXX_FLAGS='-include stdint.h' \
    -DLIBTYPE=STATIC \
    -DALSOFT_UTILS=OFF \
    -DALSOFT_EXAMPLES=OFF \
    -DALSOFT_INSTALL=ON \
    -DALSOFT_INSTALL_CONFIG=OFF \
    -DALSOFT_INSTALL_HRTF_DATA=OFF \
    -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF \
    -DALSOFT_INSTALL_EXAMPLES=OFF \
    -DALSOFT_INSTALL_UTILS=OFF \
    -DALSOFT_BUILD_ROUTER=OFF \
    -DALSOFT_DLOPEN=OFF \
    -DALSOFT_BACKEND_PIPEWIRE=OFF \
    -DALSOFT_BACKEND_PULSEAUDIO=OFF \
    -DALSOFT_BACKEND_JACK=OFF \
    -DALSOFT_BACKEND_PORTAUDIO=OFF

build_jobs=${JOBS:-$(nproc 2>/dev/null || echo 1)}
cmake --build "$build_dir" --target install --parallel "$build_jobs"

if [[ ! -f "$prefix/lib/libOpenAL32.a" || ! -f "$prefix/include/AL/al.h" ]]; then
    echo "OpenAL Soft static installation is incomplete in $prefix" >&2
    exit 1
fi
