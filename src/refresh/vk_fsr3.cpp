/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "vk_fsr3.h"

#if USE_VULKAN

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>

#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>

extern "C" {
#include "shared/shared.h"
}

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

struct q2_fsr3_pipeline_label {
    uint32_t effect = 0, pass = 0, permutation = 0;
    char name[64] = {};
};

struct q2_fsr3_context {
    FfxCreatePipelineFunc create_pipeline = nullptr;
    std::map<VkPipeline, q2_fsr3_pipeline_label> pipeline_labels;
    VkQueryPool profile_pool = VK_NULL_HANDLE;
    bool profile_active = false;
    uint32_t profile_slot = 0;
    uint32_t profile_count[3] = {};
    q2_fsr3_pipeline_label profile_labels[3][64] = {};
    VkPipeline profile_pipeline = VK_NULL_HANDLE;
    FfxFsr3UpscalerContext context = {};
    FfxFsr3Context full_context = {};
    FfxInterface interface = {};
    FfxInterface shared_interface = {};
    FfxInterface frame_interpolation_interface = {};
    VkDeviceContext device_context = {};
    void *scratch = nullptr;
    size_t scratch_size = 0;
    FfxResourceInternal dilated_depth = {};
    FfxResourceInternal dilated_motion_vectors = {};
    FfxResourceInternal reconstructed_prev_nearest_depth = {};
    uint32_t shared_resources_count = 0;
    uint32_t render_width = 0;
    uint32_t render_height = 0;
    uint32_t display_width = 0;
    uint32_t display_height = 0;
    uint32_t frame_index = 0;
    bool frame_generation = false;
    bool full_context_created = false;
    FfxErrorCode last_error = FFX_OK;
};

/* Q2Pro loads the Vulkan loader dynamically. The FSR3 Vulkan backend uses a
 * few global physical-device entry points, so route those calls through the
 * renderer's loader instead of linking against the Vulkan loader. */
static PFN_vkGetInstanceProcAddr q2_get_instance_proc_addr;
static PFN_vkGetDeviceProcAddr q2_get_device_proc_addr;
static VkInstance q2_instance;
static q2_fsr3_capabilities_t q2_enabled_capabilities;
static q2_fsr3_context_t *q2_active_context;

static FfxErrorCode q2_fsr3_create_pipeline(FfxInterface *backend, FfxEffect effect,
    FfxPass pass, uint32_t permutation, const FfxPipelineDescription *description,
    FfxUInt32 id, FfxPipelineState *pipeline)
{
    auto *context = q2_active_context;
    FfxErrorCode result = context->create_pipeline(backend, effect, pass, permutation,
                                                  description, id, pipeline);
    if (result == FFX_OK) {
        auto &label = context->pipeline_labels[(VkPipeline)(uintptr_t)pipeline->pipeline];
        label = { static_cast<uint32_t>(effect), pass, permutation };
        for (unsigned i = 0; i + 1 < sizeof(label.name) && pipeline->name[i]; i++)
            label.name[i] = pipeline->name[i] < 128 ? static_cast<char>(pipeline->name[i]) : '?';
    }
    return result;
}

static VKAPI_ATTR void VKAPI_CALL q2_fsr3_bind_pipeline(VkCommandBuffer cmd,
    VkPipelineBindPoint point, VkPipeline pipeline)
{
    auto *context = q2_active_context;
    if (point == VK_PIPELINE_BIND_POINT_COMPUTE)
        context->profile_pipeline = pipeline;
    auto function = reinterpret_cast<PFN_vkCmdBindPipeline>(
        q2_get_device_proc_addr(context->device_context.vkDevice, "vkCmdBindPipeline"));
    function(cmd, point, pipeline);
}

static VKAPI_ATTR void VKAPI_CALL q2_fsr3_dispatch(VkCommandBuffer cmd,
    uint32_t x, uint32_t y, uint32_t z)
{
    auto *context = q2_active_context;
    auto dispatch = reinterpret_cast<PFN_vkCmdDispatch>(
        q2_get_device_proc_addr(context->device_context.vkDevice, "vkCmdDispatch"));
    uint32_t slot = context->profile_slot, index = context->profile_count[slot];
    bool profile = context->profile_active && index < 64;
    auto stamp = reinterpret_cast<PFN_vkCmdWriteTimestamp>(
        q2_get_device_proc_addr(context->device_context.vkDevice, "vkCmdWriteTimestamp"));
    if (profile) {
        context->profile_labels[slot][index] = context->pipeline_labels[context->profile_pipeline];
        stamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, context->profile_pool, slot * 128 + index * 2);
    }
    dispatch(cmd, x, y, z);
    if (profile) {
        stamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context->profile_pool, slot * 128 + index * 2 + 1);
        context->profile_count[slot]++;
    }
}

