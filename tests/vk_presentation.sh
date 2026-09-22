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
require_contract 'vk_create_fsr_context_with_fallback' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Q2_FSR3_Preflight' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'preflight.missing_function' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'frame-generation preflight failed; attempting FSR upscaling fallback' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'r_fsr_frame_generation_compute_only' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'r_fsr_frame_generation_compute_only", "0", CVAR_ARCHIVE' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_fsr_frame_generation_presentation_adapter_available' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_presentation_adapter_ensure' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_presentation_acquire' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Q2_VK_PresentationAdapterProviderReady' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Q2_VK_PresentationAdapterAcquire' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Q2_VK_PresentationAdapterPresent' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Q2_VK_PresentationAdapterMarkRecreated' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Q2_VK_PresentationAdapterAbortFrame' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_fsr_frame_generation_compute_only_enabled' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_fsr_frame_generation_effective' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_fsr_frame_generation_effective() &&' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'if (vk_fsr_frame_generation_effective())' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'no_framegen_swapchain' "$repo_dir/src/refresh/vk_backend.c"
require_contract '#if defined(_WIN32)' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'frame-generation context creation failed; using FSR upscaling' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_disable_frame_generation_during_setup' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract '*frame_generation_requested = false' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'VK FSR3 framegen:' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'fsr_framegen_requested_total' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'fsr_framegen_prepared_total' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'fsr_framegen_computed_total' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'fsr_framegen_presented_total' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'additional_presented_total' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_reset_fsr_frame_generation_telemetry' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_fsr_frame_generation_user_requested' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_finish_fsr_frame_generation_telemetry(' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'submission_completed' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'presentation_failed' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'additional_present_interval_us' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'end_command_buffer_failed' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'reset_fences_failed' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'queue_submit_failed' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk_mark_fsr_frame_generation_pause' \
    "$repo_dir/src/refresh/vk_backend.c"
if $fixed_search_command 'vk.fsr_framegen_requested_total--' \
       "$repo_dir/src/refresh/vk_backend.c"; then
    echo "FSR3 contract: pause must preserve frame-generation request totals" >&2
    exit 2
fi
require_contract 'fallback_reason = "pause_reuse"' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.fsr_framegen_additional_presented = false' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'frame_generation_telemetry' "$repo_dir/tests/fsr_benchmark.py"
require_contract 'summarize_frame_generation' "$repo_dir/tests/fsr_benchmark.py"
require_contract 'generated_fps_from_frame_generation' "$repo_dir/tests/fsr_benchmark.py"
require_contract 'additional_present_intervals_us' "$repo_dir/tests/fsr_benchmark.py"
require_contract 'Actively changes internal render resolution' \
    "$repo_dir/assets/baseq2/q2pro.menu"
require_contract 'FSR3 compute-only frame generation' \
    "$repo_dir/assets/baseq2/q2pro.menu"
require_contract 'r_fsr_frame_generation_compute_only' \
    "$repo_dir/assets/baseq2/q2pro.menu"
require_contract 'missing Vulkan device function %s' "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'profile_supported' "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'max_frame_time_ms' "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'full_context_initialized' "$repo_dir/src/refresh/vk_fsr3.cpp"
require_contract 'q2_fsr3_sdk_hardening.py' \
    "$repo_dir/subprojects/packagefiles/fidelityfx-fsr3/meson.build"
require_contract 'fsr3_frame_interpolation_provider_compiled = false' \
    "$repo_dir/subprojects/packagefiles/fidelityfx-fsr3/meson.build"
require_contract 'Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_COMPILED' \
    "$repo_dir/meson.build"
require_contract 'Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_REASON' \
    "$repo_dir/meson.build"
require_contract 'Q2_VK_PRESENTATION_PROVIDER_UNAVAILABLE' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'Q2_VK_PresentationAdapterProviderBuildCompiled' \
    "$repo_dir/src/refresh/vk_presentation_adapter.c"
