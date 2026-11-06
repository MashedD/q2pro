#!/bin/sh -eu
# Check that Linux binaries only need libraries that every current distribution
# provides under the same name, and no newer glibc than the oldest supported
# distribution has. The arguments are the binaries to check.
#
# A library that is missing or named differently on the player's system stops
# the game with "error while loading shared libraries", as libcurl-gnutls.so.4
# and libjpeg.so.8 did in the first Linux package on Fedora-based systems.

# glibc of Ubuntu 22.04, the oldest distribution the package supports.
#
# libcurl is deliberately missing: its name differs between distributions,
# so the package loads it at runtime instead (meson option curl-dlopen).
max_glibc=2.35

allowed="
ld-linux-x86-64.so.2 libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0
libz.so.1 libpng16.so.16
libSDL2-2.0.so.0 libX11.so.6 libXi.so.6 libGL.so.1 libEGL.so.1
libwayland-client.so.0 libwayland-cursor.so.0 libwayland-egl.so.1
libdecor-0.so.0
"
allowed=" $(printf '%s' "$allowed" | tr '\n' ' ') "

status=0
for bin in "$@"; do
    for lib in $(objdump -p "$bin" | awk '$1 == "NEEDED" { print $2 }'); do
        case "$allowed" in
            *" $lib "*) ;;
            *) echo "$bin needs $lib, which is not available everywhere" >&2
               status=1 ;;
        esac
    done

    need=$(objdump -T "$bin" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' |
           sort -uV | tail -n 1)
    if [ -n "$need" ] &&
       [ "$(printf '%s\n%s\n' "$need" "$max_glibc" | sort -V | tail -n 1)" != "$max_glibc" ]; then
        echo "$bin needs glibc $need, newer than $max_glibc" >&2
        status=1
    fi
done

exit $status
