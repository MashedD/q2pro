#!/usr/bin/env bash
set -Eeuo pipefail

meson setup build-win64 \
    --cross-file cross-mingw64.txt \
    --buildtype=release \
    -Ddefault_library=static \
    -Dc_link_args="-static -static-libgcc" \
    --force-fallback-for=zlib,libjpeg,libpng \
    -Danticheat-server=true || :
meson compile -C build-win64 -j$(nproc)
chmod -x build-win64/gamex86_64.dll

