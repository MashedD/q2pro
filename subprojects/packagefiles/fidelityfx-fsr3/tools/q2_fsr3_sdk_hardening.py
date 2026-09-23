#!/usr/bin/env python3
"""Make partial FSR3 context creation safe for the frame-generation retry."""

from pathlib import Path
import sys


def replace_once(text, old, new, label):
    if old not in text:
        raise RuntimeError(f"FSR3 SDK hardening pattern not found: {label}")
    return text.replace(old, new, 1)


def write_if_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def harden_vulkan_frame_interpolation_wait(root):
    """Use a hybrid wait on Linux to reduce presenter CPU use."""

    helper_path = root / "sdk/src/backends/vk/FrameInterpolationSwapchain/FrameInterpolationSwapchainVK_Helpers.cpp"
    helper = helper_path.read_text()
    marker = "// Q2PRO_LINUX_HYBRID_WAIT"
    if marker in helper:
        return

    old = (
        "void waitForPerformanceCount(const int64_t targetCount)\n"
        "{\n"
        "    int64_t currentCount = 0;\n"
        "    do\n"
        "    {\n"
        "        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currentCount));\n"
        "    } while (currentCount < targetCount);\n"
        "}\n"
    )
    new = (
        "void waitForPerformanceCount(const int64_t targetCount)\n"
        "{\n"
        "#ifdef _WIN32\n"
        "    int64_t currentCount = 0;\n"
        "    do\n"
        "    {\n"
        "        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currentCount));\n"
        "    } while (currentCount < targetCount);\n"
        "#else\n"
        "    // Q2PRO_LINUX_HYBRID_WAIT: sleep for the coarse interval, then spin\n"
        "    // during the final 0.5ms so the present deadline stays precise. The\n"
        "    // Linux platform shim reports QPC in nanoseconds.\n"
        "    constexpr int64_t spinWindow = 500000;\n"
        "    int64_t currentCount = 0;\n"
        "    for (;;)\n"
        "    {\n"
        "        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currentCount));\n"
        "        const int64_t remaining = targetCount - currentCount;\n"
        "        if (remaining <= 0)\n"
        "            break;\n"
        "        if (remaining > spinWindow)\n"
        "            std::this_thread::sleep_for(std::chrono::nanoseconds(remaining - spinWindow));\n"
        "    }\n"
        "#endif\n"
        "}\n"
    )
    write_if_changed(helper_path, replace_once(helper, old, new, "Vulkan hybrid presenter wait"))


def guard_once(text, body, macro):
    """Wrap an SDK fragment exactly once, even across repeated configure runs."""

    guard = f"#ifdef {macro}\n"
    wrapped = guard + body + "#endif\n"
    if wrapped in text:
        return text
    if body not in text:
        return text
    return text.replace(body, wrapped, 1)


