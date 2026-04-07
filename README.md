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

# Documentation

- [Client](doc/client.asciidoc)
- [Server](doc/server.asciidoc)
- [Building](BUILDING.md)

# Fixes in this fork

- fixed playback of some demos
- fixed `ch_scale` so that `scr_scale` doesn't affect crosshair size
- fixed compilation warnings
- fixed problem with displaying excessive items in address book after
  quickly switching from Server Browser
- fixed OpenTDM crash on Windows when loading q2dm1 twice

# Changes

- building: added win32/win64 cross compilation settings
- building: win64 build uses now `OpenAL64.dll` instead of `OpenAL32.dll`,
  so now win32 and win64 binaries can live next to each other (optional)
- building: win build has OpenAL library hard linked now
- with `crosshair_paused 1` don't draw crosshair on top of "Paused" image
- lin64 build now looks for game files in current folder
  instead of `$HOME/.q2pro` as it was not XDG Base Directory Specification
  compliant
- change so that config file name on Linux is same as on Windows (q2config.cfg
  instead of config.cfg)
- textures: when missing, replaced red dot on black screen to pink/black
  checkerboard similar to one seen in original Quake 2
- sound: added `al_hrtf` option to menu
- sound: added `al_reverb` option to menu. 1 - softer, 2 - stronger
- effects: added `cl_itemhighlight`, `cl_playerhighlight` and `cl_itemhighlight_glow`
- rendering: added `r_lava_glowmaps` to toggle lava glowmaps independently
- added `chase quadkiller` command (when playing MVD demos)

# TODO

- fix transparency problems in game menu background
- check q2repro for useful changes (like in manuals)
- consider adding Vulkan renderer
