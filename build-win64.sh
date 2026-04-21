#!/usr/bin/env bash
set -Eeuo pipefail

meson setup build-win64 \
    --cross-file cross-mingw64.txt \
    --buildtype=release \
    -Ddefault_library=static \
    -Dc_link_args="-static -static-libgcc" \
    --force-fallback-for=zlib,libjpeg,libpng \
    -Dqal-hard-linked=true \
    -Danticheat-server=true || :
meson compile -C build-win64 -j$(nproc)
chmod -x build-win64/gamex86_64.dll
x86_64-w64-mingw32-strip build-win64/gamex86_64.dll
x86_64-w64-mingw32-strip build-win64/q2pro.exe
x86_64-w64-mingw32-strip build-win64/q2proded.exe

