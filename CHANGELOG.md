# Changelog

All notable changes to this fork are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- The 64-bit Windows executables are now `q2pro.exe` and `q2proded.exe`, and the
  32-bit ones `q2pro32.exe` and `q2proded32.exe`. Before, the 64-bit build had the
  `64` suffix. Update shortcuts and server scripts that start `q2pro64.exe`.

## [1.0.1] - 2026-11-06

### Added

- A bloom toggle in the effects menu. `gl_bloom` is now saved in the
  configuration file.

### Fixed

- Highlights no longer disappear when `cl_itemhighlight_glow` is above 0 and
  `gl_bloom` is off.

## [1.0.0] - 2026-11-06

First release of the rebuilt fork, based on upstream Q2PRO at commit 601a8df.

### Added

- Versioned release packages for Linux and Windows, built by CI.
- `qal-hard-linked` build option to link OpenAL directly. The Windows packages use
  it, so the OpenAL sound engine (`s_enable 2`) works without installing OpenAL.
- `al_reverb` for reverb on positional sounds, and an HRTF option in the sound menu.
- `config-file` build option to name the saved configuration file. Release
  packages use `q2config.cfg`; the default stays `config.cfg`.
- `ui_hide_bots` hides WallFly and q2stats bots in the server browser.
- `crosshair_paused` hides the crosshair while the game is paused.
- Menus scroll with the keyboard and mouse when they do not fit on the screen,
  and more common settings are reachable from the menus.
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

[Unreleased]: https://github.com/MashedD/q2pro/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/MashedD/q2pro/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/MashedD/q2pro/releases/tag/v1.0.0
