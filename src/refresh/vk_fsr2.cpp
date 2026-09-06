/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "vk_fsr2.h"

#if USE_VULKAN

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "ffx_fsr2.h"
#include "ffx_fsr2_vk.h"

struct q2_fsr2_context {
    FfxFsr2Context context = {};
    FfxFsr2Interface interface = {};
    void *scratch = nullptr;
    size_t scratch_size = 0;
    uint32_t render_width = 0;
    uint32_t render_height = 0;
    uint32_t display_width = 0;
    uint32_t display_height = 0;
    uint32_t frame_index = 0;
};

/* The Q2Pro Vulkan backend deliberately loads the Vulkan loader dynamically.
 * FSR2's 2.2.1 backend still references a few global Vulkan entry points
 * directly, so provide loader-backed shims instead of linking vulkan-1.dll or
 * libvulkan into the executable. */
static PFN_vkGetInstanceProcAddr q2_get_instance_proc_addr;
static PFN_vkGetDeviceProcAddr q2_get_device_proc_addr;

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device, const char *name)
{
    return q2_get_device_proc_addr ? q2_get_device_proc_addr(device, name) : nullptr;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice device, const char *layer, uint32_t *count,
    VkExtensionProperties *properties)
{
    auto function = q2_get_instance_proc_addr ?
        reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
            q2_get_instance_proc_addr(nullptr, "vkEnumerateDeviceExtensionProperties")) : nullptr;
    return function ? function(device, layer, count, properties) : VK_ERROR_INITIALIZATION_FAILED;
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice device, VkPhysicalDeviceProperties *properties)
{
    auto function = q2_get_instance_proc_addr ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            q2_get_instance_proc_addr(nullptr, "vkGetPhysicalDeviceProperties")) : nullptr;
    if (function)
        function(device, properties);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice device, VkPhysicalDeviceProperties2 *properties)
{
    auto function = q2_get_instance_proc_addr ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            q2_get_instance_proc_addr(nullptr, "vkGetPhysicalDeviceProperties2")) : nullptr;
    if (function)
        function(device, properties);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice device, VkPhysicalDeviceFeatures2 *features)
{
    auto function = q2_get_instance_proc_addr ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            q2_get_instance_proc_addr(nullptr, "vkGetPhysicalDeviceFeatures2")) : nullptr;
    if (function)
        function(device, features);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice device, VkPhysicalDeviceMemoryProperties *properties)
{
    auto function = q2_get_instance_proc_addr ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            q2_get_instance_proc_addr(nullptr, "vkGetPhysicalDeviceMemoryProperties")) : nullptr;
    if (function)
        function(device, properties);
}

static void q2_fsr2_message(FfxFsr2MsgType type, const wchar_t *message)
{
    (void)type;
    (void)message;
}

extern "C" q2_fsr2_context_t *Q2_FSR2_Create(
    VkPhysicalDevice physical_device, VkDevice device,
    PFN_vkGetInstanceProcAddr get_instance_proc_addr,
    PFN_vkGetDeviceProcAddr get_device_proc_addr,
    uint32_t render_width, uint32_t render_height,
    uint32_t display_width, uint32_t display_height)
{
    if (!physical_device || !device || !get_device_proc_addr ||
        !render_width || !render_height || !display_width || !display_height)
        return nullptr;

    q2_get_instance_proc_addr = get_instance_proc_addr;
    q2_get_device_proc_addr = get_device_proc_addr;
    q2_fsr2_context_t *result = new q2_fsr2_context_t;
    result->render_width = render_width;
    result->render_height = render_height;
    result->display_width = display_width;
    result->display_height = display_height;
    FfxFsr2ContextDescription description = {};
    result->scratch_size = ffxFsr2GetScratchMemorySizeVK(physical_device);
    result->scratch = std::malloc(result->scratch_size);
    if (!result->scratch)
        goto fail;

    if (ffxFsr2GetInterfaceVK(&result->interface, result->scratch,
                              result->scratch_size, physical_device,
                              get_device_proc_addr) != FFX_OK)
        goto fail;

    description.flags = FFX_FSR2_ENABLE_DEBUG_CHECKING;
    description.maxRenderSize = { render_width, render_height };
    description.displaySize = { display_width, display_height };
    description.callbacks = result->interface;
    description.device = ffxGetDeviceVK(device);
    description.fpMessage = q2_fsr2_message;
    if (ffxFsr2ContextCreate(&result->context, &description) != FFX_OK)
        goto fail;

    return result;

fail:
    if (result->scratch)
        std::free(result->scratch);
    delete result;
    return nullptr;
}

extern "C" void Q2_FSR2_Destroy(q2_fsr2_context_t *context)
{
    if (!context)
        return;
    ffxFsr2ContextDestroy(&context->context);
    std::free(context->scratch);
    delete context;
}

extern "C" bool Q2_FSR2_Dispatch(
    q2_fsr2_context_t *context, VkCommandBuffer command_buffer,
    VkImage color, VkImageView color_view, VkFormat color_format,
    VkImage depth, VkImageView depth_view, VkFormat depth_format,
    VkImage motion, VkImageView motion_view, VkFormat motion_format,
    VkImage output, VkImageView output_view, VkFormat output_format,
    float frame_time_ms, float vertical_fov_radians, bool reset)
{
    if (!context || !command_buffer || !color || !color_view || !depth ||
        !depth_view || !motion || !motion_view || !output || !output_view)
        return false;

    FfxFsr2DispatchDescription description = {};
    description.commandList = ffxGetCommandListVK(command_buffer);
    description.color = ffxGetTextureResourceVK(
        &context->context, color, color_view, context->render_width,
        context->render_height, color_format, L"q2_scene",
        FFX_RESOURCE_STATE_COMPUTE_READ);
    description.depth = ffxGetTextureResourceVK(
        &context->context, depth, depth_view, context->render_width,
        context->render_height, depth_format, L"q2_depth",
        FFX_RESOURCE_STATE_COMPUTE_READ);
    description.motionVectors = ffxGetTextureResourceVK(
        &context->context, motion, motion_view, context->render_width,
        context->render_height, motion_format, L"q2_motion",
        FFX_RESOURCE_STATE_COMPUTE_READ);
    description.output = ffxGetTextureResourceVK(
        &context->context, output, output_view, context->display_width,
        context->display_height, output_format, L"q2_fsr_output",
        FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    description.jitterOffset = { 0.0f, 0.0f };
    description.motionVectorScale = { 1.0f, 1.0f };
    description.renderSize = { context->render_width, context->render_height };
    description.enableSharpening = false;
    description.sharpness = 0.0f;
    description.frameTimeDelta = std::max(frame_time_ms, 1.0f);
    description.preExposure = 1.0f;
    description.reset = reset;
    description.cameraNear = 2.0f;
    description.cameraFar = 2048.0f;
    description.cameraFovAngleVertical = vertical_fov_radians;
    description.viewSpaceToMetersFactor = 1.0f;

    FfxErrorCode error = ffxFsr2ContextDispatch(&context->context, &description);
    if (error != FFX_OK)
        return false;
    context->frame_index++;
    return true;
}

#endif
