#!/usr/bin/env bash
set -Eeuo pipefail

source_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$source_root"

triplet=i686-w64-mingw32
target_root="/usr/$triplet"
static_lib_dir="$target_root/static/lib"
openal_prefix="$source_root/build-win32/openal"

required_archives=(
    "$target_root/lib/libcurl.a"
    "$target_root/lib/libnghttp2.a"
    "$target_root/lib/libpsl.a"
    "$target_root/lib/libidn2.a"
    "$target_root/lib/libzstd.a"
    "$target_root/lib/libunistring.a"
    "$target_root/lib/libssh2.a"
    "$target_root/lib/libssl.a"
    "$target_root/lib/libcrypto.a"
    "$target_root/lib/libiconv.a"
    "$static_lib_dir/libbrotlidec.a"
    "$static_lib_dir/libbrotlicommon.a"
)
for archive in "${required_archives[@]}"; do
    if [[ ! -f "$archive" ]]; then
        echo "missing static MinGW archive: $archive" >&2
        exit 1
    fi
done

bash "$source_root/build-openal-win.sh" \
    i686-w64-mingw32 "$openal_prefix"

meson_args=(
    --cross-file cross-mingw32.txt \
    --buildtype=release \
    -Ddefault_library=static \
    -Dc_link_args='-static -static-libgcc -D_WIN32_WINNT=0x0501 -DNTDDI_VERSION=0x05010200' \
    -Dcpp_link_args="-static -static-libgcc -static-libstdc++ -L$static_lib_dir" \
    --force-fallback-for=libcurl,zlib,libjpeg,libpng \
    -Dqal-static-prefix="$openal_prefix" \
    -Dqal-hard-linked=true \
    -Danticheat-server=true \
    -Dvulkan=enabled
)
if [[ -f build-win32/meson-private/coredata.dat ]]; then
    meson setup --reconfigure build-win32 "${meson_args[@]}"
else
    meson setup build-win32 "${meson_args[@]}"
fi
meson compile -C build-win32
chmod -x build-win32/gamex86.dll
i686-w64-mingw32-strip build-win32/gamex86.dll
i686-w64-mingw32-strip build-win32/q2pro.exe
i686-w64-mingw32-strip build-win32/q2proded.exe
mv build-win32/q2pro.exe build-win32/q2pro-x86.exe
mv build-win32/q2proded.exe build-win32/q2proded-x86.exe

for executable in build-win32/q2pro-x86.exe build-win32/q2proded-x86.exe; do
    unexpected_imports=$(i686-w64-mingw32-objdump -p "$executable" |
        awk '/DLL Name:/{print $3}' |
        grep -E '^(libgcc|libstdc\+\+|libwinpthread|libcurl|libssl|libcrypto|libbrotli|libidn2|libnghttp2|libpsl|libssh2|libzstd|libunistring|libiconv|libcharset|zlib1)' || true)
    if [[ -n "$unexpected_imports" ]]; then
        echo "unexpected MinGW runtime imports in $executable:" >&2
        echo "$unexpected_imports" >&2
        exit 1
    fi
done
