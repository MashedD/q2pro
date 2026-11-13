#!/bin/sh -eu
# Build the Linux x86_64 package in the given output directory. Run from the
# repository root with full git history.
#
# The package has to start on any current distribution, not only the one it is
# built on. libjpeg's file name differs between distributions, so it is built
# in from its Meson wrap. libcurl's name differs too (libcurl-gnutls.so.4 on
# Debian and Ubuntu), so it is loaded at runtime under either name
# (-Dcurl-dlopen). Other libraries are taken from the system only if every
# distribution names them the same; check-linux-deps.sh enforces that.
# FFmpeg's library versions differ between distributions too, so a small
# FFmpeg for Ogg music and cinematics is built in (see build-ffmpeg.sh), as in
# the Windows packages.

out=$(realpath -m "$1")
ver=$(python3 version.py)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

./.ci/build-ffmpeg.sh linux "$tmp/ffmpeg-prefix"

# Only FFmpeg's static libraries exist in that prefix, so the linker takes
# those. libjpeg-turbo calls setenv(), which C11 without GNU extensions hides.
PKG_CONFIG_PATH="$tmp/ffmpeg-prefix/lib/pkgconfig" \
meson setup "$tmp/build" --buildtype=release --wrap-mode=nofallback \
    --force-fallback-for=libjpeg -Dlibjpeg-turbo:c_std=gnu11 -Dcurl-dlopen=true \
    -Dvulkan=enabled \
    -Davcodec=enabled -Dsystem-wide=false -Dconfig-file=q2config.cfg \
    -Danticheat-server=true \
    -Dwindows-crash-dumps=disabled -Dwerror=true
meson compile -C "$tmp/build"

pkg=$tmp/q2pro-$ver-linux-x86_64
mkdir -p "$pkg/baseq2"
cp "$tmp/build/q2pro" "$tmp/build/q2proded" "$pkg/"
cp "$tmp/build/gamex86_64.so" src/client/ui/q2pro.menu "$pkg/baseq2/"
# Package defaults, run by the engine right after the game's default.cfg.
(cd data/baseq2 && zip -q -X -9 "$pkg/baseq2/q2pro.pkz" q2pro-defaults.cfg materials/rt_reflections.mat)
strip "$pkg/q2pro" "$pkg/q2proded" "$pkg/baseq2/gamex86_64.so"
./.ci/check-linux-deps.sh "$pkg/q2pro" "$pkg/q2proded" "$pkg/baseq2/gamex86_64.so"
cp LICENSE "$pkg/LICENSE.txt"
cp "$tmp/ffmpeg-prefix/LICENSE-FFmpeg.txt" "$pkg/"
cp doc/client.asciidoc "$pkg/MANUAL.txt"
sed -e "s/##VER##/$ver/" -e "s/##DATE##/$(date -R)/" \
    .ci/readme-template.txt > "$pkg/README.txt"
cat >> "$pkg/README.txt" <<'END'

Extract the archive with tar, which keeps the files executable:

    tar -xzf q2pro-*-linux-x86_64.tar.gz

Some archive programs drop the executable permission. Then q2pro does not
start, or reports that baseq2/gamex86_64.so is not executable. Fix it from
the directory that contains q2pro:

    chmod +x q2pro q2proded baseq2/gamex86_64.so

This build runs on distributions with glibc 2.35 or newer, such as Ubuntu
22.04, Debian 12, Fedora, Arch, SteamOS and Bazzite. It uses SDL2, libpng
and zlib from your system, plus OpenAL for 3D sound and libcurl for HTTP
downloads if they are installed; most desktops have them already.
Ogg Vorbis music and Ogg Theora cinematics are built in, using FFmpeg under
the LGPL (see LICENSE-FFmpeg.txt).
END

mkdir -p "$out"
fakeroot tar -czf "$out/q2pro-$ver-linux-x86_64.tar.gz" -C "$tmp" "$(basename "$pkg")"