extern "C" void Q2_FSR3_ProfileBegin(q2_fsr3_context_t *context, VkCommandBuffer cmd,
    uint32_t slot, bool enabled, float period)
{
    if (!context || slot >= 3)
        return;
    context->profile_active = false;
    VkDevice device = context->device_context.vkDevice;
    if (!context->profile_pool && enabled) {
        VkQueryPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = 384;
        auto create = reinterpret_cast<PFN_vkCreateQueryPool>(q2_get_device_proc_addr(device, "vkCreateQueryPool"));
        if (create(device, &info, nullptr, &context->profile_pool) != VK_SUCCESS) {
            Com_WPrintf("FSR per-pass timestamp allocation failed\n");
            return;
        }
    }
    if (!context->profile_pool)
        return;
    if (context->profile_count[slot]) {
        uint64_t values[128] = {};
        auto read = reinterpret_cast<PFN_vkGetQueryPoolResults>(q2_get_device_proc_addr(device, "vkGetQueryPoolResults"));
        if (read(device, context->profile_pool, slot * 128, context->profile_count[slot] * 2,
                 sizeof(values), values, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            for (uint32_t i = 0; i < context->profile_count[slot]; i++) {
                const auto &label = context->profile_labels[slot][i];
                Com_Printf("FSR job: name=%s effect=%u pass=%u permutation=0x%x gpu_us=%.3f\n",
                    label.name, label.effect, label.pass, label.permutation,
                    (values[i * 2 + 1] - values[i * 2]) * period / 1000.0);
            }
        }
    }
    context->profile_count[slot] = 0;
    context->profile_slot = slot;
    if (enabled) {
        auto reset = reinterpret_cast<PFN_vkCmdResetQueryPool>(q2_get_device_proc_addr(device, "vkCmdResetQueryPool"));
        reset(cmd, context->profile_pool, slot * 128, 128);
        context->profile_active = true;
    }
}

static PFN_vkVoidFunction q2_fsr3_get_instance_proc(const char *name)
{
    return q2_get_instance_proc_addr && q2_instance ?
        q2_get_instance_proc_addr(q2_instance, name) : nullptr;
}

static VKAPI_ATTR VkResult VKAPI_CALL q2_fsr3_create_compute_pipelines(
    VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkComputePipelineCreateInfo *infos, const VkAllocationCallbacks *allocator,
    VkPipeline *pipelines)
{
    auto function = reinterpret_cast<PFN_vkCreateComputePipelines>(
        q2_get_device_proc_addr(device, "vkCreateComputePipelines"));
    if (!q2_enabled_capabilities.subgroup_size)
        return function(device, cache, count, infos, allocator, pipelines);
    std::vector<VkComputePipelineCreateInfo> copies(infos, infos + count);
    std::vector<VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT> sizes(count);
    for (uint32_t i = 0; i < count; i++) {
        const auto *head = static_cast<const VkBaseInStructure *>(copies[i].stage.pNext);
        sizes[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT;
        sizes[i].pNext = head && head->sType == sizes[i].sType ? head->pNext : head;
        sizes[i].requiredSubgroupSize = q2_enabled_capabilities.subgroup_size;
        copies[i].stage.pNext = &sizes[i];
    }
    return function(device, cache, count, copies.data(), allocator, pipelines);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL q2_fsr3_get_device_proc(
    VkDevice device, const char *name)
{
    PFN_vkVoidFunction function = q2_get_device_proc_addr && device ?
        q2_get_device_proc_addr(device, name) : nullptr;
    if (function && !strcmp(name, "vkCreateComputePipelines"))
        return reinterpret_cast<PFN_vkVoidFunction>(q2_fsr3_create_compute_pipelines);
    if (function && !strcmp(name, "vkCmdBindPipeline"))
        return reinterpret_cast<PFN_vkVoidFunction>(q2_fsr3_bind_pipeline);
    if (function && !strcmp(name, "vkCmdDispatch"))
        return reinterpret_cast<PFN_vkVoidFunction>(q2_fsr3_dispatch);
    if (!function && !std::strcmp(name, "vkGetBufferMemoryRequirements2KHR"))
        function = q2_get_device_proc_addr ?
            q2_get_device_proc_addr(device, "vkGetBufferMemoryRequirements2") : nullptr;
    return function;
}

static bool q2_fsr3_has_instance_functions(void)
{
    static const char *const names[] = {
        "vkEnumerateDeviceExtensionProperties",
        "vkGetPhysicalDeviceProperties",
        "vkGetPhysicalDeviceProperties2",
        "vkGetPhysicalDeviceFeatures",
        "vkGetPhysicalDeviceFeatures2",
        "vkGetPhysicalDeviceMemoryProperties",
    };

    for (const char *name : names) {
        if (!q2_fsr3_get_instance_proc(name))
            return false;
    }
    return true;
}

static bool q2_fsr3_has_device_functions(VkDevice device)
{
    static const char *const names[] = {
        "vkCreateDescriptorPool",
        "vkDestroyDescriptorPool",
        "vkCreateBuffer",
        "vkDestroyBuffer",
        "vkGetBufferMemoryRequirements",
        "vkGetBufferMemoryRequirements2KHR",
        "vkAllocateMemory",
        "vkFreeMemory",
        "vkMapMemory",
        "vkUnmapMemory",
        "vkBindBufferMemory",
        "vkCreateImage",
        "vkDestroyImage",
        "vkGetImageMemoryRequirements",
        "vkBindImageMemory",
        "vkCreateImageView",
        "vkDestroyImageView",
        "vkCreateSampler",
        "vkDestroySampler",
        "vkCreateDescriptorSetLayout",
        "vkDestroyDescriptorSetLayout",
        "vkAllocateDescriptorSets",
        "vkFreeDescriptorSets",
        "vkUpdateDescriptorSets",
        "vkCreateShaderModule",
        "vkDestroyShaderModule",
        "vkCreatePipelineLayout",
        "vkDestroyPipelineLayout",
        "vkCreateComputePipelines",
        "vkDestroyPipeline",
        "vkCmdPipelineBarrier",
        "vkCmdBindPipeline",
        "vkCmdBindDescriptorSets",
        "vkCmdDispatch",
        "vkCmdCopyBuffer",
        "vkCmdCopyImage",
        "vkCmdCopyBufferToImage",
        "vkCmdClearColorImage",
        "vkCmdFillBuffer",
    };

    if (!q2_get_device_proc_addr || !device)
        return false;
    for (const char *name : names) {
        if (!q2_fsr3_get_device_proc(device, name))
            return false;
    }
    return true;
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device, const char *name)
{
    return q2_fsr3_get_device_proc(device, name);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice device, const char *layer, uint32_t *count,
    VkExtensionProperties *properties)
{
    auto function = q2_get_instance_proc_addr ?
        reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
            q2_fsr3_get_instance_proc("vkEnumerateDeviceExtensionProperties")) : nullptr;
    if (!function || !count)
        return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t available = 0;
    VkResult result = function(device, layer, &available, nullptr);
    if (result != VK_SUCCESS)
        return result;
    std::vector<VkExtensionProperties> list(available);
    result = function(device, layer, &available, list.data());
    if (result != VK_SUCCESS)
        return result;
    uint32_t written = 0, capacity = properties ? *count : 0;
    for (uint32_t i = 0; i < available; i++) {
        const char *name = list[i].extensionName;
        if ((!q2_enabled_capabilities.fp16 &&
             !strcmp(name, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME)) ||
            (!q2_enabled_capabilities.subgroup_size &&
             !strcmp(name, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME)))
            continue;
        if (properties && written < capacity)
            properties[written] = list[i];
        written++;
    }
    *count = properties ? std::min(written, capacity) : written;
    return properties && written > capacity ? VK_INCOMPLETE : VK_SUCCESS;
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice device, VkPhysicalDeviceProperties *properties)
{
    auto function = q2_get_instance_proc_addr ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            q2_fsr3_get_instance_proc("vkGetPhysicalDeviceProperties")) : nullptr;
    if (function)
        function(device, properties);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice device, VkPhysicalDeviceProperties2 *properties)
{
    auto function = q2_get_instance_proc_addr ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            q2_fsr3_get_instance_proc("vkGetPhysicalDeviceProperties2")) : nullptr;
    if (function)
        function(device, properties);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice device, VkPhysicalDeviceFeatures2 *features)
{
    auto function = q2_get_instance_proc_addr ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            q2_fsr3_get_instance_proc("vkGetPhysicalDeviceFeatures2")) : nullptr;
    if (function)
        function(device, features);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice device, VkPhysicalDeviceFeatures *features)
{
    auto function = q2_get_instance_proc_addr ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(
            q2_fsr3_get_instance_proc("vkGetPhysicalDeviceFeatures")) : nullptr;
    if (function)
        function(device, features);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice device, VkPhysicalDeviceMemoryProperties *properties)
{
    auto function = q2_get_instance_proc_addr ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            q2_fsr3_get_instance_proc("vkGetPhysicalDeviceMemoryProperties")) : nullptr;
    if (function)
        function(device, properties);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice device, const VkBufferCreateInfo *create_info,
    const VkAllocationCallbacks *allocator, VkBuffer *buffer)
{
    auto function = q2_get_device_proc_addr ?
        reinterpret_cast<PFN_vkCreateBuffer>(
            q2_fsr3_get_device_proc(device, "vkCreateBuffer")) : nullptr;
    return function ? function(device, create_info, allocator, buffer) :
        VK_ERROR_INITIALIZATION_FAILED;
}

