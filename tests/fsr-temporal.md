# Vulkan FSR temporal checks

Run the GPU-free contracts from the repository root:

```sh
sh tests/vk_presentation.sh build-lin64
```

They exercise the actual backend's presentation configurations (20 plus four
MSAA variants), C/GLSL push layout, projection jitter and reduced viewport
scaling, timing-window trimming, the 5% acceptance boundary, stale submission
rejection, selected-quality-only fallback, entity history invalidation, and
barrier batching including GENERAL-to-GENERAL dependencies and overflow. The
contract harness also checks explicit BeginFrame/GetJitter ordering, sanitized
inputs and jitter fallback, failure-state reset, frame-ID propagation, and
pause reuse through the retained output texture rather than the normal
presentation-image copy path.

## Acceptance matrix

| Area | Acceptance | Evidence |
| --- | --- | --- |
| FSR3 upscaling | Native, quality/balanced/performance/ultra-performance, motion auto/full/fast, and pause/resume | GPU-free contracts plus 720p/1440p smoke runs |
| Temporal inputs | BeginFrame precedes GetJitter; invalid jitter falls back to zero and sets reset; sharpness/frame time are bounded | `tests/vk_presentation.sh` source contracts |
| Recovery | Upscale/frame-generation failures disable generation, preserve ordinary FSR, and reset temporal history | C harness and source contracts |
| Telemetry | Unique renderer frame IDs, separate SDK temporal IDs, jitter values/phase, reasoned reset fields, per-frame result/pause fields, renderer-completion pacing, simulation-time pacing, and FSR per-pass GPU timings parse deterministically | `tests/fsr_benchmark.py`, `tests/test_fsr_benchmark.py` |
| Pause presentation | Reuses the retained FSR output and leaves the normal presentation-image copy path unused for pause reuse | Source contract and manual pause captures |
| Frame generation | Provider swapchain activation, queue topology, explicit toggle precedence, and pacing telemetry | Source contracts and a 2560x1440 mailbox RX 6500M run with 232 interpolation callbacks followed by accepted presents; display scanout cadence remains unmeasured |

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

Temporal diagnostics now identify the reset cause (`camera_cut`, `resize`,
`pause_resume`, configuration, invalid jitter, or dispatch failure) and expose
the SDK jitter phase in `VK FSR3 frame` records. Optional GPU pixel validation
for non-finite motion vectors and mask coverage is still deliberately deferred:
it requires an opt-in readback/compute pass and must not affect normal
benchmarks.

Set `r_fsr_dynamic 1` to collect GPU-time recommendations with hysteresis,
quantized scale steps, and a cooldown. At a safe frame boundary the backend
builds a candidate internal-target bundle, commits it only after all resources
succeed, and preserves the swapchain/display framebuffers. Failed candidates
are discarded and the previous scale remains active. `VK FSR3 dynamic`
telemetry reports the applied state and transaction counter. The lifecycle is
split into wait-free internal-target destruction and separate FSR
motion-framebuffer creation. Native-scale fallback remains recoverable, and
disabling dynamic mode retries restoration of the fixed profile after a
transient allocation failure. `r_fsr_dynamic_cpu 1` opts into CPU timing only
when GPU timestamps are unavailable.

These are smoke tests, not a long-duration image-quality benchmark. No Vulkan
validation layer was available for synchronization validation. Windows runtime,
device-loss injection, and every transparency/MD5 asset combination remain
untested. Frame-generation swapchain pacing is explicitly unsupported and not
validated; the benchmark records requested/result telemetry but does not claim
generated-frame presentation cadence. Test CPU-only timing fallback, other GPU
vendors, and long moving-camera demos before making broader performance claims.

For manual upscaling comparisons use `r_fsr_auto 0`, the same quality/resolution
and demo, then compare `r_fsr_motion auto`, `full`, and `fast` with
`vk_perf_stats 1`. Explicit `r_fsr_frame_generation 1` now takes precedence over
`r_fsr_auto`; automatic quality benchmarking pauses while interpolation is
enabled. Return to `r_fsr_frame_generation 0` before measuring automatic
selected-quality-versus-native performance.
Archived `r_fsr_motion fast` settings remain fast; set it to `auto` explicitly
to use the new hybrid path in an existing configuration.
