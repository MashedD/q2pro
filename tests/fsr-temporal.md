# Vulkan FSR temporal checks

Run the GPU-free contracts from the repository root:

```sh
sh tests/vk_presentation.sh build-lin64
```

They exercise the actual backend's presentation configurations (20 plus four
MSAA variants), C/GLSL push layout, projection jitter and reduced viewport
scaling, timing-window trimming, the 5% acceptance boundary, stale submission
rejection, selected-quality-only fallback, entity history invalidation, and
barrier batching including GENERAL-to-GENERAL dependencies and overflow.

Compile and validate every motion vertex/fragment shader with `glslc` and
`spirv-val`. The shared push layout is a Meson dependency of generated shader
headers, including cross builds.

## Runtime verification, 2026-09-07

Linux / RADV / Radeon RX 6500M, windowed, q2dm1, mailbox presentation, bloom
enabled, frame generation disabled. Settings, logs and screenshots were kept
under `/tmp/q2-fsr-verify.8B5amL`, separate from the user's configuration.

- 1280x720: native automatic baseline, Quality trial and automatic rejection;
  forced auto/full/fast motion modes; viewsize 80 and 100; manual pause/resume;
  ray tracing off and on. All runs exited successfully without Vulkan errors.
- 2560x1440: all four quality profiles, camera rotation, weapon/particle
  effects, numeric mip-bias override, and sharpening. Full-frame screenshots
  were produced without cropped scene attachments.
- Updated repository video menu loaded from the isolated directory and
  displayed the new texture-bias option. Two menu captures differed only in
  the blinking field cursor. Two manual-pause captures had identical 3D scene
  pixels; differences were confined to animated HUD icons.
- Linux, Win32 and Win64 builds succeeded. Windows builds retained the main
  project's `qal-hard-linked=true`; Windows executables were not run.
- Shader validation and all GPU-free contracts passed. Linux full builds
  still emit existing FidelityFX SDK/generated-blob warnings.

In the 720p raster scene, representative motion-pass GPU timings were about
85 microseconds for auto versus 179–184 microseconds for full. This is a
motion-pass comparison, not an overall FPS improvement. The final automatic
trial measured native at 928 microseconds and Quality FSR at 3373 microseconds,
and correctly returned to native without changing the selected quality.
FSR dispatch remains the dominant cost on this lightweight scene.

## Remaining validation limits

These are smoke tests, not a long-duration image-quality benchmark. No Vulkan
validation layer was available for synchronization validation. Windows runtime,
frame-generation stability, device-loss injection, and every transparency/MD5
asset combination remain untested. Test CPU-only timing fallback, other GPU
vendors, and long moving-camera demos before making broader performance claims.

For manual comparisons use `r_fsr_auto 0`, the same quality/resolution and demo,
then compare `r_fsr_motion auto`, `full`, and `fast` with `vk_perf_stats 1`.
Return to `r_fsr_auto 1` for selected-quality-versus-native automatic selection.
Archived `r_fsr_motion fast` settings remain fast; set it to `auto` explicitly
to use the new hybrid path in an existing configuration.
