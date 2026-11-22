# Changelog

All notable changes to this fork are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.7.2] - 2026-11-22

### Added

- The crosshair setup menu has "hit markers" and "hit marker time"
  entries for `cl_hit_markers` and `scr_hit_marker_time`.

## [1.7.1] - 2026-11-22

### Fixed

- The bundled baseq2 game sent no hit markers, so they never showed in
  single player and coop. It now sends them for hits on monsters and
  players to clients with `cl_hit_markers`, and can play `g_hit_sound` to
  older clients instead.

## [1.7.0] - 2026-11-21

### Added

- `cl_hit_markers` is sent in the userinfo, so servers can send re-release
  hit markers only to clients that show them. Older Q2PRO and R1Q2 dropped
  the connection on their first hit when a server sent hit markers to
  everyone. The OpenTDM, OpenFFA and 3zb2-zigflag forks use it from their
  next releases, and can play `g_hit_sound` to other clients instead.

## [1.6.0] - 2026-11-21

### Added

- `vk_rt_materials` takes the relief and roughness of world surfaces from
  material maps, `textures/<name>_n.png`, with Vulkan ray tracing. The
  relief shows under dynamic lights, glowing surfaces and highlights, and
  gently under lightmaps. The ray tracing menu has a "material maps" entry.
  No maps are included; `tools/make-pbr-materials.py` makes them from a
  texture pack of your own.
- `ui_cvar_names` starts each menu setting's description with the name of
  the variable it sets, in every menu, including your own menu scripts. The
  screen setup menu has a "show variable names" entry.

## [1.5.1] - 2026-11-21

### Fixed

- With SDL3 under sdl2-compat on Wayland, the view could turn by about 15
  degrees in a single frame, usually in the first seconds of play, so a
  shot went into a wall beside the player. SDL's accumulated mouse state
  sometimes held a jump that no motion event carried; the SDL driver now
  adds up the motion of the events instead.
- With `ch_follow_shot`, the crosshair vanished or stayed in the center
  during demo playback. It now follows the recorded aim.

## [1.5.0] - 2026-11-21

### Added

- `cl_railtrail_type 3` draws the railgun shot as an energy bolt: a bright
  core in a coil of glowing rings, with lightning arcs and sparks, colored
  by `cl_railspiral_color`. `cl_railring_radius` sets the size of the rings
  and `cl_railbolt_light` how brightly the bolt lights its surroundings.
  The railgun trail menu has entries for all three.
- `ch_follow_shot` moves the crosshair to where your shots will land.
  Without aim correction on the server, Quake 2 weapons fire from beside
  and below the eye, so the shot lands off the centered crosshair by an
  amount that depends on the distance; a fixed `ch_x` and `ch_y` is only
  right at one distance. It is off by default, and the crosshair menu has
  a "follow the shot" entry.
- The crosshair menu has fields for its horizontal and vertical offset
  (`ch_x`, `ch_y`).
- The OpenGL renderer can render on the GPU chosen in the video menu, on
  Linux with Mesa drivers. The menu entry is now called "GPU" and works for
  both renderers, and `vk_device` also accepts PCI vendor:device IDs such
  as `1002:743f`.

### Changed

- The crosshair settings (`ch_scale`, `ch_x`, `ch_y`, `ch_health` and the
  colors) are saved in the configuration file, so what the menu sets is no
  longer lost when the game restarts.

### Fixed

- Under Vulkan, dynamic lights from rockets, blasters and other effects
  spread over walls as they do under OpenGL. With ray tracing they no
  longer leave uneven, flatly lit rectangles on walls for a moment.
- With Vulkan ray tracing, looking up from under water no longer puts
  caustics on surfaces above the water, and the water's blue-green fog
  only covers the water in between, so what is above the surface stays
  visible. Caustics now light the textures instead of tinting them, and
  the underwater and air effects fade into each other when you dive in or
  come out.
- The Windows build works with the current Microsoft compiler.

## [1.4.2] - 2026-11-17

### Added

- More optional weather sounds from `sound/weather/`: footsteps in deep
  snow, in puddles and on wet ground, loops for light and heavy rain, rain
  heard from under water, a storm wind, and splashes, drops and steam
  hisses where rain and snow land close by.

## [1.4.1] - 2026-11-16

### Added

