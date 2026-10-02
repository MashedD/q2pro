#include "ffx_shader_blobs.h"

#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/ffx_types.h>

#include "q2_fsr3_shader_blobs.h"

FfxErrorCode ffxGetPermutationBlobByIndex(
    FfxEffect effectId,
    FfxPass passId,
    FfxBindStage bindStage,
    uint32_t permutationOptions,
    FfxShaderBlob* outBlob)
{
    if (bindStage != FFX_BIND_COMPUTE_SHADER_STAGE)
        return FFX_ERROR_INVALID_ENUM;

    switch (effectId) {
    case FFX_EFFECT_FSR3UPSCALER:
        return fsr3UpscalerGetPermutationBlobByIndex(
            static_cast<FfxFsr3UpscalerPass>(passId), permutationOptions, outBlob);
    case FFX_EFFECT_FRAMEINTERPOLATION:
        return frameInterpolationGetPermutationBlobByIndex(
            static_cast<FfxFrameInterpolationPass>(passId), bindStage,
            permutationOptions, outBlob);
    case FFX_EFFECT_OPTICALFLOW:
        return opticalflowGetPermutationBlobByIndex(
            static_cast<FfxOpticalflowPass>(passId), permutationOptions, outBlob);
    default:
        return FFX_ERROR_INVALID_ENUM;
    }
}

FfxErrorCode ffxIsWave64(
    FfxEffect effectId, uint32_t permutationOptions, bool& isWave64)
{
    switch (effectId) {
    case FFX_EFFECT_FSR3UPSCALER:
        return fsr3UpscalerIsWave64(permutationOptions, isWave64);
    case FFX_EFFECT_FRAMEINTERPOLATION:
        return frameInterpolationIsWave64(permutationOptions, isWave64);
    case FFX_EFFECT_OPTICALFLOW:
        return opticalflowIsWave64(permutationOptions, isWave64);
    default:
        return FFX_ERROR_INVALID_ENUM;
    }
}
