#!/bin/sh -eu
# Cross-compile the Windows client and server packages for one architecture,
# with FFmpeg statically linked for Ogg music and cinematics. The first argument
# is win32 or win64, the second the output directory. Run from the repository
# root with full git history, on a system with MinGW-w64.

arch=$1
out=$(realpath -m "$2")

# Pin FFmpeg so that a release can be rebuilt from the same sources.
ffmpeg_tag=n8.1.3

case $arch in
win32) triplet=i686-w64-mingw32 ; cpu=x86 ; suffix= ;;
win64) triplet=x86_64-w64-mingw32 ; cpu=x86_64 ; suffix=64 ;;
*) echo "unknown architecture: $arch" >&2 ; exit 2 ;;
esac

src=$(pwd)
ver=$(python3 version.py)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

git clone --depth=1 --branch "$ffmpeg_tag" https://github.com/FFmpeg/FFmpeg.git "$tmp/ffmpeg"
# configure-ffmpeg.sh runs ../configure, so build in a directory inside the checkout.
mkdir "$tmp/ffmpeg/build"
cd "$tmp/ffmpeg/build"
"$src/.ci/configure-ffmpeg.sh" "--$arch" "$tmp/ffmpeg-prefix"
make -j"$(nproc)" install

export PKG_CONFIG_SYSROOT_DIR="$tmp/ffmpeg-prefix"
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_SYSROOT_DIR/lib/pkgconfig"

cd "$src"
meson setup "$tmp/build" --cross-file ".ci/$triplet.txt" \
    --auto-features=enabled --fatal-meson-warnings -Dwerror=true \
    -Dwrap_mode=forcefallback \
    -Dgame-build-options=optimization=s,b_lto=true \
    -Dsdl2=disabled -Dwayland=disabled -Dx11=disabled
meson compile -C "$tmp/build"

cd "$tmp/build"
"$triplet-strip" q2pro.exe q2proded.exe "game$cpu.dll"
mkdir -p "$out" baseq2
cp "$src/src/client/ui/q2pro.menu" baseq2/
mv "game$cpu.dll" baseq2/
mv q2pro.exe "q2pro$suffix.exe"
mv q2proded.exe "q2proded$suffix.exe"

sed -e "s/##VER##/$ver/" -e "s/##DATE##/$(date -R)/" \
    "$src/.ci/readme-template.txt" > README
unix2dos -k -n "$src/LICENSE" LICENSE.txt README README.txt \
    "$src/doc/client.asciidoc" MANUAL.txt
zip -9 "$out/q2pro-$ver-$arch-client.zip" "q2pro$suffix.exe" LICENSE.txt \
    MANUAL.txt README.txt baseq2/q2pro.menu "baseq2/game$cpu.dll"

unix2dos -k -n "$src/doc/server.asciidoc" MANUAL.txt
zip -9 "$out/q2pro-$ver-$arch-server.zip" "q2proded$suffix.exe" LICENSE.txt \
    MANUAL.txt README.txt