static void q2_fsr3_message(FfxMsgType type, const wchar_t *message)
{
    char text[256];
    size_t length = 0;

    if (!message)
        return;
    while (message[length] && length + 1 < sizeof(text)) {
        const wchar_t character = message[length];
        text[length] = character >= 0 && character <= 127 ?
            static_cast<char>(character) : '?';
        length++;
    }
    text[length] = '\0';
    Com_WPrintf("Vulkan FSR3 SDK (%s): %s\n",
                type == FFX_MESSAGE_TYPE_ERROR ? "error" : "warning", text);
}

static void *q2_fsr3_vk_image_resource(VkImage image)
{
#if VK_USE_64_BIT_PTR_DEFINES
    return reinterpret_cast<void *>(image);
#else
    return reinterpret_cast<void *>(static_cast<uintptr_t>(image));
#endif
}

static FfxResource q2_fsr3_resource(
    VkImage image, VkFormat format, uint32_t width, uint32_t height,
    FfxResourceUsage usage, FfxResourceStates state)
{
    FfxResourceDescription description = {};
    description.type = FFX_RESOURCE_TYPE_TEXTURE2D;
    description.format = ffxGetSurfaceFormatVK(format);
    description.width = width;
    description.height = height;
    description.depth = 1;
    description.mipCount = 1;
    description.flags = FFX_RESOURCE_FLAGS_NONE;
    description.usage = usage;
    return ffxGetResourceVK(q2_fsr3_vk_image_resource(image), description,
                            nullptr, state);
}

