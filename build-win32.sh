#!/usr/bin/env bash
set -Eeuo pipefail

meson setup build-win32 \
    --cross-file cross-mingw32.txt \
    --buildtype=release \
    -Ddefault_library=static \
    -Dc_link_args='-static -static-libgcc -D_WIN32_WINNT=0x0501 -DNTDDI_VERSION=0x05010200' \
    --force-fallback-for=libcurl,zlib,libjpeg,libpng \
    -Dqal-hard-linked=true \
    -Danticheat-server=true || :
meson compile -C build-win32
chmod -x build-win32/gamex86.dll
i686-w64-mingw32-strip build-win32/gamex86.dll
i686-w64-mingw32-strip build-win32/q2pro.exe
i686-w64-mingw32-strip build-win32/q2proded.exe
mv build-win32/q2pro.exe build-win32/q2pro-x86.exe
mv build-win32/q2proded.exe build-win32/q2proded-x86.exe

