#!/bin/sh -eu
# Build OpenAL Soft as a static library for Windows, for the qal-hard-linked build
# option. The first argument is the MinGW-w64 triplet, the second the install
# prefix. Run from the repository root.

triplet=$1
prefix=$(realpath -m "$2")

# The source comes from the wrap that already provides the OpenAL headers.
source_dir=$(pwd)/subprojects/openal-soft-1.23.1
if [ ! -f "$source_dir/CMakeLists.txt" ]; then
    meson subprojects download openal-soft
fi

# Backends that only exist on other systems are turned off so that CMake cannot
# pick them up from the build machine. HRTF data ships inside the library.
cmake -S "$source_dir" -B "$prefix/build" -G Ninja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER="$triplet-gcc" \
    -DCMAKE_CXX_COMPILER="$triplet-g++" \
    -DCMAKE_RC_COMPILER="$triplet-windres" \
    -DCMAKE_AR="$(command -v "$triplet-ar")" \
    -DCMAKE_RANLIB="$(command -v "$triplet-ranlib")" \
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
cmake --build "$prefix/build" --target install
