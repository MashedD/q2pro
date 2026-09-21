#!/bin/sh
set -eu

# Run from any directory, using an already configured Linux build.
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
build_arg=${1:-build-lin64}
case $build_arg in
    /*) build_dir=$build_arg ;;
    *) build_dir=$repo_dir/$build_arg ;;
esac
test -f "$build_dir/config.h" || {
    echo "missing configured build: $build_dir/config.h" >&2
    exit 2
}
test -f "$repo_dir/src/refresh/vk_fsr3.cpp" || exit 2
if command -v rg >/dev/null 2>&1; then
    search_command='rg -q'
    fixed_search_command='rg -Fq'
else
    search_command='grep -q'
    fixed_search_command='grep -Fq'
fi
if $search_command 'FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE' \
       "$repo_dir/src/refresh/vk_fsr3.cpp"; then
    echo "unsupported FSR3 auto-exposure define is present" >&2
    exit 2
fi

# Keep the contract harness coupled to the real control-flow guards. These
# checks are intentionally read-only: the renderer remains outside this test's
# ownership, while regressions in reset/pause/frame-id ordering fail early.
require_contract() {
    pattern=$1
    file=$2
    if ! $fixed_search_command "$pattern" "$file"; then
        echo "missing FSR3 contract: $pattern" >&2
        exit 2
    fi
}
require_contract 'bool frame_reset = vk.fsr_reset' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'if (!frame_reset && frame_generation_prepared &&' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.fsr_pause_reuse = true' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.frame_fsr = false' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.fsr_timing_discontinuity = vk.fsr_reset' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_session_frame_generation_disabled = true' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'context->frame_generation_failed ||' \
    "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract '!context->frame_started' \
    "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'context->current_frame_id = context->next_frame_id++' \
    "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'description.frameID = context->current_frame_id' \
    "$repo_dir/src/refresh/vk_fsr3.cpp"

# A renderer frame owns one FSR frame ID.  Keep the explicit lifecycle at the
# call site: BeginFrame must open the SDK frame before jitter is requested, and
# jitter failure must sanitize the values and force a reset.
begin_line=$(grep -n 'Q2_FSR3_BeginFrame(vk.fsr3)' "$repo_dir/src/refresh/vk_backend.c" | head -n 1 | cut -d: -f1)
jitter_line=$(grep -n 'Q2_FSR3_GetJitter(vk.fsr3' "$repo_dir/src/refresh/vk_backend.c" | head -n 1 | cut -d: -f1)
test -n "$begin_line" -a -n "$jitter_line" -a "$begin_line" -lt "$jitter_line" || {
    echo "FSR3 contract: BeginFrame must precede GetJitter" >&2
    exit 2
}
require_contract 'if (!vk.fsr_jitter_ready)' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.fsr_jitter[0] = 0.0f' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.fsr_jitter[1] = 0.0f' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.fsr_reset = true' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'q2_fsr3_sanitize_inputs' \
    "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'q2_fsr3_sanitize_sharpness' \
    "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract '!std::isfinite(*x) || !std::isfinite(*y)' \
    "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'context->force_reset = inputs.reset' \
    "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'Q2_FSR3_GetCurrentFrameId' "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'Q2_FSR3_GetJitterPhase' "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'jitter_phase=%u jitter_phases=%u' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'reset_reason=%s' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.fsr_frame_reset_reason = vk.fsr_reset ? vk.fsr_reset_reason' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_fsr_invalidate_history_reason(VK_FSR_RESET_CAMERA_CUT)' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_fsr_invalidate_history_reason(VK_FSR_RESET_PAUSE_RESUME)' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_fsr_dynamic_update' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'applied=%s' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.fsr_dynamic_transaction_id++' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'r_fsr_dynamic' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_destroy_internal_render_targets_nowait' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_destroy_texture_resource_nowait' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_destroy_fsr_resources_nowait' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_create_fsr_motion_framebuffer' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_destroy_display_framebuffers_nowait' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'VK_FSR_RESET_RENDER_SCALE' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_rebuild_internal_render_targets' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_destroy_internal_targets_bundle_nowait' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.frame_active || vk.render_pass_active' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Vulkan FSR motion requires a sampled depth view' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.fsr_dynamic.cooldown_remaining = cooldown_frames' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_create_fsr_motion_pipelines' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'fsr_dynamic_fixed_restore_pending' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'if (error != FFX_OK)' "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'context->frame_generation_failed = true' "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'if (!dispatched)' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_disable_frame_generation("FSR3 upscaler dispatch failed")' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_log_fence_wait_failure' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'last_submit_result' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_log_fsr_frame_generation_failure' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'missing Vulkan device function %s' "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'profile_supported' "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'max_frame_time_ms' "$repo_dir/src/refresh/vk_fsr3.cpp"

# Pause reuse composites a retained FSR texture. It must not route the paused
# frame through the normal presentation-image copy path.
require_contract 'vk_composite_presentation_texture(&vk.fsr_output_texture)' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Do not copy it during pause entry' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.fsr_pause_reuse = true' "$repo_dir/src/refresh/vk_backend.c"
require_contract '!vk.fsr_pause_reuse' "$repo_dir/src/refresh/vk_backend.c"
if $fixed_search_command 'vk_prepare_direct_pause_cache' "$repo_dir/src/refresh/vk_backend.c"; then
    echo "FSR3 contract: pause must not copy the presented image directly" >&2
    exit 2
fi

cc=${CC:-cc}
if command -v pkg-config >/dev/null 2>&1; then
    sdl_cflags=$(pkg-config --cflags sdl2 2>/dev/null || true)
else
    sdl_cflags=
fi
set -- -std=c11 -O1 -g -fms-extensions -ffunction-sections -fdata-sections \
    -DHAVE_CONFIG_H -D_GNU_SOURCE -DUSE_CLIENT=1 -DUSE_REF=1 \
    -I"$build_dir" -I"$repo_dir/inc" -I"$repo_dir"
# SDL headers are optional for this harness on builds that expose them through
# the configured include path; use pkg-config flags when available.
if test -n "$sdl_cflags"; then
    # shellcheck disable=SC2086
    set -- "$@" $sdl_cflags
fi
set -- "$@" "$repo_dir/tests/vk_presentation.c" -Wl,--gc-sections -lm \
    -o "$build_dir/vk_presentation_test"
"$cc" "$@"
"$build_dir/vk_presentation_test"