static FfxErrorCode q2_fsr3_create_shared_resources(q2_fsr3_context_t *context)
{
    FfxFsr3UpscalerSharedResourceDescriptions descriptions = {};
    FfxErrorCode error = ffxFsr3UpscalerGetSharedResourceDescriptions(
        &context->context, &descriptions);
    if (error != FFX_OK)
        return error;

    FfxResourceInternal *resources[] = {
        &context->dilated_depth,
        &context->dilated_motion_vectors,
        &context->reconstructed_prev_nearest_depth,
    };
    const FfxCreateResourceDescription *create_descriptions[] = {
        &descriptions.dilatedDepth,
        &descriptions.dilatedMotionVectors,
        &descriptions.reconstructedPrevNearestDepth,
    };

    for (size_t i = 0; i < sizeof(resources) / sizeof(resources[0]); i++) {
        error = context->interface.fpCreateResource(
            &context->interface, create_descriptions[i], 0, resources[i]);
        if (error != FFX_OK)
            return error;
        context->shared_resources_count++;
    }
    return FFX_OK;
}

static void q2_fsr3_destroy_shared_resources(q2_fsr3_context_t *context)
{
    FfxResourceInternal *resources[] = {
        &context->dilated_depth,
        &context->dilated_motion_vectors,
        &context->reconstructed_prev_nearest_depth,
    };
    for (uint32_t i = 0; i < context->shared_resources_count; i++)
        context->interface.fpDestroyResource(&context->interface, *resources[i], 0);
    context->shared_resources_count = 0;
}

