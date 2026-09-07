#include "ffx_shader_blobs.h"

#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/ffx_types.h>

#include "q2_fsr3_shader_blobs.h"

FfxErrorCode ffxGetPermutationBlobByIndex(
    FfxEffect effectId,
    FfxPass passId,
    FfxBindStage bindStage,
    uint32_t permutationOptions,
    FfxShaderBlob* outBlob)
{
    if (effectId != FFX_EFFECT_FSR3UPSCALER ||
        bindStage != FFX_BIND_COMPUTE_SHADER_STAGE)
        return FFX_ERROR_INVALID_ENUM;

    return fsr3UpscalerGetPermutationBlobByIndex(
        static_cast<FfxFsr3UpscalerPass>(passId), permutationOptions, outBlob);
}

FfxErrorCode ffxIsWave64(
    FfxEffect effectId, uint32_t permutationOptions, bool& isWave64)
{
    if (effectId != FFX_EFFECT_FSR3UPSCALER)
        return FFX_ERROR_INVALID_ENUM;

    return fsr3UpscalerIsWave64(permutationOptions, isWave64);
}