def harden_vulkan_frame_interpolation_source(root):
    """Keep the Vulkan provider buildable with GCC/MinGW and Linux."""

    source_path = root / "sdk/src/backends/vk/FrameInterpolationSwapchain/FrameInterpolationSwapchainVK.cpp"
    source = source_path.read_text()

    platform_header = root / "q2_fsr3_sdk_platform.h"
    packaged_platform_header = root.parent / "packagefiles/fidelityfx-fsr3/q2_fsr3_sdk_platform.h"
    if packaged_platform_header.exists():
        write_if_changed(platform_header, packaged_platform_header.read_text())

    # The SDK's provider is shared by Windows and Linux. The tracked q2pro
    # platform layer supplies synchronization/timing primitives on Linux.
    for path in [
        root / "sdk/src/backends/vk/FrameInterpolationSwapchain/FrameInterpolationSwapchainVK.cpp",
        root / "sdk/src/backends/vk/FrameInterpolationSwapchain/FrameInterpolationSwapchainVK.h",
        root / "sdk/src/backends/vk/FrameInterpolationSwapchain/FrameInterpolationSwapchainVK_Helpers.h",
    ]:
        text = path.read_text()
        text = text.replace(
            '#include <Windows.h>\n#include <synchapi.h>\n',
            '#include <q2_fsr3_sdk_platform.h>\n',
        )
        text = text.replace(
            '#include <Windows.h>\n',
            '#include <q2_fsr3_sdk_platform.h>\n',
        )
        write_if_changed(path, text)

    source = source_path.read_text()
    source = source.replace("#pragma once\n", "", 1)
    source = source.replace(
        '#ifdef VK_USE_PLATFORM_WIN32_KHR\n'
        '#ifdef VK_USE_PLATFORM_WIN32_KHR\n'
        '    VkSurfaceFullScreenExclusiveInfoEXT      surfaceFullScreenExclusive;\n'
        '    VkSurfaceFullScreenExclusiveWin32InfoEXT surfaceFullScreenExclusiveWin32;\n'
        '#endif\n'
        '#endif\n'
        '#endif\n',
        '#ifdef VK_USE_PLATFORM_WIN32_KHR\n'
        '    VkSurfaceFullScreenExclusiveInfoEXT      surfaceFullScreenExclusive;\n'
        '    VkSurfaceFullScreenExclusiveWin32InfoEXT surfaceFullScreenExclusiveWin32;\n'
        '#endif\n',
    )
    source = source.replace(
        '#ifdef VK_USE_PLATFORM_WIN32_KHR\n'
        '    VkSurfaceFullScreenExclusiveInfoEXT      surfaceFullScreenExclusive;\n'
        '    VkSurfaceFullScreenExclusiveWin32InfoEXT surfaceFullScreenExclusiveWin32;\n'
        '#endif\n'
        '#endif\n'
        '#endif\n',
        '#ifdef VK_USE_PLATFORM_WIN32_KHR\n'
        '    VkSurfaceFullScreenExclusiveInfoEXT      surfaceFullScreenExclusive;\n'
        '    VkSurfaceFullScreenExclusiveWin32InfoEXT surfaceFullScreenExclusiveWin32;\n'
        '#endif\n',
    )
    source = guard_once(
        source,
        '    VkSurfaceFullScreenExclusiveInfoEXT      surfaceFullScreenExclusive;\n'
        '    VkSurfaceFullScreenExclusiveWin32InfoEXT surfaceFullScreenExclusiveWin32;\n',
        'VK_USE_PLATFORM_WIN32_KHR',
    )
    fullscreen_info = (
        '        case VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT:\n'
        '            realSwapchainCreateInfo.surfaceFullScreenExclusive       = *reinterpret_cast<const VkSurfaceFullScreenExclusiveInfoEXT*>(pCurrent);\n'
        '            realSwapchainCreateInfo.surfaceFullScreenExclusive.pNext = const_cast<void*>(realSwapchainCreateInfo.swapchain.pNext);  // because pNext is void* instead of const void* in vulkan header\n'
        '            realSwapchainCreateInfo.swapchain.pNext                  = &realSwapchainCreateInfo.surfaceFullScreenExclusive;\n'
        '            break;\n'
    )
    source = source.replace(
        '#ifdef VK_USE_PLATFORM_WIN32_KHR\n' + fullscreen_info +
        '#endif\n#endif\n#endif\n',
        '#ifdef VK_USE_PLATFORM_WIN32_KHR\n' + fullscreen_info + '#endif\n',
    )
    source = guard_once(source, fullscreen_info, 'VK_USE_PLATFORM_WIN32_KHR')
    fullscreen_win32 = (
        '        case VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT:\n'
        '            FFX_USE_PNEXT_AS_IS(surfaceFullScreenExclusiveWin32, VkSurfaceFullScreenExclusiveWin32InfoEXT);\n'
        '            break;\n'
    )
    source = source.replace(
        '#ifdef VK_USE_PLATFORM_WIN32_KHR\n'
        '#ifdef VK_USE_PLATFORM_WIN32_KHR\n' + fullscreen_win32 +
        '#endif\n#endif\n#endif\n#endif\n',
        '#ifdef VK_USE_PLATFORM_WIN32_KHR\n' + fullscreen_win32 + '#endif\n',
    )
    source = guard_once(source, fullscreen_win32, 'VK_USE_PLATFORM_WIN32_KHR')

    if "reinterpret_cast<FfxWaitCallbackFunc>(valuePtr)" not in source:
        source = replace_once(
            source,
            "pSwapChainVK->setWaitCallback(static_cast<FfxWaitCallbackFunc>(valuePtr));",
            "pSwapChainVK->setWaitCallback(reinterpret_cast<FfxWaitCallbackFunc>(valuePtr));",
            "GCC function-pointer conversion",
        )

    if "reinterpret_cast<PFN_vkDestroySwapchainKHR>(vkDestroySwapchainFFX)" not in source:
        source = source.replace(
            "functions->destroySwapchainKHR   = vkDestroySwapchainFFX;",
            "functions->destroySwapchainKHR   = reinterpret_cast<PFN_vkDestroySwapchainKHR>(vkDestroySwapchainFFX);",
        )
        source = source.replace(
            "functions->getSwapchainImagesKHR = vkGetSwapchainImagesFFX;",
            "functions->getSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(vkGetSwapchainImagesFFX);",
        )
        source = source.replace(
            "functions->acquireNextImageKHR   = vkAcquireNextImageFFX;",
            "functions->acquireNextImageKHR   = reinterpret_cast<PFN_vkAcquireNextImageKHR>(vkAcquireNextImageFFX);",
        )
        source = source.replace(
            "functions->queuePresentKHR       = vkQueuePresentFFX;",
            "functions->queuePresentKHR       = reinterpret_cast<PFN_vkQueuePresentKHR>(vkQueuePresentFFX);",
        )
        source = source.replace(
            "functions->setHdrMetadataEXT = vkSetHdrMetadataFFX;",
            "functions->setHdrMetadataEXT = reinterpret_cast<PFN_vkSetHdrMetadataEXT>(vkSetHdrMetadataFFX);",
        )

    source = source.replace(
        "gameSwapChain = reinterpret_cast<VkSwapchainKHR>(pSwapChainVK);",
        "gameSwapChain = reinterpret_cast<FfxSwapchain>(pSwapChainVK);",
    )

    if "SubmissionSemaphores emptySignals;" not in source:
        source = replace_once(
            source,
            "presenter->presentQueue.submit(VK_NULL_HANDLE, toWait, SubmissionSemaphores());",
            "SubmissionSemaphores emptySignals;\n"
            "                        presenter->presentQueue.submit(VK_NULL_HANDLE, toWait, emptySignals);",
            "presenter empty semaphore temporary",
        )
        source = replace_once(
            source,
            "res = presentInfo.gameQueue.submit(VK_NULL_HANDLE, toWait, SubmissionSemaphores());",
            "SubmissionSemaphores emptySignals;\n"
            "            res = presentInfo.gameQueue.submit(VK_NULL_HANDLE, toWait, emptySignals);",
            "game queue empty semaphore temporary",
        )

    if "q2_fsr3_vk_image_from_resource(frameInfo.resource.resource)" not in source:
        source = source.replace(
            "static_cast<VkImage>(frameInfo.resource.resource)",
            "q2_fsr3_vk_image_from_resource(frameInfo.resource.resource)",
        )
        source = source.replace(
            "static_cast<VkImage>(interpolatedResource.resource)",
            "q2_fsr3_vk_image_from_resource(interpolatedResource.resource)",
        )
        source = source.replace(
            "static_cast<VkImage>(resource.resource)",
            "q2_fsr3_vk_image_from_resource(resource.resource)",
        )
        source = source.replace(
            "static_cast<VkImage>(presentInfo.currentUiSurface.resource)",
            "q2_fsr3_vk_image_from_resource(presentInfo.currentUiSurface.resource)",
        )

    for expression in [
        "resource.image",
        "semaphore",
        "presentInfo.presentQueue.queue",
        "presentInfo.asyncComputeQueue.queue",
        "imageAcquireQueue.queue",
        "presentInfo.realSwapchainImages[i]",
    ]:
        source = source.replace(
            f"debugNameSetter.setDebugName({expression},",
            f"debugNameSetter.setDebugName(q2_fsr3_vk_handle_as_resource({expression}),",
        )

    ui_composition_path = root / "sdk/src/backends/vk/FrameInterpolationSwapchain/FrameInterpolationSwapchainVK_UiComposition.cpp"
    ui_composition = ui_composition_path.read_text()
    ui_composition = ui_composition.replace(
        "s_uiCompositionRenderPass = nullptr;",
        "s_uiCompositionRenderPass = VK_NULL_HANDLE;",
    )
    write_if_changed(ui_composition_path, ui_composition)

    debug_pacing_path = root / "sdk/src/backends/vk/FrameInterpolationSwapchain/FrameInterpolationSwapchainVK_DebugPacing.cpp"
    debug_pacing = debug_pacing_path.read_text()
    debug_pacing = debug_pacing.replace(
        "s_debugPacingRenderPass = nullptr;",
        "s_debugPacingRenderPass = VK_NULL_HANDLE;",
    )
    write_if_changed(debug_pacing_path, debug_pacing)

    write_if_changed(source_path, source)


