#!/usr/bin/env bash
set -Eeuo pipefail

openal_prefix="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/build-win64/openal"
bash "$(dirname -- "${BASH_SOURCE[0]}")/build-openal-win.sh" \
    x86_64-w64-mingw32 "$openal_prefix"

meson setup build-win64 \
    --cross-file cross-mingw64.txt \
    --buildtype=release \
    -Ddefault_library=static \
    -Dc_link_args="-static -static-libgcc" \
    --force-fallback-for=zlib,libjpeg,libpng \
    -Dqal-static-prefix="$openal_prefix" \
    -Dqal-hard-linked=true \
    -Danticheat-server=true \
    -Dvulkan=enabled
meson compile -C build-win64 -j"$(nproc)"
chmod -x build-win64/gamex86_64.dll
x86_64-w64-mingw32-strip build-win64/gamex86_64.dll
x86_64-w64-mingw32-strip build-win64/q2pro.exe
x86_64-w64-mingw32-strip build-win64/q2proded.exe
