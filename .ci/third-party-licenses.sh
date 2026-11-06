#!/bin/sh -eu
# Write the license texts of the libraries that Windows packages link
# statically into one file. Run from the repository root after meson setup has
# downloaded the subprojects. Usage: third-party-licenses.sh OUTPUT
#
# Directory names come from the wrap files, so updating a wrap needs no change
# here. A missing license file is an error: shipping without it would break the
# library's license terms.

out=$1

dir_of() {
    sed -n 's/^directory *= *//p' "subprojects/$1.wrap"
}

{
    echo "Third-party software included in this package"
    echo "=============================================="
    echo
    echo "These libraries are linked into the executables. Their licenses follow."
    echo "FFmpeg is covered separately in LICENSE-FFmpeg.txt."
    for entry in \
        "OpenAL Soft:openal-soft:COPYING BSD-3Clause" \
        "libcurl:libcurl:COPYING" \
        "libjpeg-turbo:libjpeg-turbo:LICENSE.md README.ijg" \
        "libpng:libpng:LICENSE" \
        "zlib-ng:zlib-ng:LICENSE.md"
    do
        name=${entry%%:*}
        rest=${entry#*:}
        wrap=${rest%%:*}
        files=${rest#*:}
        dir=subprojects/$(dir_of "$wrap")
        for f in $files; do
            if [ ! -f "$dir/$f" ]; then
                echo "missing $dir/$f" >&2
                exit 1
            fi
            echo
            echo
            echo "---- $name ($(basename "$dir")): $f ----"
            echo
            cat "$dir/$f"
        done
    done
} > "$out"