def harden_breadcrumb_uint64_format(root):
    """Use a format that matches the SDK's explicitly 64-bit values."""

    header_path = root / "sdk/src/shared/ffx_breadcrumbs_list.h"
    header = header_path.read_text()
    old = '#define FFX_BREADCRUMBS_APPEND_UINT64(buff, count, number) \\\n    FFX_BREADCRUMBS_APPEND_NUMBER(buff, count, number, 21, "%zu")'
    new = '#define FFX_BREADCRUMBS_APPEND_UINT64(buff, count, number) \\\n    FFX_BREADCRUMBS_APPEND_NUMBER(buff, count, static_cast<unsigned long long>(number), 21, "%llu")'
    if old in header:
        write_if_changed(header_path, header.replace(old, new, 1))


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: q2_fsr3_sdk_hardening.py <fsr3-root>")

    root = Path(sys.argv[1])
    harden_vulkan_frame_interpolation_wait(root)
    harden_vulkan_frame_interpolation_source(root)
    harden_breadcrumb_uint64_format(root)
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
        write_if_changed(private_path, private)

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
        write_if_changed(source_path, source)

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
            write_if_changed(component_private_path, component_private)
            write_if_changed(component_source_path, component_source)

    if "sharedResourceCreated" not in private_path.read_text():
        raise RuntimeError("FSR3 SDK hardening did not update the private context")
    if "frameInterpolationContextCreated" not in source_path.read_text():
        raise RuntimeError("FSR3 SDK hardening did not update context cleanup")


if __name__ == "__main__":
    main()