require_contract 'Q2_VK_PresentationAdapterProviderReady' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'q2_vk_presentation_provider_capabilities_t' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'Q2_VK_PresentationAdapterProviderCapabilities' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'lifecycle_capable' \
    "$repo_dir/src/refresh/vk_presentation_adapter.c"
require_contract 'Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_UNKNOWN' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_SHARED_FAMILY' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'graphics_queue_index' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'present_queue_index' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'provider_synchronization_ready' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'Q2_VK_PRESENTATION_SYNC_BINARY_SEMAPHORE' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'Q2_VK_PRESENTATION_SYNC_TIMELINE_SEMAPHORE' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'q2_vk_presentation_sync_contract_t' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'Q2_VK_PRESENTATION_SYNC_IMAGE_FENCE_ALIASES_FRAME' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'native_sync_facts_known' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'native_sync_facts' \
    "$repo_dir/src/refresh/vk_presentation_adapter.h"
require_contract 'native synchronization facts are unknown' \
    "$repo_dir/src/refresh/vk_presentation_adapter.c"
require_contract 'q2_vk_presentation_provider_sync_ready' \
    "$repo_dir/src/refresh/vk_presentation_adapter.c"
require_contract 'Q2_VK_PRESENTATION_FRAME_INTERPOLATION' \
    "$repo_dir/src/refresh/vk_presentation_adapter.c"
require_contract 'Q2_VK_PresentationAdapterProviderBuildCompiled()' \
    "$repo_dir/src/refresh/vk_presentation_adapter.c"
require_contract 'Q2_VK_PresentationAdapterProviderBuildReason' \
    "$repo_dir/src/refresh/vk_presentation_adapter.c"
require_contract 'Vulkan FSR3 frame-generation provider' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Q2_VK_PresentationAdapterProviderBuildPlatform' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Vulkan FSR3 provider capabilities:' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Vulkan FSR3 provider capability detail:' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Q2_VK_PresentationAdapterProviderCapabilities' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'queue_family_facts_known = true' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'provider_synchronization_ready = false' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'acquire_signal = Q2_VK_PRESENTATION_SYNC_BINARY_SEMAPHORE' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'render_finished_signal =' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'frame_completion = Q2_VK_PRESENTATION_SYNC_FENCE' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'image_reuse = Q2_VK_PRESENTATION_SYNC_FENCE' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'VkSubmitInfo submit_info' \
    "$repo_dir/src/refresh/vk_backend.c"
require_contract 'vk.QueueSubmit' \
    "$repo_dir/src/refresh/vk_backend.c"
if $fixed_search_command 'VkSubmitInfo2' \
       "$repo_dir/src/refresh/vk_backend.c" ||
   $fixed_search_command 'VK_KHR_synchronization2' \
       "$repo_dir/src/refresh/vk_backend.c" ||
   $fixed_search_command 'VK_SEMAPHORE_TYPE_TIMELINE' \
       "$repo_dir/src/refresh/vk_backend.c"; then
    echo "FSR3 contract: unsupported synchronization2/timeline path is present" >&2
    exit 2
fi
require_contract 'graphics_queue_index' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'present_queue_index' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'queues_same_family' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'swapchain_sharing_mode' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'Vulkan queue topology:' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'VK_SHARING_MODE_CONCURRENT' "$repo_dir/src/refresh/vk_backend.c"
require_contract 'VK_SHARING_MODE_EXCLUSIVE' "$repo_dir/src/refresh/vk_backend.c"
sharing_line=$(grep -n 'vk.swapchain_sharing_mode = VK_SHARING_MODE' \
    "$repo_dir/src/refresh/vk_backend.c" | head -n 1 | cut -d: -f1)
swapchain_create_line=$(grep -n 'vk.CreateSwapchainKHR' \
    "$repo_dir/src/refresh/vk_backend.c" | head -n 1 | cut -d: -f1)