static bool q2_fsr3_has_shared_resources(const q2_fsr3_context_t *context)
{
    return context->shared_resources_count == 3;
}

extern "C" q2_fsr3_context_t *Q2_FSR3_Create(
    VkPhysicalDevice physical_device, VkDevice device,
    VkInstance instance,
    PFN_vkGetInstanceProcAddr get_instance_proc_addr,
    PFN_vkGetDeviceProcAddr get_device_proc_addr,
    uint32_t render_width, uint32_t render_height,
    uint32_t display_width, uint32_t display_height,
    VkFormat display_format, bool frame_generation,
    const q2_fsr3_capabilities_t *capabilities)
{
    if (!physical_device || !device || !instance || !get_instance_proc_addr ||
        !get_device_proc_addr ||
        !render_width || !render_height || !display_width || !display_height)
        return nullptr;

    q2_instance = instance;
    q2_enabled_capabilities = capabilities ? *capabilities : q2_fsr3_capabilities_t{};
    q2_get_instance_proc_addr = get_instance_proc_addr;
    q2_get_device_proc_addr = get_device_proc_addr;
    if (!q2_fsr3_has_instance_functions() ||
        !q2_fsr3_has_device_functions(device)) {
        Com_WPrintf("Vulkan FSR3 is missing required Vulkan entry points\n");
        q2_instance = VK_NULL_HANDLE;
        q2_get_instance_proc_addr = nullptr;
        q2_get_device_proc_addr = nullptr;
        return nullptr;
    }

    q2_fsr3_context_t *result = new q2_fsr3_context_t;
    q2_active_context = result;
    result->render_width = render_width;
    result->render_height = render_height;
    result->display_width = display_width;
    result->display_height = display_height;
    result->frame_generation = frame_generation;
    FfxFsr3UpscalerContextDescription description = {};
    FfxFsr3ContextDescription full_description = {};
    bool context_created = false;
    FfxErrorCode error = FFX_OK;
    result->device_context = {
        device, physical_device, q2_fsr3_get_device_proc
    };
    const size_t max_contexts = frame_generation ? FFX_FSR3_CONTEXT_COUNT : 1;
    result->scratch_size = ffxGetScratchMemorySizeVK(physical_device,
                                                     max_contexts);
    if (!result->scratch_size) {
        Com_WPrintf("Vulkan FSR3 returned an invalid backend scratch size\n");
        goto fail;
    }
    result->scratch = std::calloc(1, result->scratch_size);
    if (!result->scratch)
        goto fail;

    error = ffxGetInterfaceVK(&result->interface,
                              ffxGetDeviceVK(&result->device_context),
                              result->scratch, result->scratch_size,
                              max_contexts);
    if (error != FFX_OK) {
        Com_WPrintf("Vulkan FSR3 backend interface creation failed (error %d)\n",
                    static_cast<int>(error));
        goto fail;
    }

    /* Q2Pro supplies already-exposed LDR scene color. Auto exposure adds an
     * extra FSR pass. */
    result->create_pipeline = result->interface.fpCreatePipeline;
    result->interface.fpCreatePipeline = q2_fsr3_create_pipeline;
    /* Auto exposure adds an extra FSR pass and changes the result compared
     * with the former FSR2
     * path, which did not enable it. Keep the upscaling-only context as the
     * default path so enabling frame generation is the only mode that pays
     * for the additional optical-flow/interpolation resources. */
    if (!frame_generation) {
        description.flags = 0;
#if USE_DEBUG
        description.flags |= FFX_FSR3UPSCALER_ENABLE_DEBUG_CHECKING;
#endif
        description.maxRenderSize = { render_width, render_height };
        description.maxUpscaleSize = { display_width, display_height };
        description.fpMessage = q2_fsr3_message;
        description.backendInterface = result->interface;
        error = ffxFsr3UpscalerContextCreate(&result->context, &description);
        if (error != FFX_OK) {
            Com_WPrintf("Vulkan FSR3 context creation failed (error %d)\n",
                        static_cast<int>(error));
            goto fail;
        }
        context_created = true;

        error = q2_fsr3_create_shared_resources(result);
        if (error != FFX_OK) {
            Com_WPrintf("Vulkan FSR3 shared-resource creation failed (error %d)\n",
                        static_cast<int>(error));
            goto fail;
        }
    } else {
        result->shared_interface = result->interface;
        result->frame_interpolation_interface = result->interface;
        full_description.flags = 0;
#if USE_DEBUG
        full_description.flags |= FFX_FSR3_ENABLE_DEBUG_CHECKING;
#endif
        full_description.maxRenderSize = { render_width, render_height };
        full_description.maxUpscaleSize = { display_width, display_height };
        full_description.displaySize = { display_width, display_height };
        full_description.backendInterfaceSharedResources = result->shared_interface;
        full_description.backendInterfaceUpscaling = result->interface;
        full_description.backendInterfaceFrameInterpolation = result->frame_interpolation_interface;
        full_description.fpMessage = q2_fsr3_message;
        full_description.backBufferFormat = ffxGetSurfaceFormatVK(display_format);
        error = ffxFsr3ContextCreate(&result->full_context, &full_description);
        if (error != FFX_OK) {
            Com_WPrintf("Vulkan FSR3 frame-generation context creation failed (error %d)\n",
                        static_cast<int>(error));
            goto fail;
        }
        result->full_context_created = true;

        FfxFrameGenerationConfig config = {};
        config.frameGenerationEnabled = true;
        config.allowAsyncWorkloads = false;
        config.frameGenerationCallback = nullptr;
        error = ffxFsr3ConfigureFrameGeneration(&result->full_context, &config);
        if (error != FFX_OK) {
            Com_WPrintf("Vulkan FSR3 frame-generation configuration failed (error %d)\n",
                        static_cast<int>(error));
            goto fail;
        }
    }
    return result;

fail:
    if (result->shared_resources_count)
        q2_fsr3_destroy_shared_resources(result);
    if (result->full_context_created)
        ffxFsr3ContextDestroy(&result->full_context);
    else if (context_created)
        ffxFsr3UpscalerContextDestroy(&result->context);
    std::free(result->scratch);
    delete result;
    q2_active_context = nullptr;
    q2_instance = VK_NULL_HANDLE;
    q2_get_instance_proc_addr = nullptr;
    q2_get_device_proc_addr = nullptr;
    return nullptr;
}

