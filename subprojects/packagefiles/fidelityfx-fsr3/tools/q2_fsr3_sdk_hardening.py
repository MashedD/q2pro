#!/usr/bin/env python3
"""Make partial FSR3 context creation safe for the frame-generation retry."""

from pathlib import Path
import sys


def replace_once(text, old, new, label):
    if old not in text:
        raise RuntimeError(f"FSR3 SDK hardening pattern not found: {label}")
    return text.replace(old, new, 1)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: q2_fsr3_sdk_hardening.py <fsr3-root>")

    root = Path(sys.argv[1])
    private_path = root / "sdk/src/components/fsr3/ffx_fsr3_private.h"
    source_path = root / "sdk/src/components/fsr3/ffx_fsr3.cpp"

    private = private_path.read_text()
    if "sharedResourceCreated" not in private:
        private = replace_once(
            private,
            "    FfxUInt32                               effectContextIdFrameGeneration;\n",
            "    FfxUInt32                               effectContextIdFrameGeneration;\n"
            "    bool                                    sharedBackendContextCreated;\n"
            "    bool                                    upscalerContextCreated;\n"
            "    bool                                    opticalflowContextCreated;\n"
            "    bool                                    frameInterpolationContextCreated;\n"
            "    bool                                    sharedResourceCreated[FFX_FSR3_RESOURCE_IDENTIFIER_COUNT];\n",
            "partial-cleanup state",
        )
        private_path.write_text(private)

    source = source_path.read_text()
    if "sharedBackendContextCreated" not in source:
        source = replace_once(
            source,
            "        FFX_VALIDATE(contextPrivate->backendInterfaceSharedResources.fpCreateBackendContext(&contextPrivate->backendInterfaceSharedResources,\n"
            "            FFX_EFFECT_SHAREDRESOURCES,\n"
            "            nullptr,\n"
            "            &contextPrivate->effectContextIdSharedResources));\n",
            "        FFX_VALIDATE(contextPrivate->backendInterfaceSharedResources.fpCreateBackendContext(&contextPrivate->backendInterfaceSharedResources,\n"
            "            FFX_EFFECT_SHAREDRESOURCES,\n"
            "            nullptr,\n"
            "            &contextPrivate->effectContextIdSharedResources));\n"
            "        contextPrivate->sharedBackendContextCreated = true;\n",
            "shared backend stage",
        )
        source = replace_once(
            source,
            "\t\tFFX_VALIDATE(ffxFsr3UpscalerContextCreate(&contextPrivate->upscalerContext, &upDesc));\n",
            "\t\tFFX_VALIDATE(ffxFsr3UpscalerContextCreate(&contextPrivate->upscalerContext, &upDesc));\n"
            "        contextPrivate->upscalerContextCreated = true;\n",
            "upscaler stage",
        )
        source = replace_once(
            source,
            "        FFX_VALIDATE(ffxOpticalflowContextCreate(&contextPrivate->ofContext, &ofDescription));\n",
            "        FFX_VALIDATE(ffxOpticalflowContextCreate(&contextPrivate->ofContext, &ofDescription));\n"
            "        contextPrivate->opticalflowContextCreated = true;\n",
            "optical-flow stage",
        )
        source = replace_once(
            source,
            "        FFX_VALIDATE(ffxFrameInterpolationContextCreate(&contextPrivate->fiContext, &fiDescription));\n",
            "        FFX_VALIDATE(ffxFrameInterpolationContextCreate(&contextPrivate->fiContext, &fiDescription));\n"
            "        contextPrivate->frameInterpolationContextCreated = true;\n",
            "frame-interpolation stage",
        )
        source = replace_once(
            source,
            "        FFX_VALIDATE(contextDescription->backendInterfaceSharedResources.fpCreateResource(\n"
            "            &contextDescription->backendInterfaceSharedResources, &ofResourceDescs.opticalFlowVector, contextPrivate->effectContextIdSharedResources, &contextPrivate->sharedResources[FFX_FSR3_RESOURCE_IDENTIFIER_OPTICAL_FLOW_VECTOR]));\n"
            "        FFX_VALIDATE(contextDescription->backendInterfaceSharedResources.fpCreateResource(\n"
            "            &contextDescription->backendInterfaceSharedResources, &ofResourceDescs.opticalFlowSCD, contextPrivate->effectContextIdSharedResources, &contextPrivate->sharedResources[FFX_FSR3_RESOURCE_IDENTIFIER_OPTICAL_FLOW_SCD_OUTPUT]));\n",
            "        FFX_VALIDATE(contextDescription->backendInterfaceSharedResources.fpCreateResource(\n"
            "            &contextDescription->backendInterfaceSharedResources, &ofResourceDescs.opticalFlowVector, contextPrivate->effectContextIdSharedResources, &contextPrivate->sharedResources[FFX_FSR3_RESOURCE_IDENTIFIER_OPTICAL_FLOW_VECTOR]));\n"
            "        contextPrivate->sharedResourceCreated[FFX_FSR3_RESOURCE_IDENTIFIER_OPTICAL_FLOW_VECTOR] = true;\n"
            "        FFX_VALIDATE(contextDescription->backendInterfaceSharedResources.fpCreateResource(\n"
            "            &contextDescription->backendInterfaceSharedResources, &ofResourceDescs.opticalFlowSCD, contextPrivate->effectContextIdSharedResources, &contextPrivate->sharedResources[FFX_FSR3_RESOURCE_IDENTIFIER_OPTICAL_FLOW_SCD_OUTPUT]));\n"
            "        contextPrivate->sharedResourceCreated[FFX_FSR3_RESOURCE_IDENTIFIER_OPTICAL_FLOW_SCD_OUTPUT] = true;\n",
            "optical-flow resources",
        )
        source = replace_once(
            source,
            "            FFX_VALIDATE(contextDescription->backendInterfaceSharedResources.fpCreateResource(\n                &contextDescription->backendInterfaceSharedResources, &dilD, contextPrivate->effectContextIdSharedResources, &contextPrivate->sharedResources[FFX_FSR3_RESOURCE_IDENTIFIER_DILATED_DEPTH_0 + (i * FFX_FSR3_RESOURCE_IDENTIFIER_UPSCALED_COUNT)]));\n",
            "            const FfxUInt32 depthId = FFX_FSR3_RESOURCE_IDENTIFIER_DILATED_DEPTH_0 + (i * FFX_FSR3_RESOURCE_IDENTIFIER_UPSCALED_COUNT);\n"
            "            FFX_VALIDATE(contextDescription->backendInterfaceSharedResources.fpCreateResource(\n                &contextDescription->backendInterfaceSharedResources, &dilD, contextPrivate->effectContextIdSharedResources, &contextPrivate->sharedResources[depthId]));\n"
            "            contextPrivate->sharedResourceCreated[depthId] = true;\n",
            "dilated-depth resource",
        )
        source = replace_once(
            source,
            "            FFX_VALIDATE(contextDescription->backendInterfaceSharedResources.fpCreateResource(\n                &contextDescription->backendInterfaceSharedResources, &dilMVs, contextPrivate->effectContextIdSharedResources, &contextPrivate->sharedResources[FFX_FSR3_RESOURCE_IDENTIFIER_DILATED_MOTION_VECTORS_0 + (i * FFX_FSR3_RESOURCE_IDENTIFIER_UPSCALED_COUNT)]));\n",
            "            const FfxUInt32 motionId = FFX_FSR3_RESOURCE_IDENTIFIER_DILATED_MOTION_VECTORS_0 + (i * FFX_FSR3_RESOURCE_IDENTIFIER_UPSCALED_COUNT);\n"
            "            FFX_VALIDATE(contextDescription->backendInterfaceSharedResources.fpCreateResource(\n                &contextDescription->backendInterfaceSharedResources, &dilMVs, contextPrivate->effectContextIdSharedResources, &contextPrivate->sharedResources[motionId]));\n"
            "            contextPrivate->sharedResourceCreated[motionId] = true;\n",
            "dilated-motion resource",
        )
        source = replace_once(
            source,
            "            FFX_VALIDATE(contextDescription->backendInterfaceSharedResources.fpCreateResource(\n                &contextDescription->backendInterfaceSharedResources, &recND, contextPrivate->effectContextIdSharedResources, &contextPrivate->sharedResources[FFX_FSR3_RESOURCE_IDENTIFIER_RECONSTRUCTED_PREVIOUS_NEAREST_DEPTH_0 + (i * FFX_FSR3_RESOURCE_IDENTIFIER_UPSCALED_COUNT)]));\n",
            "            const FfxUInt32 reconstructedId = FFX_FSR3_RESOURCE_IDENTIFIER_RECONSTRUCTED_PREVIOUS_NEAREST_DEPTH_0 + (i * FFX_FSR3_RESOURCE_IDENTIFIER_UPSCALED_COUNT);\n"
            "            FFX_VALIDATE(contextDescription->backendInterfaceSharedResources.fpCreateResource(\n                &contextDescription->backendInterfaceSharedResources, &recND, contextPrivate->effectContextIdSharedResources, &contextPrivate->sharedResources[reconstructedId]));\n"
            "            contextPrivate->sharedResourceCreated[reconstructedId] = true;\n",
            "reconstructed-depth resource",
        )
        source = replace_once(
            source,
            "\tfor (FfxUInt32 i = 0; i < FFX_FSR3_RESOURCE_IDENTIFIER_COUNT; i++)\n    {\n        FFX_VALIDATE(contextPrivate->backendInterfaceSharedResources.fpDestroyResource(&contextPrivate->backendInterfaceSharedResources, contextPrivate->sharedResources[i], contextPrivate->effectContextIdSharedResources))\n    }\n    contextPrivate->backendInterfaceSharedResources.fpDestroyBackendContext(&contextPrivate->backendInterfaceSharedResources, contextPrivate->effectContextIdSharedResources);\n",
            "\tfor (FfxUInt32 i = 0; i < FFX_FSR3_RESOURCE_IDENTIFIER_COUNT; i++)\n    {\n        if (contextPrivate->sharedResourceCreated[i])\n            FFX_VALIDATE(contextPrivate->backendInterfaceSharedResources.fpDestroyResource(&contextPrivate->backendInterfaceSharedResources, contextPrivate->sharedResources[i], contextPrivate->effectContextIdSharedResources))\n    }\n    if (contextPrivate->sharedBackendContextCreated)\n        contextPrivate->backendInterfaceSharedResources.fpDestroyBackendContext(&contextPrivate->backendInterfaceSharedResources, contextPrivate->effectContextIdSharedResources);\n",
            "shared resource cleanup",
        )
        source = replace_once(
            source,
            "    if (!upscalingOnly)\n    {\n        FFX_VALIDATE(ffxFrameInterpolationContextDestroy(&contextPrivate->fiContext));\n        FFX_VALIDATE(ffxOpticalflowContextDestroy(&contextPrivate->ofContext));\n    }\n        \n    if (!interpolationOnly)\n    {\n        FFX_VALIDATE(ffxFsr3UpscalerContextDestroy(&contextPrivate->upscalerContext));\n    }\n",
            "    if (!upscalingOnly && contextPrivate->frameInterpolationContextCreated)\n    {\n        FFX_VALIDATE(ffxFrameInterpolationContextDestroy(&contextPrivate->fiContext));\n    }\n    if (!upscalingOnly && contextPrivate->opticalflowContextCreated)\n    {\n        FFX_VALIDATE(ffxOpticalflowContextDestroy(&contextPrivate->ofContext));\n    }\n        \n    if (!interpolationOnly && contextPrivate->upscalerContextCreated)\n    {\n        FFX_VALIDATE(ffxFsr3UpscalerContextDestroy(&contextPrivate->upscalerContext));\n    }\n",
            "component cleanup",
        )
        source_path.write_text(source)

    components = [
        (
            "sdk/src/components/opticalflow/ffx_opticalflow_private.h",
            "sdk/src/components/opticalflow/ffx_opticalflow.cpp",
            "    bool firstExecution;\n    bool refreshPipelineStates;\n",
            "    bool firstExecution;\n    bool refreshPipelineStates;\n    bool backendContextCreated;\n",
            "    errorCode = context->contextDescription.backendInterface.fpCreateBackendContext(&context->contextDescription.backendInterface, FFX_EFFECT_OPTICALFLOW, nullptr, &context->effectContextId);\n    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);\n",
            "    errorCode = context->contextDescription.backendInterface.fpCreateBackendContext(&context->contextDescription.backendInterface, FFX_EFFECT_OPTICALFLOW, nullptr, &context->effectContextId);\n    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);\n    context->backendContextCreated = true;\n",
            "    context->contextDescription.backendInterface.fpDestroyBackendContext(&context->contextDescription.backendInterface, context->effectContextId);\n\n    return FFX_OK;\n",
            "    context->contextDescription.backendInterface.fpDestroyBackendContext(&context->contextDescription.backendInterface, context->effectContextId);\n    context->backendContextCreated = false;\n\n    return FFX_OK;\n",
            "    FfxOpticalflowContext_Private* contextPrivate = (FfxOpticalflowContext_Private*)(context);\n    FfxErrorCode errorCode = opticalflowCreate(contextPrivate, contextDescription);\n\n    return errorCode;\n",
            "    FfxOpticalflowContext_Private* contextPrivate = (FfxOpticalflowContext_Private*)(context);\n    FfxErrorCode errorCode = opticalflowCreate(contextPrivate, contextDescription);\n    if (errorCode != FFX_OK && contextPrivate->backendContextCreated)\n        opticalflowRelease(contextPrivate);\n\n    return errorCode;\n",
        ),
        (
            "sdk/src/components/fsr3upscaler/ffx_fsr3upscaler_private.h",
            "sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp",
            "    bool                                firstExecution;\n    uint32_t                            resourceFrameIndex;\n",
            "    bool                                firstExecution;\n    bool                                backendContextCreated;\n    uint32_t                            resourceFrameIndex;\n",
            "    FfxErrorCode errorCode = context->contextDescription.backendInterface.fpCreateBackendContext(&context->contextDescription.backendInterface, FFX_EFFECT_FSR3UPSCALER, nullptr, &context->effectContextId);\n    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);\n",
            "    FfxErrorCode errorCode = context->contextDescription.backendInterface.fpCreateBackendContext(&context->contextDescription.backendInterface, FFX_EFFECT_FSR3UPSCALER, nullptr, &context->effectContextId);\n    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);\n    context->backendContextCreated = true;\n",
            "    context->contextDescription.backendInterface.fpDestroyBackendContext(&context->contextDescription.backendInterface, context->effectContextId);\n\n    return FFX_OK;\n",
            "    context->contextDescription.backendInterface.fpDestroyBackendContext(&context->contextDescription.backendInterface, context->effectContextId);\n    context->backendContextCreated = false;\n\n    return FFX_OK;\n",
            "    const FfxErrorCode errorCode = fsr3upscalerCreate(contextPrivate, contextDescription);\n\n    return errorCode;\n",
            "    const FfxErrorCode errorCode = fsr3upscalerCreate(contextPrivate, contextDescription);\n    if (errorCode != FFX_OK && contextPrivate->backendContextCreated)\n        fsr3upscalerRelease(contextPrivate);\n\n    return errorCode;\n",
        ),
        (
            "sdk/src/components/frameinterpolation/ffx_frameinterpolation_private.h",
            "sdk/src/components/frameinterpolation/ffx_frameinterpolation.cpp",
            "    bool                                        firstExecution;\n    bool                                        refreshPipelineStates;\n\n    bool                                        asyncSupported;\n",
            "    bool                                        firstExecution;\n    bool                                        refreshPipelineStates;\n    bool                                        backendContextCreated;\n\n    bool                                        asyncSupported;\n",
            "    FfxErrorCode errorCode = context->contextDescription.backendInterface.fpCreateBackendContext(&context->contextDescription.backendInterface, FFX_EFFECT_FRAMEINTERPOLATION, nullptr, &context->effectContextId);\n    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);\n",
            "    FfxErrorCode errorCode = context->contextDescription.backendInterface.fpCreateBackendContext(&context->contextDescription.backendInterface, FFX_EFFECT_FRAMEINTERPOLATION, nullptr, &context->effectContextId);\n    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);\n    context->backendContextCreated = true;\n",
            "    context->contextDescription.backendInterface.fpDestroyBackendContext(&context->contextDescription.backendInterface, context->effectContextId);\n\n    return FFX_OK;\n",
            "    context->contextDescription.backendInterface.fpDestroyBackendContext(&context->contextDescription.backendInterface, context->effectContextId);\n    context->backendContextCreated = false;\n\n    return FFX_OK;\n",
            "    FfxFrameInterpolationContext_Private* contextPrivate = (FfxFrameInterpolationContext_Private*)(context);\n    FfxErrorCode errorCode = frameinterpolationCreate(contextPrivate, contextDescription);\n\n    return errorCode;\n",
            "    FfxFrameInterpolationContext_Private* contextPrivate = (FfxFrameInterpolationContext_Private*)(context);\n    FfxErrorCode errorCode = frameinterpolationCreate(contextPrivate, contextDescription);\n    if (errorCode != FFX_OK && contextPrivate->backendContextCreated)\n        frameinterpolationRelease(contextPrivate);\n\n    return errorCode;\n",
        ),
    ]
    for (private_rel, source_rel, private_old, private_new,
         create_old, create_new, destroy_old, destroy_new,
         wrapper_old, wrapper_new) in components:
        component_private_path = root / private_rel
        component_source_path = root / source_rel
        component_private = component_private_path.read_text()
        component_source = component_source_path.read_text()
        if "backendContextCreated" not in component_private:
            component_private = replace_once(
                component_private, private_old, private_new,
                f"{private_rel} stage flag")
            component_source = replace_once(
                component_source, create_old, create_new,
                f"{source_rel} create stage")
            component_source = replace_once(
                component_source, destroy_old, destroy_new,
                f"{source_rel} destroy stage")
            component_source = replace_once(
                component_source, wrapper_old, wrapper_new,
                f"{source_rel} failure cleanup")
            component_private_path.write_text(component_private)
            component_source_path.write_text(component_source)

    if "sharedResourceCreated" not in private_path.read_text():
        raise RuntimeError("FSR3 SDK hardening did not update the private context")
    if "frameInterpolationContextCreated" not in source_path.read_text():
        raise RuntimeError("FSR3 SDK hardening did not update context cleanup")


if __name__ == "__main__":
    main()
