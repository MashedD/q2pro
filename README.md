# Q2PRO

**As Skuller's repo was removed there are not many actively maintained
repositories. This one should be, so Pull Requests are welcomed!**

Q2PRO is an enhanced Quake 2 client and server for Windows and Linux. Supported
features include:

* unified OpenGL renderer with support for wide range of OpenGL versions
* enhanced console with persistent command history and autocompletion
* rendering / physics / packet rate separation
* syncing to GPU for reduced input lag with vsync on
* ZIP packfiles (.pkz)
* JPEG/PNG textures and screenshots
* MD3 and MD5 (re-release) models
* Ogg Vorbis music and Ogg Theora cinematics
* compatibility with re-release assets
* fast and secure HTTP downloads
* multichannel sound using OpenAL
* stereo WAV files support
* seeking in demos, recording from demos, server side multiview demos
* live game broadcasting capabilities
* network protocol extensions for larger maps
* eliminates frame overflows (even for legacy clients)
* won't crash if game data is corrupted

Q2PRO doesn't have releases. It is always recommended to use the latest nightly
build from the top of Releases page.

Linux binaries are not provided. Users are advised to build from source. See
BUILDING.md file for instructions.

For information on using and configuring Q2PRO, refer to client and server
manuals available in doc/ subdirectory.

# Fixes in this fork

- fixed playback of some demos
- fixed `ch_scale` so that `scr_scale` doesn't affect crosshair size
- don't draw crosshair on top of "Paused" image
- fixed compilation warnings
- fixed problem with displaying excessive items in address book after
  quickly switching from Server Browser

# Changes

- added win32/win64 cross compilation settings
- win64 build uses now `OpenAL64.dll` instead of `OpenAL32.dll`,
  so now win32 and win64 binaries can live next to each other
- lin64 build now looks for game files in current folder
  instead of `$HOME/.q2pro` as it was not XDG Base Directory Specification
  compliant
- change so that config file name on Linux is same as on Windows (q2config.cfg
  instead of config.cfg)
- added new scores layout for streaming, works only with OpenTDM. To turn it on:
```
set cl_stream_scoreboard "1"
set cl_stream_scoreboard_x "20" // margin
set cl_stream_scoreboard_offset "120" // position from the bottom
```

# Building

## Prerequisites

Dependencies might be missing and some are probably excessive.
I didn't optimize this as it's time/cost not effective for me.
Best might be to use Docker for the job.

```bash
# Tested on CachyOS
sudo pacman -S gcc meson ninja pkgconf

# Build zstd static version if missing in:
# /usr/x86_64-w64-mingw32/lib/libzstd.a
# /usr/i686-w64-mingw32/lib/libzstd.a
cd /tmp
git clone https://github.com/facebook/zstd.git
cd zstd/build/cmake

# Win64
cat <<EOF>toolchain-mingw64.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
rm -rf build-cmake
cmake -S . -B build-cmake -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_STATIC=ON -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw64.cmake
cmake --build build-cmake
sudo cp build-cmake/lib/libzstd.a /usr/x86_64-w64-mingw32/lib/

# Win32
cat <<EOF>toolchain-mingw32.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)
set(CMAKE_C_COMPILER i686-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
rm -rf build-cmake
cmake -S . -B build-cmake -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_STATIC=ON -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw32.cmake
cmake --build build-cmake
sudo cp build-cmake/lib/libzstd.a /usr/i686-w64-mingw32/lib/

# For cross compilation
sudo pacman -S \
    mingw-w64-tools \
    mingw-w64-binutils \
    mingw-w64-crt \
    mingw-w64-gcc \
    mingw-w64-headers \
    mingw-w64-winpthreads
paru -S \
    mingw-w64-zlib \
    mingw-w64-zlib-ng \
    mingw-w64-ffmpeg \
    mingw-w64-pkg-config \
    mingw-w64-libpng \
    mingw-w64-libjpeg-turbo \
    mingw-w64-openal \
    mingw-w64-zstd
```

## Compilation

Review scripts before executing them.

```bash
./build-lin64.sh
./build-win32.sh
./build-win64.sh
```

## To play

Copy files from build folder to:

```
baseq2/gamex*.*
q2pro*
```

along with files/folders from non-remastered Quake 2:

```
baseq2/players/
baseq2/pak0.pak
baseq2/pak1.pak
baseq2/pak2.pak
```

and add [OpenAL Soft](https://github.com/kcat/openal-soft) for
Windows builds:

```
OpenAL32.dll
OpenAL64.dll // rename from OpenAL32.dll 64-bit version
```

Additionally Copy file `src/client/ui/q2pro.menu` to `baseq2` folder.

# TODO

- fix transparency problems in game menu background
- expand q2pro.menu with additional items
- add option to switch audio parameters to be like in r1q2's OpenAL
  (I have a ready commit for that too, but I'll add it later on)
- replace links to https://github.com/skullernet/q2pro as they
  are dead
- check q2repro for useful changes (like in manuals)