extern "C" void Q2_FSR3_Destroy(q2_fsr3_context_t *context)
{
    if (!context)
        return;
    if (context->full_context_created)
        ffxFsr3ContextDestroy(&context->full_context);
    else {
        q2_fsr3_destroy_shared_resources(context);
        ffxFsr3UpscalerContextDestroy(&context->context);
    }
    std::free(context->scratch);
    if (context->profile_pool) {
        auto destroy = reinterpret_cast<PFN_vkDestroyQueryPool>(
            q2_get_device_proc_addr(context->device_context.vkDevice, "vkDestroyQueryPool"));
        destroy(context->device_context.vkDevice, context->profile_pool, nullptr);
    }
    delete context;
    q2_active_context = nullptr;
    q2_instance = VK_NULL_HANDLE;
    q2_get_instance_proc_addr = nullptr;
    q2_get_device_proc_addr = nullptr;
}

extern "C" bool Q2_FSR3_GetJitter(q2_fsr3_context_t *context, float *x, float *y)
{
    if (!context || !x || !y)
        return false;

    const int32_t phase_count = ffxFsr3GetJitterPhaseCount(
        static_cast<int32_t>(context->render_width),
        static_cast<int32_t>(context->display_width));
    if (phase_count <= 0)
        return false;

    const int32_t phase = static_cast<int32_t>(context->frame_index % phase_count);
    return ffxFsr3GetJitterOffset(x, y, phase, phase_count) == FFX_OK;
}

extern "C" int Q2_FSR3_GetLastError(const q2_fsr3_context_t *context)
{
    return context ? static_cast<int>(context->last_error) :
        static_cast<int>(FFX_ERROR_INVALID_POINTER);
}

