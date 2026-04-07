Prerequisities
--------------

Q2PRO can be built on Linux, BSD and similar platfroms using a recent version
of GCC or Clang.

Q2PRO client requires either SDL2 or OpenAL for sound output. For video output,
native X11 and Wayland backends are available, as well as generic SDL2 backend.

Note that SDL2 is optional if using native X11 and Wayland backends and OpenAL,
which is preferred configuration.

Both client and dedicated server require zlib support for full compatibility at
network protocol level. The rest of dependencies are optional.

For JPEG support libjpeg-turbo is required, plain libjpeg will not work. Most
Linux distributions already provide libjpeg-turbo in place of libjpeg.

For playing back music and cinematics FFmpeg libraries are required.

OpenAL sound backend requires OpenAL Soft development headers for compilation.
At runtime, OpenAL library from any vendor can be used (but OpenAL Soft is
strongly recommended).

To install the *full* set of dependencies for building Q2PRO on Debian or
Ubuntu use the following command:

```bash
apt-get install meson gcc libc6-dev libsdl2-dev libopenal-dev \
    libpng-dev libjpeg-dev zlib1g-dev mesa-common-dev \
    libcurl4-gnutls-dev libx11-dev libxi-dev \
    libwayland-dev wayland-protocols libdecor-0-dev \
    libavcodec-dev libavformat-dev libavutil-dev \
    libswresample-dev libswscale-dev
```

If you intend to build just dedicated server, smaller set of dependencies can
be installed:

```bash
apt-get install meson gcc libc6-dev zlib1g-dev
```

Users of other distributions should look for equivalent development packages
and install them.

Building
--------

Q2PRO uses Meson build system for its build process.

Setup build directory (arbitrary name can be used instead of `builddir`):

```bash
meson setup builddir
```

Review and configure options:

```bash
meson configure builddir
```

Q2PRO specific options are listed in `Project options` section. They are
defined in `meson_options.txt` file.

E.g. to install to different prefix:

```bash
meson configure -Dprefix=/usr builddir
```

Finally, invoke build command:

```bash
meson compile -C builddir
```

To enable verbose output during the build, use `meson compile -C builddir -v`.

Installation
------------

You need to have either full version of Quake 2 unpacked somewhere, or a demo.
Both should be patched to 3.20 point release.

Run `sudo ninja -C builddir install` to install Q2PRO system-wide into
configured prefix (`/usr/local` by default).

Copy `baseq2/pak*.pak` files and `baseq2/players` directory from unpacked
Quake 2 data into `/usr/local/share/q2pro/baseq2` to complete the
installation.

Alternatively, configure with `-Dsystem-wide=false` to build a ‘portable’
version that expects to be launched from the root of Quake 2 data tree (this
is default when building for Windows).

On Windows, Q2PRO automatically sets current directory to the directory Q2PRO
executable is in. On other platforms current directory must be set before
launching Q2PRO executable if portable version is built.

Music support
-------------

Q2PRO supports playback of background music ripped off original CD in Ogg
Vorbis format. Music files should be placed in `music` subdirectory of the game
directory in format `music/trackNN.ogg`, where `NN` corresponds to CD track
number. `NN` should be typically in range 02-11 (track 01 is data track on
original CD and should never be used). GOG naming scheme which has tracks 02-21
(with extra tracks for `rogue` and `xatrix` addons) is also supported.

Depending on FFmpeg configuration, music in several other formats can be
transparently supported: FLAC, Opus, MP3 and WAV.

MinGW-w64
---------

MinGW-w64 cross-compiler is available in recent versions of all major Linux
distributions.

Library dependencies that Q2PRO uses have been prepared as Meson subprojects
and will be automatically downloaded and built by Meson.

To install MinGW-w64 on Debian or Ubuntu, use the following command:

```bash
apt-get install mingw-w64
```

It is recommended to also install nasm, which is needed to build libjpeg-turbo
with SIMD support:

```bash
apt-get install nasm
```

Meson needs correct cross build definition file for compilation. Example
cross-files can be found in `.ci` subdirectory (available in git
repository, but not source tarball). Note that these cross-files are specific
to CI scripts and shouldn't be used directly (you'll need, at least, to
customize default `pkg-config` search path). Refer to Meson documentation for
more info.

Setup build directory:

```bash
meson setup --cross-file x86_64-w64-mingw32.txt -Dwrap_mode=forcefallback builddir
```

Build:

```bash
meson compile -C builddir
```

Visual Studio
-------------

It is possible to build Q2PRO on Windows using Visual Studio 2022 and Meson.

Install Visual Studio and Meson using official installers.

Optionally, download and install nasm executable. The easiest way to add it
into PATH is to put it into `Program Files/Meson`.

The build needs to be launched from appropriate Visual Studio command line
shell, e.g. `x64 Native Tools Command Prompt`.

Change to Q2PRO source directory, then setup build directory:

```bash
meson setup -Dwrap_mode=forcefallback builddir
```

Build:

```bash
meson compile -C builddir
```

MashedD's way
-------------

### Prerequisites

Dependencies might be missing and some are probably excessive.
I didn't optimize this as it's time/cost not effective for me.
Best might be to use Docker for the job.

```bash
# Tested on CachyOS
sudo pacman -S gcc meson ninja pkgconf libcurl-gnutls

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

### Compilation

Review scripts before executing them.

```bash
./build-lin64.sh
./build-win32.sh
./build-win64.sh
```

### To play

Copy files from build folder to:

```
baseq2/gamex*.*
q2pro*
```

along with files/folders from non-remastered Quake 2 (optionally baseq2/music):

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

Additionally Copy file `assets/baseq2/q2pro.menu` to `baseq2` folder.

