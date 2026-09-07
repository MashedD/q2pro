// This file is part of the FidelityFX SDK.
//
// The upstream shader compiler emits a large permutation header using a
// Windows-only executable. Q2Pro generates the same Vulkan SPIR-V blobs with
// glslc during the Meson build, keeping the source pin and the build portable.

#include "ffx_fsr2_shaders_vk.h"
#include "q2_fsr2_shader_blobs.h"

extern "C" Fsr2ShaderBlobVK fsr2GetPermutationBlobByIndexVK(
    FfxFsr2Pass passId, uint32_t permutationOptions)
{
    const uint32_t use_lut =
        permutationOptions & FSR2_SHADER_PERMUTATION_REPROJECT_USE_LANCZOS_TYPE;

    switch (passId) {
    case FFX_FSR2_PASS_DEPTH_CLIP:
        return q2_fsr2_depth_clip[use_lut != 0];
    case FFX_FSR2_PASS_RECONSTRUCT_PREVIOUS_DEPTH:
        return q2_fsr2_reconstruct_previous_depth[use_lut != 0];
    case FFX_FSR2_PASS_LOCK:
        return q2_fsr2_lock[use_lut != 0];
    case FFX_FSR2_PASS_ACCUMULATE:
    case FFX_FSR2_PASS_ACCUMULATE_SHARPEN:
        return q2_fsr2_accumulate[use_lut != 0];
    case FFX_FSR2_PASS_RCAS:
        return q2_fsr2_rcas[use_lut != 0];
    case FFX_FSR2_PASS_COMPUTE_LUMINANCE_PYRAMID:
        return q2_fsr2_compute_luminance_pyramid[use_lut != 0];
    case FFX_FSR2_PASS_GENERATE_REACTIVE:
        return q2_fsr2_autogen_reactive[use_lut != 0];
    case FFX_FSR2_PASS_TCR_AUTOGENERATE:
        return q2_fsr2_tcr_autogen[use_lut != 0];
    default:
        break;
    }

    return {};
}
