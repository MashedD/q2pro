#!/usr/bin/env bash
set -Eeuo pipefail

meson setup build-win32 \
    --cross-file cross-mingw32.txt \
    --buildtype=release \
    -Ddefault_library=static \
    -Dc_link_args='-static -static-libgcc -D_WIN32_WINNT=0x0501 -DNTDDI_VERSION=0x05010200' \
    --force-fallback-for=libcurl,zlib,libjpeg,libpng \
    -Danticheat-server=true || :
meson compile -C build-win32

