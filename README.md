# Q2PRO

[![Build](https://github.com/MashedD/q2pro/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/MashedD/q2pro/actions/workflows/build.yml)
[![License: GPL v2](https://img.shields.io/badge/license-GPL--2.0-blue.svg)](LICENSE)

An enhanced Quake 2 client and server for Windows and Linux, built for smooth
play, low input lag and modern systems while staying compatible with existing
servers, mods and demos.

This is a fork of [Q2PRO](https://github.com/q2pro/q2pro) by Andrey Nazarov.

## Features

**Playing**

- Separate rendering, physics and packet rates
- Syncing to the GPU for reduced input lag with vsync on
- Enhanced console with persistent command history and autocompletion
- Seeking in demos, and recording from demos

**Graphics and sound**

- Unified OpenGL renderer supporting a wide range of OpenGL versions
- JPEG and PNG textures and screenshots
- MD3 and MD5 (re-release) models, and compatibility with re-release assets
- Ogg Vorbis music and Ogg Theora cinematics
- Multichannel sound using OpenAL, and stereo WAV files

**Servers and networking**

- Fast and secure HTTP downloads, and ZIP packfiles (`.pkz`)
- Server-side multiview demos and live game broadcasting
- Network protocol extensions for larger maps
- No frame overflows, even for legacy clients
- No crashes if game data is corrupted

## Getting started

You need the game data from a copy of Quake 2, such as `baseq2/pak0.pak`.

1. Download the package for your system from the
   [Releases](https://github.com/MashedD/q2pro/releases) page.
2. Extract it into your Quake 2 directory.
3. Run `q2pro`, or `q2pro64` on 64-bit Windows.

To build from source, see [BUILDING.md](BUILDING.md).

## Documentation

- [Client manual](doc/client.asciidoc): settings and commands
- [Server manual](doc/server.asciidoc): running a server
- [Building](BUILDING.md): dependencies and compilation
- [Changelog](CHANGELOG.md): what changed in each version

## Project status

The fork is being rebuilt on top of upstream, one reviewed topic at a time, so that
its history explains every change. The planned order is general fixes and
improvements, then a Vulkan renderer, then ray tracing.

Those features are not on `master` yet. Releases made before the rebuild, such as
[`release-2026-10-02`](https://github.com/MashedD/q2pro/releases/tag/release-2026-10-02),
include earlier versions of them. The earlier history is preserved in the
[`archive/master-2026`](https://github.com/MashedD/q2pro/tree/archive/master-2026)
branch.

## Contributing

Bug reports and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Credits

- [id Software](https://github.com/id-Software/Quake-2) for Quake 2
- Andrey Nazarov and the [Q2PRO contributors](https://github.com/q2pro/q2pro/graphs/contributors)

## License

Q2PRO is free software, licensed under the
[GNU General Public License, version 2](LICENSE) or later.
