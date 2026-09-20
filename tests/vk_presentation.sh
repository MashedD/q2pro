#!/bin/sh
set -eu

# Run from any directory, using an already configured Linux build.
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
build_arg=${1:-build-lin64}
case $build_arg in
    /*) build_dir=$build_arg ;;
    *) build_dir=$repo_dir/$build_arg ;;
esac
test -f "$build_dir/config.h" || {
    echo "missing configured build: $build_dir/config.h" >&2
    exit 2
}
test -f "$repo_dir/src/refresh/vk_fsr3.cpp" || exit 2
if command -v rg >/dev/null 2>&1; then
    search_command='rg -q'
else
    search_command='grep -q'
fi
if $search_command 'FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE' \
       "$repo_dir/src/refresh/vk_fsr3.cpp"; then
    echo "unsupported FSR3 auto-exposure define is present" >&2
    exit 2
fi

cc=${CC:-cc}
if command -v pkg-config >/dev/null 2>&1; then
    sdl_cflags=$(pkg-config --cflags sdl2 2>/dev/null || true)
else
    sdl_cflags=
fi
set -- -std=c11 -O1 -g -fms-extensions -ffunction-sections -fdata-sections \
    -DHAVE_CONFIG_H -D_GNU_SOURCE -DUSE_CLIENT=1 -DUSE_REF=1 \
    -I"$build_dir" -I"$repo_dir/inc" -I"$repo_dir"
# SDL headers are optional for this harness on builds that expose them through
# the configured include path; use pkg-config flags when available.
if test -n "$sdl_cflags"; then
    # shellcheck disable=SC2086
    set -- "$@" $sdl_cflags
fi
set -- "$@" "$repo_dir/tests/vk_presentation.c" -Wl,--gc-sections -lm \
    -o "$build_dir/vk_presentation_test"
"$cc" "$@"
"$build_dir/vk_presentation_test"
