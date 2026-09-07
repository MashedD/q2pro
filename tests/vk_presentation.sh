#!/bin/sh
set -eu

# Run from the repository root, using an already configured Linux build.
build_dir=${1:-build-lin64}
test -f "$build_dir/config.h"
! rg -q 'FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE' src/refresh/vk_fsr3.cpp
cc -std=c11 -O1 -g -fms-extensions -ffunction-sections -fdata-sections \
    -DHAVE_CONFIG_H -D_GNU_SOURCE -DUSE_CLIENT=1 -DUSE_REF=1 \
    -I"$build_dir" -Iinc -I. $(pkg-config --cflags sdl2) tests/vk_presentation.c \
    -Wl,--gc-sections -lm -o "$build_dir/vk_presentation_test"
"$build_dir/vk_presentation_test"
