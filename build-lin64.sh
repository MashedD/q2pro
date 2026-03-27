#!/usr/bin/env bash
set -Eeuo pipefail

meson setup build-lin64 --buildtype=release -Danticheat-server=true || :
meson compile -C build-lin64