test -n "$sharing_line" -a -n "$swapchain_create_line" &&
test "$sharing_line" -lt "$swapchain_create_line" || {
    echo "FSR3 contract: swapchain sharing mode must be recorded before creation" >&2
    exit 2
}
require_contract 'sharedResourceCreated' \
    "$repo_dir/subprojects/packagefiles/fidelityfx-fsr3/tools/q2_fsr3_sdk_hardening.py"
require_contract 'frameInterpolationContextCreated' \
    "$repo_dir/subprojects/packagefiles/fidelityfx-fsr3/tools/q2_fsr3_sdk_hardening.py"
require_contract 'backendContextCreated' \
    "$repo_dir/subprojects/packagefiles/fidelityfx-fsr3/tools/q2_fsr3_sdk_hardening.py"

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
set -- "$@" "$repo_dir/tests/vk_presentation.c" \
    "$repo_dir/src/refresh/vk_presentation_adapter.c" \
    -Wl,--gc-sections -lm \
    -o "$build_dir/vk_presentation_test"
"$cc" "$@"
"$build_dir/vk_presentation_test"

# The presentation adapter is intentionally GPU-free. Compile its state
# machine independently so acquire/present/recreate ordering stays covered
# even on systems without a Vulkan device.
"$cc" -std=c11 -O1 -g -fms-extensions -ffunction-sections -fdata-sections \
    -DHAVE_CONFIG_H -D_GNU_SOURCE -I"$build_dir" -I"$repo_dir" \
    "$repo_dir/src/refresh/vk_presentation_adapter.c" \
    "$repo_dir/tests/vk_presentation_adapter.c" \
    -Wl,--gc-sections -o "$build_dir/vk_presentation_adapter_test"
"$build_dir/vk_presentation_adapter_test"

# Repeat the adapter state-machine test with a compiled provider so the
# positive readiness path is covered independently of the current no-provider
# build configuration.
"$cc" -std=c11 -O1 -g -fms-extensions -ffunction-sections -fdata-sections \
    -D_GNU_SOURCE -DQ2_FSR3_FRAME_INTERPOLATION_PROVIDER_COMPILED=1 \
    -I"$build_dir" -I"$repo_dir" \
    "$repo_dir/src/refresh/vk_presentation_adapter.c" \
    "$repo_dir/tests/vk_presentation_adapter.c" \
    -Wl,--gc-sections -o "$build_dir/vk_presentation_adapter_compiled_test"
"$build_dir/vk_presentation_adapter_compiled_test"

# Exercise the actual C++ preflight implementation with fake Vulkan loaders;
# the backend harness above intentionally stubs this ABI and cannot validate
# function-name resolution or C/C++ struct-return compatibility.
cxx=${CXX:-c++}
"$cxx" -std=c++17 -O1 -g -ffunction-sections -fdata-sections \
    -DFFX_GCC -D_GNU_SOURCE -DHAVE_CONFIG_H \
    -I"$build_dir" -I"$repo_dir" -I"$repo_dir/inc" \
    -I"$repo_dir/subprojects/FidelityFX-SDK-1.1.4/sdk/include" \
    -I"$repo_dir/subprojects/FidelityFX-SDK-1.1.4/sdk/src/shared" \
    -I"$repo_dir/subprojects/FidelityFX-SDK-1.1.4/sdk/src/backends/shared" \
    -I"$repo_dir/subprojects/FidelityFX-SDK-1.1.4/sdk/src/backends/shared/blob_accessors" \
    -I"$repo_dir/subprojects/FidelityFX-SDK-1.1.4/sdk/src/components" \
    -I"$repo_dir/subprojects/FidelityFX-SDK-1.1.4" \
    -include "$repo_dir/src/refresh/vk_fsr3_compat.h" \
    "$repo_dir/tests/vk_fsr3_preflight.cpp" \
    "$repo_dir/src/refresh/vk_fsr3.cpp" \
    -pthread -Wl,--gc-sections -o "$build_dir/vk_fsr3_preflight_test"
"$build_dir/vk_fsr3_preflight_test"