extern "C" bool Q2_FSR3_Dispatch(
    q2_fsr3_context_t *context, VkCommandBuffer command_buffer,
    VkImage color, VkImageView color_view, VkFormat color_format,
    VkImage depth, VkImageView depth_view, VkFormat depth_format,
    VkImage motion, VkImageView motion_view, VkFormat motion_format,
    VkImage reactive, VkImageView reactive_view, VkFormat reactive_format,
    VkImage composition,
    VkImage output, VkImageView output_view, VkFormat output_format,
    float jitter_x, float jitter_y, float sharpness,
    float frame_time_ms, float vertical_fov_radians,
    float camera_near, float camera_far, bool reset)
{
    (void)color_view;
    (void)depth_view;
    (void)motion_view;
    (void)reactive_view;
    (void)output_view;
    if (!context)
        return false;
    if (!command_buffer || !color || !depth || !motion ||
        !output || (!context->full_context_created &&
                    !q2_fsr3_has_shared_resources(context))) {
        context->last_error = FFX_ERROR_INVALID_POINTER;
        return false;
    }

    FfxCommandList command_list = ffxGetCommandListVK(command_buffer);
    FfxResource color_resource = q2_fsr3_resource(
        color, color_format, context->render_width, context->render_height,
        FFX_RESOURCE_USAGE_READ_ONLY, FFX_RESOURCE_STATE_COMPUTE_READ);
    FfxResource depth_resource = q2_fsr3_resource(
        depth, depth_format, context->render_width, context->render_height,
        FFX_RESOURCE_USAGE_DEPTHTARGET, FFX_RESOURCE_STATE_COMPUTE_READ);
    FfxResource motion_resource = q2_fsr3_resource(
        motion, motion_format, context->render_width, context->render_height,
        FFX_RESOURCE_USAGE_READ_ONLY, FFX_RESOURCE_STATE_COMPUTE_READ);
    FfxResource reactive_resource = {};
    if (reactive) {
        reactive_resource = q2_fsr3_resource(
            reactive, reactive_format, context->render_width, context->render_height,
            FFX_RESOURCE_USAGE_READ_ONLY, FFX_RESOURCE_STATE_COMPUTE_READ);
    }
    FfxResource output_resource = q2_fsr3_resource(
        output, output_format, context->display_width, context->display_height,
        FFX_RESOURCE_USAGE_UAV, FFX_RESOURCE_STATE_UNORDERED_ACCESS);

    if (context->full_context_created) {
        FfxFsr3DispatchUpscaleDescription description = {};
        description.commandList = command_list;
        description.color = color_resource;
        description.depth = depth_resource;
        description.motionVectors = motion_resource;
        description.reactive = reactive_resource;
        if (composition)
            description.transparencyAndComposition = q2_fsr3_resource(
                composition, VK_FORMAT_R8_UNORM, context->render_width, context->render_height,
                FFX_RESOURCE_USAGE_READ_ONLY, FFX_RESOURCE_STATE_COMPUTE_READ);
        description.upscaleOutput = output_resource;
        description.jitterOffset = { jitter_x, jitter_y };
        description.motionVectorScale = {
            static_cast<float>(context->render_width),
            static_cast<float>(context->render_height)
        };
        description.renderSize = { context->render_width, context->render_height };
        description.upscaleSize = { context->display_width, context->display_height };
        description.enableSharpening = sharpness > 0.0f;
        description.sharpness = std::clamp(sharpness, 0.0f, 1.0f);
        description.frameTimeDelta = std::max(frame_time_ms, 1.0f);
        description.preExposure = 1.0f;
        description.reset = reset;
        description.cameraNear = camera_near;
        description.cameraFar = camera_far;
        description.cameraFovAngleVertical = vertical_fov_radians;
        description.viewSpaceToMetersFactor = 1.0f;
        description.frameID = context->frame_index;
        context->last_error = ffxFsr3ContextDispatchUpscale(
            &context->full_context, &description);
    } else {
        FfxFsr3UpscalerDispatchDescription description = {};
        description.commandList = command_list;
        description.color = color_resource;
        description.depth = depth_resource;
        description.motionVectors = motion_resource;
        description.reactive = reactive_resource;
        if (composition)
            description.transparencyAndComposition = q2_fsr3_resource(
                composition, VK_FORMAT_R8_UNORM, context->render_width, context->render_height,
                FFX_RESOURCE_USAGE_READ_ONLY, FFX_RESOURCE_STATE_COMPUTE_READ);
        description.dilatedDepth = context->interface.fpGetResource(
            &context->interface, context->dilated_depth);
        description.dilatedMotionVectors = context->interface.fpGetResource(
            &context->interface, context->dilated_motion_vectors);
        description.reconstructedPrevNearestDepth = context->interface.fpGetResource(
            &context->interface, context->reconstructed_prev_nearest_depth);
        description.output = output_resource;
        description.jitterOffset = { jitter_x, jitter_y };
        description.motionVectorScale = {
            static_cast<float>(context->render_width),
            static_cast<float>(context->render_height)
        };
        description.renderSize = { context->render_width, context->render_height };
        description.upscaleSize = { context->display_width, context->display_height };
        description.enableSharpening = sharpness > 0.0f;
        description.sharpness = std::clamp(sharpness, 0.0f, 1.0f);
        description.frameTimeDelta = std::max(frame_time_ms, 1.0f);
        description.preExposure = 1.0f;
        description.reset = reset;
        description.cameraNear = camera_near;
        description.cameraFar = camera_far;
        description.cameraFovAngleVertical = vertical_fov_radians;
        description.viewSpaceToMetersFactor = 1.0f;
        context->last_error = ffxFsr3UpscalerContextDispatch(
            &context->context, &description);
    }
    if (context->last_error != FFX_OK)
        return false;
    if (!context->full_context_created)
        context->frame_index++;
    return true;
}