- The effects menu has a "weather setup" page for snow, rain and
  thunderstorms.

## [1.4.0] - 2026-11-16

### Added

- `cl_snow` makes it snow on any map, only where the sky is above, with a
  wind and gusts made from noise, or sounds of your own from
  `sound/weather/` (`cl_snow_volume`), and snow that settles into drifts
  on floors and ledges under open sky over a few minutes (`cl_snow_cover`,
  `cl_snow_cover_time`). Each map has a prevailing wind that shapes the
  drifts, dynamic lights light the snow (with shadows under Vulkan ray
  tracing), and it doesn't settle near lava. Explosions blow craters into
  it, which fill in again (`cl_snow_craters`).
- `cl_rain` makes it rain on any map in the same way, as streaks that
  splash where they land, with a rain sound made from noise or your own
  (`cl_rain_volume`), and puddles that fill over two minutes and ripple
  (`cl_rain_puddles`).
- `cl_thunder` turns rain into a thunderstorm, with lightning flashes
  (`cl_thunder_flash`, 0 for none) and thunder that follows after a delay
  that tells how far away it struck.
- `vk_perf_stats 2` prints each frame that took much longer than usual,
  with where the time went (outside the renderer, presenting, waiting for
  the GPU, recording), to find what causes stutter.

### Fixed

- After leaving a map with Vulkan ray-traced reflections on, the first
  frame without a 3D view could keep showing the last view of the map
  instead of the console or menu, and with bloom on that frame started a
  render pass with a target in the wrong layout (a Vulkan validation
  error).

## [1.3.0] - 2026-11-13

### Added

- `cl_guntint` tints the weapon you hold in first person on its own;
  `cl_weapontint` now only covers weapons on the map and in other players'
  hands, and the effects menu has a "tint your weapon" entry.

### Changed

- `gl_bloom_lamps 2` makes lamps without a glow map glow as strongly as in
  the archived builds, in both renderers and with ray tracing; 1 keeps the
  softer glow and is now the default, so lamps glow whenever bloom is on.
  The release packages no longer set it, and the effects and ray tracing
  menus have a "lamp bloom" entry. `gl_bloom_lamps 0` now also turns lamp
  glow off with ray tracing.

### Fixed

- With the demo's data, the release packages broke starting a new game and
  the attract mode: their `q2pro.pkz` replaced the game's `default.cfg` with
  one written for the full game, whose `newgame` and `dedicated_start` load
  `base1`. The package defaults now live in `q2pro-defaults.cfg`, which the
  engine runs right after the game's own `default.cfg`, so the full game and
  the demo keep their aliases.
- Vulkan screenshots read a swapchain image that had already been handed to
  the display, which the Vulkan specification forbids and which can give a
  wrong or stale picture on some drivers. They now capture the next frame
  before it is presented.

## [1.2.2] - 2026-11-09

### Fixed

- A server slot leaked every time the game, such as q2admin, rejected the
  dummy MVD client, until new players got "Server is full." even though the
  server list showed free slots. The dummy's userinfo now includes `rate`, which q2admin requires,
  so MVD recordings keep the scoreboard.

## [1.2.1] - 2026-11-09

### Added

- `cl_tint_<group>` sets a custom color for one group of tinted and
  highlighted models, such as `cl_tint_rockets` or `cl_tint_health`.

### Changed

- `cl_itemtint`, `cl_weapontint` and `cl_ammotint` now recolor skins like the
  q2dl-skins-tinted-items-ammo-weapons skin pack, so the pack is no longer
  needed, and highlight shells use the same colors in both renderers.

### Fixed

- With Vulkan ray tracing, item highlight glows and other glows drawn only
  into bloom were missing whenever ray-traced reflections were active.
- Vulkan bloom showed a grid pattern and hard edges, because its blur
  skipped texels; it now uses the same blur as the OpenGL renderer.
- Vulkan shell glows, such as item highlights, formed a bright ring around
  the model instead of an even glow.

## [1.2.0] - 2026-11-09

### Added

- Optional ray tracing for the Vulkan renderer (`vk_raytracing`, off by
  default): shadows from dynamic lights, ambient occlusion, colored light
  from glowing surfaces, screen-space reflections and highlights, liquid and
  atmosphere effects, and small effects for impacts, splashes, teleports and
  explosions, each with its own `vk_rt_*` strength setting and a "Vulkan ray
  tracing" menu. Needs a GPU and driver with Vulkan ray queries. The release
  packages include `materials/rt_reflections.mat` in `q2pro.pkz`.
