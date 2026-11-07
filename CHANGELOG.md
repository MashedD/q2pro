# Changelog

All notable changes to this fork are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.1.0] - 2026-11-07

### Added

- An optional Vulkan renderer, selected with `vid_ref vk` or the new
  "renderer" entry in the video menu. It draws everything the OpenGL renderer
  does and reads the same settings, adds per-pixel lighting, bloom, MSAA,
  stencil shadows and frames in flight, and can pick the GPU (`vk_device`).
  OpenGL stays the default. The release packages include it for Linux and
  Windows. See "Vulkan Renderer" in the client manual.

## [1.0.1] - 2026-11-07

### Added

- The Linux package plays Ogg Vorbis music and Ogg Theora cinematics, like the
  Windows packages. Before, that needed a build from source.

## [1.0.0] - 2026-11-06

First release of the rebuilt fork, based on upstream Q2PRO at commit 601a8df.

### Added

- Versioned release packages for Linux and Windows, built by CI. The Linux
  package runs on any distribution with glibc 2.35 or newer, such as Ubuntu
  22.04, Debian 12, Fedora, Bazzite, Arch and SteamOS.
- Client packages include `baseq2/q2pro.pkz` with default key bindings and
  settings: WASD movement, jump on the right mouse button, crouch on Ctrl,
  weapons on the mouse wheel, running by default and a 120 fps limit. F-keys
  open Q2PRO's menus, and Tab and F1 are swapped. Delete the file to use the
  game's original defaults.
- `curl-dlopen` build option to load libcurl at runtime, so a binary starts on
  systems that name it differently. Without libcurl only HTTP downloads are
  unavailable. The Linux package uses it.
- `qal-hard-linked` build option to link OpenAL directly. The Windows packages use
  it, so the OpenAL sound engine (`s_enable 2`) works without installing OpenAL.
- `al_reverb` for reverb on positional sounds, and an HRTF option in the sound menu.
- `config-file` build option to name the saved configuration file. Release
  packages use `q2config.cfg`; the default stays `config.cfg`.
- `ui_hide_bots` hides WallFly and q2stats bots in the server browser.
- `crosshair_paused` hides the crosshair while the game is paused.
- Menus scroll with the keyboard and mouse when they do not fit on the screen,
  and more common settings are reachable from the menus, including a bloom
  toggle. `gl_bloom` is saved in the configuration file.
- Menu options show a short description at the bottom of the screen.
- `m_r1q2_mouse`, `cl_movement_feel_mode` and `cl_step_smoothing_mode` for
  R1Q2-style mouse handling and movement feel.
- Missing textures are drawn as a checkerboard so that they are easy to spot.
- `r_lava_glowmaps` controls glowmaps on lava, including lava without a lightmap.
- `gl_intensity_2D` brightens the HUD, menus and console in OpenGL.
- `cl_itemhighlight`, `cl_weaponhighlight`, `cl_ammohighlight`,
  `cl_playerhighlight` and `cl_itemhighlight_glow` for optional color highlights.
- `cl_itemtint`, `cl_weapontint`, `cl_ammotint` and `cl_playertint` recolor model
  skins in the OpenGL shader backend.
- `cl_projectiletrail_color` colors rocket and grenade trails.
- `cl_disable_self_blood` hides blood from hits on your own player.
- Windows packages include `THIRD-PARTY-LICENSES.txt` with the licenses of the
  libraries built into the executables.
- Windows packages name the 64-bit executables `q2pro.exe` and `q2proded.exe`,
  and the 32-bit ones `q2pro32.exe` and `q2proded32.exe`. Upstream names the
  64-bit build `q2pro64.exe`.

All new options are off or keep upstream's behaviour by default.

### Fixed

- `scr_scale` no longer changes the crosshair size.
- Text lines leave room for font shadows.
- `exec` has its own loop limit, so a config that chains several files no longer
  fails with "Infinite loop avoided" when nothing is looping.
- Demos that begin with a frame based on one that was never recorded now play.
- A Windows assertion when a game such as OpenTDM sets inline models while a map
  is still loading.
- Broken textures on OpenGL drivers that mishandle direct and multi-bind texture
  calls.
- A missing `q2pro.menu` is reported instead of failing silently.
- A game module crash on maps where an actor's target does not exist.
- A game library without the executable permission is reported with the
  `chmod` command that fixes it, instead of only "Permission denied".

[Unreleased]: https://github.com/MashedD/q2pro/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/MashedD/q2pro/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/MashedD/q2pro/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/MashedD/q2pro/releases/tag/v1.0.0
