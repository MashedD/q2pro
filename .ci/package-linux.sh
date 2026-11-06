#!/bin/sh -eu
# Build the Linux x86_64 package in the given output directory. Run from the
# repository root with full git history.
#
# FFmpeg is disabled because its library versions differ between distributions,
# so a binary linked to one set would not start on many others.

out=$(realpath -m "$1")
ver=$(python3 version.py)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

meson setup "$tmp/build" --buildtype=release --wrap-mode=nofallback \
    -Davcodec=disabled -Dsystem-wide=false -Dconfig-file=q2config.cfg \
    -Danticheat-server=true \
    -Dwindows-crash-dumps=disabled -Dwerror=true
meson compile -C "$tmp/build"

pkg=$tmp/q2pro-$ver-linux-x86_64
mkdir -p "$pkg/baseq2"
cp "$tmp/build/q2pro" "$tmp/build/q2proded" "$pkg/"
cp "$tmp/build/gamex86_64.so" src/client/ui/q2pro.menu "$pkg/baseq2/"
# Default settings, packed so that they take priority over pak0's default.cfg.
(cd data/baseq2 && zip -q -X -9 "$pkg/baseq2/q2pro.pkz" default.cfg)
strip "$pkg/q2pro" "$pkg/q2proded" "$pkg/baseq2/gamex86_64.so"
cp LICENSE "$pkg/LICENSE.txt"
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

This build needs SDL2, OpenAL, libpng, libjpeg, zlib and libcurl from your
distribution. Ogg music and cinematics need FFmpeg, so build from source for
those: see BUILDING.md in the source archive.
END

mkdir -p "$out"
fakeroot tar -czf "$out/q2pro-$ver-linux-x86_64.tar.gz" -C "$tmp" "$(basename "$pkg")"
