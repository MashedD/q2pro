#!/usr/bin/env bash
set -Eeuo pipefail

meson setup build-lin64 --buildtype=release -Dsystem-wide=false -Danticheat-server=true || :
meson compile -C build-lin64
sstrip build-lin64/gamex86_64.so
sstrip build-lin64/q2pro