extern "C" bool Q2_FSR3_PrepareFrameGeneration(
    q2_fsr3_context_t *context, VkCommandBuffer command_buffer,
    VkImage depth, VkFormat depth_format, VkImage motion, VkFormat motion_format,
    float jitter_x, float jitter_y, float frame_time_ms,
    float vertical_fov_radians, float camera_near, float camera_far)
{
    if (!context || !context->full_context_created || !command_buffer ||
        !depth || !motion)
        return false;

    FfxFsr3DispatchFrameGenerationPrepareDescription description = {};
    description.commandList = ffxGetCommandListVK(command_buffer);
    description.depth = q2_fsr3_resource(depth, depth_format,
                                         context->render_width,
                                         context->render_height,
                                         FFX_RESOURCE_USAGE_DEPTHTARGET,
                                         FFX_RESOURCE_STATE_COMPUTE_READ);
    description.motionVectors = q2_fsr3_resource(motion, motion_format,
                                                 context->render_width,
                                                 context->render_height,
                                                 FFX_RESOURCE_USAGE_READ_ONLY,
                                                 FFX_RESOURCE_STATE_COMPUTE_READ);
    description.jitterOffset = { jitter_x, jitter_y };
    description.motionVectorScale = {
        static_cast<float>(context->render_width),
        static_cast<float>(context->render_height)
    };
    description.renderSize = { context->render_width, context->render_height };
    description.frameTimeDelta = std::max(frame_time_ms, 1.0f);
    description.cameraNear = camera_near;
    description.cameraFar = camera_far;
    description.cameraFovAngleVertical = vertical_fov_radians;
    description.viewSpaceToMetersFactor = 1.0f;
    description.frameID = context->frame_index;
    const FfxErrorCode error = ffxFsr3ContextDispatchFrameGenerationPrepare(
        &context->full_context, &description);
    context->last_error = error;
    return error == FFX_OK;
}

extern "C" bool Q2_FSR3_DispatchFrameGeneration(
    q2_fsr3_context_t *context, VkCommandBuffer command_buffer,
    VkImage present, VkFormat present_format, VkImage output,
    VkFormat output_format, bool reset)
{
    if (!context || !context->full_context_created || !command_buffer ||
        !present || !output)
        return false;

    FfxFrameGenerationDispatchDescription description = {};
    description.commandList = ffxGetCommandListVK(command_buffer);
    description.presentColor = q2_fsr3_resource(
        present, present_format, context->display_width, context->display_height,
        FFX_RESOURCE_USAGE_READ_ONLY, FFX_RESOURCE_STATE_COMPUTE_READ);
    description.outputs[0] = q2_fsr3_resource(
        output, output_format, context->display_width, context->display_height,
        FFX_RESOURCE_USAGE_UAV, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    description.numInterpolatedFrames = 1;
    description.reset = reset;
    description.backBufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    description.minMaxLuminance[0] = 0.0f;
    description.minMaxLuminance[1] = 1000.0f;
    description.interpolationRect = { 0, 0,
                                      static_cast<int32_t>(context->display_width),
                                      static_cast<int32_t>(context->display_height) };
    description.frameID = context->frame_index;
    const FfxErrorCode error = ffxFsr3DispatchFrameGeneration(&description);
    context->last_error = error;
    if (error == FFX_OK)
        context->frame_index++;
    return error == FFX_OK;
}

extern "C" bool Q2_FSR3_FrameGenerationEnabled(
    const q2_fsr3_context_t *context)
{
    return context && context->full_context_created;
}

#endif