- `gl_bloom_lamps` lets glowing world surfaces without a glow map feed bloom,
  in both renderers, so plain lamps glow too. Off by default; the release
  packages turn it on.
- `cl_railcore_glow` adds a soft glow and a paler center to the rail core,
  in both renderers. Defaults to 0.
- `vk_present_mode` picks the Vulkan present mode (FIFO, mailbox or
  immediate) instead of deriving it from `gl_swapinterval`. The default,
  automatic, works as before.

### Changed

- With the Vulkan renderer and `cl_async 2`, a frame starts only once a
  swapchain image is free, instead of reading input first and then waiting
  in the driver for an image.
- Video setup has a "glowmap intensity" slider (`gl_glowmap_intensity`), and
  the effects menu lists the highlight toggles after the skin tint toggles.

### Fixed

- Switching renderers no longer prints "Cmd_AddMacro: gl_viewcluster already
  defined" and "gl_viewleaf already defined", and those macros now answer
  from the renderer that is running.
- Vulkan API misuse found with the Khronos validation layer: model shadows
  read height fog from the wrong place, a present semaphore could be reused
  while the presentation engine still waited on it, which can cause stutter
  or hangs on some drivers, and screenshots mapped their buffer with size 0,
  which some drivers reject.
- Vulkan bloom used two blend states without enabling the `independentBlend`
  feature, and started frames without bloom (menus, loading) with its second
  color target in the wrong layout. GPUs without the feature now get a
  warning and no bloom instead of undefined results.
- When the Vulkan swapchain can't be created, for example while the window
  is minimized, the renderer skips frames and retries every second instead
  of using a missing swapchain until the next video mode change.

## [1.1.1] - 2026-11-07

### Changed

- The default settings in the release packages (`q2pro.pkz`) turn vertical
  sync off, because it adds noticeable input lag. It can be turned back on in
  the video menu or with `gl_swapinterval 1`.

### Fixed

- The game module no longer crashes when the `give` cheat command hands out an
  item that the deathmatch flags forbid, such as armor with "no armor" set.
- Skin tints (`cl_itemtint`, `cl_weapontint`, `cl_ammotint`, `cl_playertint`)
  work in the Vulkan renderer.
- Vulkan particles were about twice as large as in the OpenGL renderer.
- `r_maxfps` limits the frame rate to the value set. Rates that don't divide
  1000 evenly used to be rounded up, so 288 gave 333 fps and 60 gave 62.

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

[Unreleased]: https://github.com/MashedD/q2pro/compare/v1.7.2...HEAD
[1.7.2]: https://github.com/MashedD/q2pro/compare/v1.7.1...v1.7.2
[1.7.1]: https://github.com/MashedD/q2pro/compare/v1.7.0...v1.7.1
[1.7.0]: https://github.com/MashedD/q2pro/compare/v1.6.0...v1.7.0
[1.6.0]: https://github.com/MashedD/q2pro/compare/v1.5.1...v1.6.0
[1.5.1]: https://github.com/MashedD/q2pro/compare/v1.5.0...v1.5.1
[1.5.0]: https://github.com/MashedD/q2pro/compare/v1.4.2...v1.5.0
[1.4.2]: https://github.com/MashedD/q2pro/compare/v1.4.1...v1.4.2
[1.4.1]: https://github.com/MashedD/q2pro/compare/v1.4.0...v1.4.1
[1.4.0]: https://github.com/MashedD/q2pro/compare/v1.3.0...v1.4.0
[1.3.0]: https://github.com/MashedD/q2pro/compare/v1.2.2...v1.3.0
[1.2.2]: https://github.com/MashedD/q2pro/compare/v1.2.1...v1.2.2
[1.2.1]: https://github.com/MashedD/q2pro/compare/v1.2.0...v1.2.1
[1.2.0]: https://github.com/MashedD/q2pro/compare/v1.1.1...v1.2.0
[1.1.1]: https://github.com/MashedD/q2pro/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/MashedD/q2pro/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/MashedD/q2pro/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/MashedD/q2pro/releases/tag/v1.0.0
