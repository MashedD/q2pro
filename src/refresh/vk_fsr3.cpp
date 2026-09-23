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
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
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
    uint64_t next_frame_id = 0;
    uint64_t current_frame_id = 0;
    std::atomic<uint64_t> provider_dispatch_count{0};
    q2_fsr3_capabilities_t capabilities = {};
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    bool frame_started = false;
    bool frame_generation = false;
    bool frame_generation_failed = false;
    bool prepared_frame_valid = false;
    bool upscaled_frame_valid = false;
    uint64_t prepared_frame_id = 0;
    uint64_t upscaled_frame_id = 0;
    bool profile_supported = true;
    bool force_reset = false;
    bool full_context_initialized = false;
    bool full_context_created = false;
    FfxErrorCode last_error = FFX_OK;
};

struct q2_fsr3_provider {
    FfxSwapchain swapchain = nullptr;
    FfxSwapchainReplacementFunctions replacement = {};
    q2_fsr3_context_t *context = nullptr;
};

/* The SDK callback signatures do not carry an opaque context. Keep the
 * callback binding thread-local and scope it around every SDK call instead of
 * storing a process-global device/context pair. */
static thread_local q2_fsr3_context_t *q2_callback_context;

#if Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_COMPILED
/* The SDK's provider worker thread calls Vulkan entry points directly. Keep
 * its device context available when that thread is outside q2pro's normal
 * FSR3 callback scope. q2pro creates one provider per Vulkan device. */
static q2_fsr3_context_t *q2_provider_context;
#endif

static q2_fsr3_context_t *q2_fsr3_active_context(void)
{
    q2_fsr3_context_t *context = q2_callback_context;
#if Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_COMPILED
    if (!context)
        context = q2_provider_context;
#endif
    return context;
}

struct q2_fsr3_dispatch_inputs {
    float jitter_x;
    float jitter_y;
    float frame_time_ms;
    float vertical_fov_radians;
    float camera_near;
    float camera_far;
    bool reset;
};

static bool q2_fsr3_valid_dimensions(uint32_t width, uint32_t height)
{
    const uint32_t max_dimension = static_cast<uint32_t>(
        std::numeric_limits<int32_t>::max());
    return width && height && width <= max_dimension && height <= max_dimension;
}

static q2_fsr3_dispatch_inputs q2_fsr3_sanitize_inputs(
    float jitter_x, float jitter_y, float frame_time_ms,
    float vertical_fov_radians, float camera_near, float camera_far,
    bool reset)
{
    constexpr float default_frame_time_ms = 16.0f;
    constexpr float max_frame_time_ms = 1000.0f;
    constexpr float default_fov = 75.0f * 0.01745329251994329577f;
    constexpr float default_near = 2.0f;
    constexpr float default_far = 2048.0f;
    q2_fsr3_dispatch_inputs result = {
        jitter_x, jitter_y, frame_time_ms, vertical_fov_radians,
        camera_near, camera_far, reset
    };

    if (!std::isfinite(result.jitter_x)) {
        result.jitter_x = 0.0f;
        result.reset = true;
    }
    if (!std::isfinite(result.jitter_y)) {
        result.jitter_y = 0.0f;
        result.reset = true;
    }
    if (!std::isfinite(result.frame_time_ms) || result.frame_time_ms <= 0.0f) {
        result.frame_time_ms = default_frame_time_ms;
        result.reset = true;
    } else if (result.frame_time_ms > max_frame_time_ms) {
        result.frame_time_ms = max_frame_time_ms;
        result.reset = true;
    }
    if (!std::isfinite(result.vertical_fov_radians) ||
        result.vertical_fov_radians <= 0.0f ||
        result.vertical_fov_radians >= 3.14159265358979323846f) {
        result.vertical_fov_radians = default_fov;
        result.reset = true;
    }
    if (!std::isfinite(result.camera_near) || result.camera_near <= 0.0f) {
        result.camera_near = default_near;
        result.reset = true;
    }
    if (!std::isfinite(result.camera_far) ||
        result.camera_far <= result.camera_near) {
        result.camera_far = default_far;
        if (result.camera_far <= result.camera_near)
            result.camera_near = default_near;
        result.reset = true;
    }
    return result;
}

static float q2_fsr3_sanitize_sharpness(float sharpness)
{
    return std::isfinite(sharpness) ? std::clamp(sharpness, 0.0f, 1.0f) : 0.0f;
}

struct q2_fsr3_context_scope {
    q2_fsr3_context_t *previous;
    explicit q2_fsr3_context_scope(q2_fsr3_context_t *context)
        : previous(q2_callback_context)
    {
        q2_callback_context = context;
    }
    ~q2_fsr3_context_scope()
    {
        q2_callback_context = previous;
    }
};

static FfxErrorCode q2_fsr3_create_pipeline(FfxInterface *backend, FfxEffect effect,
    FfxPass pass, uint32_t permutation, const FfxPipelineDescription *description,
    FfxUInt32 id, FfxPipelineState *pipeline)
{
    auto *context = q2_callback_context;
    if (!context || !context->create_pipeline)
        return FFX_ERROR_INVALID_POINTER;
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
    auto *context = q2_callback_context;
    if (!context || !context->get_device_proc_addr)
        return;
    if (point == VK_PIPELINE_BIND_POINT_COMPUTE)
        context->profile_pipeline = pipeline;
    auto function = reinterpret_cast<PFN_vkCmdBindPipeline>(
        context->get_device_proc_addr(context->device_context.vkDevice, "vkCmdBindPipeline"));
    function(cmd, point, pipeline);
}

static VKAPI_ATTR void VKAPI_CALL q2_fsr3_dispatch(VkCommandBuffer cmd,
    uint32_t x, uint32_t y, uint32_t z)
{
    auto *context = q2_callback_context;
    if (!context || !context->get_device_proc_addr)
        return;
    auto dispatch = reinterpret_cast<PFN_vkCmdDispatch>(
        context->get_device_proc_addr(context->device_context.vkDevice, "vkCmdDispatch"));
    uint32_t slot = context->profile_slot, index = context->profile_count[slot];
    bool profile = context->profile_active && index < 64;
    auto stamp = reinterpret_cast<PFN_vkCmdWriteTimestamp>(
        context->get_device_proc_addr(context->device_context.vkDevice, "vkCmdWriteTimestamp"));
    if (profile && stamp) {
        context->profile_labels[slot][index] = context->pipeline_labels[context->profile_pipeline];
        stamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, context->profile_pool, slot * 128 + index * 2);
    }
    dispatch(cmd, x, y, z);
    if (profile && stamp) {
        stamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, context->profile_pool, slot * 128 + index * 2 + 1);
        context->profile_count[slot]++;
    }
}

extern "C" void Q2_FSR3_ProfileBegin(q2_fsr3_context_t *context, VkCommandBuffer cmd,
    uint32_t slot, bool enabled, float period)
{
    if (!context || slot >= 3)
        return;
    q2_fsr3_context_scope scope(context);
    context->profile_active = false;
    if (!context->get_device_proc_addr) {
        context->profile_supported = false;
        return;
    }
    VkDevice device = context->device_context.vkDevice;
    auto create = reinterpret_cast<PFN_vkCreateQueryPool>(
        context->get_device_proc_addr(device, "vkCreateQueryPool"));
    auto read = reinterpret_cast<PFN_vkGetQueryPoolResults>(
        context->get_device_proc_addr(device, "vkGetQueryPoolResults"));
    auto reset = reinterpret_cast<PFN_vkCmdResetQueryPool>(
        context->get_device_proc_addr(device, "vkCmdResetQueryPool"));
    auto stamp = reinterpret_cast<PFN_vkCmdWriteTimestamp>(
        context->get_device_proc_addr(device, "vkCmdWriteTimestamp"));
    if (!create || !read || !reset || !stamp) {
        if (context->profile_supported)
            Com_WPrintf("FSR per-pass profiling disabled: Vulkan timestamp/query functions unavailable\n");
        context->profile_supported = false;
        context->profile_count[slot] = 0;
        return;
    }
    context->profile_supported = true;
    if (!context->profile_pool && enabled) {
        VkQueryPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = 384;
        if (create(device, &info, nullptr, &context->profile_pool) != VK_SUCCESS) {
            Com_WPrintf("FSR per-pass timestamp allocation failed\n");
            return;
        }
    }
    if (!context->profile_pool)
        return;
    if (context->profile_count[slot]) {
        uint64_t values[128] = {};
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
        reset(cmd, context->profile_pool, slot * 128, 128);
        context->profile_active = true;
    }
}

static PFN_vkVoidFunction q2_fsr3_get_instance_proc(const char *name)
{
    auto *context = q2_fsr3_active_context();
    return context && context->get_instance_proc_addr && context->instance ?
        context->get_instance_proc_addr(context->instance, name) : nullptr;
}

static VKAPI_ATTR VkResult VKAPI_CALL q2_fsr3_create_compute_pipelines(
    VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkComputePipelineCreateInfo *infos, const VkAllocationCallbacks *allocator,
    VkPipeline *pipelines)
{
    auto *context = q2_callback_context;
    if (!context || !context->get_device_proc_addr)
        return VK_ERROR_INITIALIZATION_FAILED;
    auto function = reinterpret_cast<PFN_vkCreateComputePipelines>(
        context->get_device_proc_addr(device, "vkCreateComputePipelines"));
    if (!context->capabilities.subgroup_size)
        return function(device, cache, count, infos, allocator, pipelines);
    std::vector<VkComputePipelineCreateInfo> copies(infos, infos + count);
    std::vector<VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT> sizes(count);
    for (uint32_t i = 0; i < count; i++) {
        const auto *head = static_cast<const VkBaseInStructure *>(copies[i].stage.pNext);
        sizes[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT;
        sizes[i].pNext = head && head->sType == sizes[i].sType ? head->pNext : head;
        sizes[i].requiredSubgroupSize = context->capabilities.subgroup_size;
        copies[i].stage.pNext = &sizes[i];
    }
    return function(device, cache, count, copies.data(), allocator, pipelines);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL q2_fsr3_get_device_proc(
    VkDevice device, const char *name)
{
    auto *context = q2_fsr3_active_context();
    PFN_vkVoidFunction function = context && context->get_device_proc_addr && device ?
        context->get_device_proc_addr(device, name) : nullptr;
    if (function && !strcmp(name, "vkCreateComputePipelines"))
        return reinterpret_cast<PFN_vkVoidFunction>(q2_fsr3_create_compute_pipelines);
    if (function && !strcmp(name, "vkCmdBindPipeline"))
        return reinterpret_cast<PFN_vkVoidFunction>(q2_fsr3_bind_pipeline);
    if (function && !strcmp(name, "vkCmdDispatch"))
        return reinterpret_cast<PFN_vkVoidFunction>(q2_fsr3_dispatch);
    if (!function && !std::strcmp(name, "vkGetBufferMemoryRequirements2KHR"))
        function = context && context->get_device_proc_addr ?
            context->get_device_proc_addr(device, "vkGetBufferMemoryRequirements2") : nullptr;
    return function;
}

static const char *const q2_fsr3_instance_function_names[] = {
        "vkEnumerateDeviceExtensionProperties",
        "vkGetPhysicalDeviceProperties",
        "vkGetPhysicalDeviceProperties2",
        "vkGetPhysicalDeviceFeatures",
        "vkGetPhysicalDeviceFeatures2",
        "vkGetPhysicalDeviceMemoryProperties",
};

static const char *const q2_fsr3_device_function_names[] = {
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

static bool q2_fsr3_has_instance_functions(
    PFN_vkGetInstanceProcAddr get_instance_proc_addr, VkInstance instance,
    const char **missing_name)
{
    if (missing_name)
        *missing_name = nullptr;
    if (!get_instance_proc_addr || !instance)
        return false;
    for (const char *name : q2_fsr3_instance_function_names) {
        if (!get_instance_proc_addr(instance, name)) {
            if (missing_name)
                *missing_name = name;
            return false;
        }
    }
    return true;
}

static PFN_vkVoidFunction q2_fsr3_resolve_device_proc(
    PFN_vkGetDeviceProcAddr get_device_proc_addr, VkDevice device,
    const char *name)
{
    PFN_vkVoidFunction function = get_device_proc_addr && device ?
        get_device_proc_addr(device, name) : nullptr;
    if (!function && !std::strcmp(name, "vkGetBufferMemoryRequirements2KHR"))
        function = get_device_proc_addr && device ?
            get_device_proc_addr(device, "vkGetBufferMemoryRequirements2") : nullptr;
    return function;
}

static bool q2_fsr3_has_device_functions(
    PFN_vkGetDeviceProcAddr get_device_proc_addr, VkDevice device,
    const char **missing_name)
{
    if (missing_name)
        *missing_name = nullptr;

    if (!get_device_proc_addr || !device)
        return false;
    for (const char *name : q2_fsr3_device_function_names) {
        if (!q2_fsr3_resolve_device_proc(get_device_proc_addr, device, name)) {
            if (missing_name)
                *missing_name = name;
            return false;
        }
    }
    return true;
}

static bool q2_fsr3_has_instance_functions(void)
{
    const char *missing_name = nullptr;
    const bool available = q2_fsr3_has_instance_functions(
        q2_callback_context ? q2_callback_context->get_instance_proc_addr : nullptr,
        q2_callback_context ? q2_callback_context->instance : VK_NULL_HANDLE,
        &missing_name);
    if (!available && missing_name)
        Com_WPrintf("FSR3 unavailable: missing Vulkan instance function %s\n", missing_name);
    return available;
}

static bool q2_fsr3_has_device_functions(VkDevice device)
{
    const char *missing_name = nullptr;
    const bool available = q2_fsr3_has_device_functions(
        q2_callback_context ? q2_callback_context->get_device_proc_addr : nullptr,
        device, &missing_name);
    if (!available && missing_name)
        Com_WPrintf("FSR3 unavailable: missing Vulkan device function %s\n", missing_name);
    return available;
}

extern "C" q2_fsr3_preflight_result_t Q2_FSR3_Preflight(
    VkPhysicalDevice physical_device, VkDevice device,
    VkInstance instance,
    PFN_vkGetInstanceProcAddr get_instance_proc_addr,
    PFN_vkGetDeviceProcAddr get_device_proc_addr,
    uint32_t render_width, uint32_t render_height,
    uint32_t display_width, uint32_t display_height,
    VkFormat display_format, bool frame_generation,
    const q2_fsr3_capabilities_t *capabilities)
{
    (void)display_format;
    (void)frame_generation;

    q2_fsr3_preflight_result_t result = {};
    result.capabilities = capabilities ? *capabilities : q2_fsr3_capabilities_t{};
    result.reason = "ok";

    if (!physical_device || !device || !instance) {
        result.reason = "invalid Vulkan handle";
        return result;
    }
    if (!q2_fsr3_valid_dimensions(render_width, render_height) ||
        !q2_fsr3_valid_dimensions(display_width, display_height)) {
        result.reason = "invalid dimensions";
        return result;
    }
    if (!get_instance_proc_addr) {
        result.reason = "missing Vulkan instance proc address loader";
        return result;
    }
    if (!get_device_proc_addr) {
        result.reason = "missing Vulkan device proc address loader";
        return result;
    }

    const char *missing_name = nullptr;
    if (!q2_fsr3_has_instance_functions(
            get_instance_proc_addr, instance, &missing_name)) {
        result.reason = "missing Vulkan instance function";
        result.missing_function = missing_name;
        return result;
    }
    missing_name = nullptr;
    if (!q2_fsr3_has_device_functions(
            get_device_proc_addr, device, &missing_name)) {
        result.reason = "missing Vulkan device function";
        result.missing_function = missing_name;
        return result;
    }

    result.supported = true;
    return result;
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
    auto *context = q2_fsr3_active_context();
    auto function = context ?
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
        if ((!context->capabilities.fp16 &&
             !strcmp(name, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME)) ||
            (!context->capabilities.subgroup_size &&
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
    auto function = q2_fsr3_active_context() ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            q2_fsr3_get_instance_proc("vkGetPhysicalDeviceProperties")) : nullptr;
    if (function)
        function(device, properties);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice device, VkPhysicalDeviceProperties2 *properties)
{
    auto function = q2_fsr3_active_context() ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            q2_fsr3_get_instance_proc("vkGetPhysicalDeviceProperties2")) : nullptr;
    if (function)
        function(device, properties);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice device, VkPhysicalDeviceFeatures2 *features)
{
    auto function = q2_fsr3_active_context() ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            q2_fsr3_get_instance_proc("vkGetPhysicalDeviceFeatures2")) : nullptr;
    if (function)
        function(device, features);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice device, VkPhysicalDeviceFeatures *features)
{
    auto function = q2_fsr3_active_context() ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(
            q2_fsr3_get_instance_proc("vkGetPhysicalDeviceFeatures")) : nullptr;
    if (function)
        function(device, features);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice device, VkPhysicalDeviceMemoryProperties *properties)
{
    auto function = q2_fsr3_active_context() ?
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            q2_fsr3_get_instance_proc("vkGetPhysicalDeviceMemoryProperties")) : nullptr;
    if (function)
        function(device, properties);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice device, const VkBufferCreateInfo *create_info,
    const VkAllocationCallbacks *allocator, VkBuffer *buffer)
{
    auto function = q2_fsr3_active_context() ?
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
        !q2_fsr3_valid_dimensions(render_width, render_height) ||
        !q2_fsr3_valid_dimensions(display_width, display_height))
        return nullptr;

    q2_fsr3_context_t *result = new q2_fsr3_context_t;
    result->capabilities = capabilities ? *capabilities : q2_fsr3_capabilities_t{};
    result->get_instance_proc_addr = get_instance_proc_addr;
    result->get_device_proc_addr = get_device_proc_addr;
    result->instance = instance;
    result->device_context = { device, physical_device, q2_fsr3_get_device_proc };
    q2_fsr3_context_scope scope(result);
    if (!q2_fsr3_has_instance_functions() ||
        !q2_fsr3_has_device_functions(device)) {
        Com_WPrintf("Vulkan FSR3 is missing required Vulkan entry points\n");
        delete result;
        return nullptr;
    }

    result->render_width = render_width;
    result->render_height = render_height;
    result->display_width = display_width;
    result->display_height = display_height;
    result->frame_generation = frame_generation;
    FfxFsr3UpscalerContextDescription description = {};
    FfxFsr3ContextDescription full_description = {};
    bool context_created = false;
    FfxErrorCode error = FFX_OK;
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
        /* The provider may switch between graphics and async compute at
         * runtime. Create the full context with async support even when the
         * initial provider configuration disables it; otherwise a later
         * allowAsyncWorkloads=true configuration violates the SDK contract. */
        full_description.flags |= FFX_FSR3_ENABLE_ASYNC_WORKLOAD_SUPPORT;
        full_description.maxRenderSize = { render_width, render_height };
        full_description.maxUpscaleSize = { display_width, display_height };
        full_description.displaySize = { display_width, display_height };
        full_description.backendInterfaceSharedResources = result->shared_interface;
        full_description.backendInterfaceUpscaling = result->interface;
        full_description.backendInterfaceFrameInterpolation = result->frame_interpolation_interface;
        full_description.fpMessage = q2_fsr3_message;
        full_description.backBufferFormat = ffxGetSurfaceFormatVK(display_format);
        /* The SDK can allocate one of the composite contexts before a later
         * frame-generation component fails. Mark it initialized before the
         * call so the failure path runs the SDK's composite destructor. */
        result->full_context_initialized = true;
        error = ffxFsr3ContextCreate(&result->full_context, &full_description);
        if (error != FFX_OK) {
            Com_WPrintf("Vulkan FSR3 frame-generation context creation failed (error %d)\n",
                        static_cast<int>(error));
            goto fail;
        }
        result->full_context_created = true;

        FfxFrameGenerationConfig config = {};
#if Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_COMPILED
        /* The real provider swapchain does not exist until q2pro replaces the
         * native swapchain. Enable the FSR3 context after that replacement. */
        config.frameGenerationEnabled = false;
#else
        config.frameGenerationEnabled = true;
#endif
        config.allowAsyncWorkloads = false;
        config.frameGenerationCallback = nullptr;
        error = ffxFsr3ConfigureFrameGeneration(&result->full_context, &config);
#if !Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_COMPILED
        if (error != FFX_OK) {
            Com_WPrintf("Vulkan FSR3 frame-generation configuration failed (error %d)\n",
                        static_cast<int>(error));
            goto fail;
        }
#endif
    }
    return result;

fail:
    if (result->shared_resources_count)
        q2_fsr3_destroy_shared_resources(result);
    if (result->full_context_initialized)
        ffxFsr3ContextDestroy(&result->full_context);
    else if (context_created)
        ffxFsr3UpscalerContextDestroy(&result->context);
    std::free(result->scratch);
    delete result;
    return nullptr;
}

extern "C" void Q2_FSR3_Destroy(q2_fsr3_context_t *context)
{
    if (!context)
        return;
    q2_fsr3_context_scope scope(context);
    if (context->full_context_created)
        ffxFsr3ContextDestroy(&context->full_context);
    else {
        q2_fsr3_destroy_shared_resources(context);
        ffxFsr3UpscalerContextDestroy(&context->context);
    }
    std::free(context->scratch);
    if (context->profile_pool && context->get_device_proc_addr) {
        auto destroy = reinterpret_cast<PFN_vkDestroyQueryPool>(
            context->get_device_proc_addr(context->device_context.vkDevice, "vkDestroyQueryPool"));
        if (destroy)
            destroy(context->device_context.vkDevice, context->profile_pool, nullptr);
    }
    delete context;
}

extern "C" bool Q2_FSR3_GetJitter(q2_fsr3_context_t *context, float *x, float *y)
{
    if (!context || !context->frame_started || !x || !y)
        return false;

    const int32_t phase_count = ffxFsr3GetJitterPhaseCount(
        static_cast<int32_t>(context->render_width),
        static_cast<int32_t>(context->display_width));
    if (phase_count <= 0)
        return false;

    const int32_t phase = static_cast<int32_t>(context->current_frame_id % phase_count);
    if (ffxFsr3GetJitterOffset(x, y, phase, phase_count) != FFX_OK ||
        !std::isfinite(*x) || !std::isfinite(*y)) {
        *x = 0.0f;
        *y = 0.0f;
        return false;
    }
    return true;
}

extern "C" bool Q2_FSR3_GetJitterPhase(
    const q2_fsr3_context_t *context, uint32_t *phase, uint32_t *phase_count)
{
    if (!context || !context->frame_started || !phase || !phase_count)
        return false;

    const int32_t count = ffxFsr3GetJitterPhaseCount(
        static_cast<int32_t>(context->render_width),
        static_cast<int32_t>(context->display_width));
    if (count <= 0)
        return false;

    *phase_count = static_cast<uint32_t>(count);
    *phase = static_cast<uint32_t>(context->current_frame_id % count);
    return true;
}

extern "C" void Q2_FSR3_BeginFrame(q2_fsr3_context_t *context)
{
    if (!context)
        return;
    context->current_frame_id = context->next_frame_id++;
    context->frame_started = true;
    context->force_reset = false;
    context->prepared_frame_valid = false;
    context->upscaled_frame_valid = false;
}

extern "C" uint64_t Q2_FSR3_GetCurrentFrameId(
    const q2_fsr3_context_t *context)
{
    return context && context->frame_started ? context->current_frame_id : 0;
}

extern "C" uint64_t Q2_FSR3_GetProviderDispatchCount(
    const q2_fsr3_context_t *context)
{
    return context ? context->provider_dispatch_count.load(
                         std::memory_order_relaxed) : 0;
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
    if (!context || !context->frame_started)
        return false;
    /* A failed upscale must never leave the previous successful frame
     * eligible for the provider callback. */
    context->upscaled_frame_valid = false;
    q2_fsr3_context_scope scope(context);
    if (!command_buffer || !color || !depth || !motion ||
        !output || (!context->full_context_created &&
                    !q2_fsr3_has_shared_resources(context))) {
        context->last_error = FFX_ERROR_INVALID_POINTER;
        return false;
    }

    const q2_fsr3_dispatch_inputs inputs = q2_fsr3_sanitize_inputs(
        jitter_x, jitter_y, frame_time_ms, vertical_fov_radians,
        camera_near, camera_far, reset || context->force_reset);
    context->force_reset = inputs.reset;

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
        description.jitterOffset = { inputs.jitter_x, inputs.jitter_y };
        description.motionVectorScale = {
            static_cast<float>(context->render_width),
            static_cast<float>(context->render_height)
        };
        description.renderSize = { context->render_width, context->render_height };
        description.upscaleSize = { context->display_width, context->display_height };
        description.sharpness = q2_fsr3_sanitize_sharpness(sharpness);
        description.enableSharpening = description.sharpness > 0.0f;
        description.frameTimeDelta = std::max(inputs.frame_time_ms, 1.0f);
        description.preExposure = 1.0f;
        description.reset = inputs.reset;
        description.cameraNear = inputs.camera_near;
        description.cameraFar = inputs.camera_far;
        description.cameraFovAngleVertical = inputs.vertical_fov_radians;
        description.viewSpaceToMetersFactor = 1.0f;
        description.frameID = context->current_frame_id;
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
        description.jitterOffset = { inputs.jitter_x, inputs.jitter_y };
        description.motionVectorScale = {
            static_cast<float>(context->render_width),
            static_cast<float>(context->render_height)
        };
        description.renderSize = { context->render_width, context->render_height };
        description.upscaleSize = { context->display_width, context->display_height };
        description.sharpness = q2_fsr3_sanitize_sharpness(sharpness);
        description.enableSharpening = description.sharpness > 0.0f;
        description.frameTimeDelta = std::max(inputs.frame_time_ms, 1.0f);
        description.preExposure = 1.0f;
        description.reset = inputs.reset;
        description.cameraNear = inputs.camera_near;
        description.cameraFar = inputs.camera_far;
        description.cameraFovAngleVertical = inputs.vertical_fov_radians;
        description.viewSpaceToMetersFactor = 1.0f;
        context->last_error = ffxFsr3UpscalerContextDispatch(
            &context->context, &description);
    }
    if (context->last_error != FFX_OK)
        return false;
    context->upscaled_frame_id = context->current_frame_id;
    context->upscaled_frame_valid = true;
    return true;
}

extern "C" bool Q2_FSR3_PrepareFrameGeneration(
    q2_fsr3_context_t *context, VkCommandBuffer command_buffer,
    VkImage depth, VkFormat depth_format, VkImage motion, VkFormat motion_format,
    float jitter_x, float jitter_y, float frame_time_ms,
    float vertical_fov_radians, float camera_near, float camera_far)
{
    if (!context || !context->frame_started)
        return false;
    /* Preparation is a per-frame prerequisite. A retry starts with no
     * eligibility and records it only after the SDK accepts the work. */
    context->prepared_frame_valid = false;
    if (!context->frame_generation ||
        context->frame_generation_failed ||
        !context->full_context_created || !command_buffer ||
        !depth || !motion)
        return false;
    q2_fsr3_context_scope scope(context);

    const q2_fsr3_dispatch_inputs inputs = q2_fsr3_sanitize_inputs(
        jitter_x, jitter_y, frame_time_ms, vertical_fov_radians,
        camera_near, camera_far, context->force_reset);
    context->force_reset = inputs.reset;

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
    description.jitterOffset = { inputs.jitter_x, inputs.jitter_y };
    description.motionVectorScale = {
        static_cast<float>(context->render_width),
        static_cast<float>(context->render_height)
    };
    description.renderSize = { context->render_width, context->render_height };
    description.frameTimeDelta = std::max(inputs.frame_time_ms, 1.0f);
    description.cameraNear = inputs.camera_near;
    description.cameraFar = inputs.camera_far;
    description.cameraFovAngleVertical = inputs.vertical_fov_radians;
    description.viewSpaceToMetersFactor = 1.0f;
    description.frameID = context->current_frame_id;
    const FfxErrorCode error = ffxFsr3ContextDispatchFrameGenerationPrepare(
        &context->full_context, &description);
    context->last_error = error;
    if (error != FFX_OK)
        context->frame_generation_failed = true;
    else {
        context->prepared_frame_id = context->current_frame_id;
        context->prepared_frame_valid = true;
    }
    return error == FFX_OK;
}

extern "C" bool Q2_FSR3_DispatchFrameGeneration(
    q2_fsr3_context_t *context, VkCommandBuffer command_buffer,
    VkImage present, VkFormat present_format, VkImage output,
    VkFormat output_format, bool reset)
{
    if (!context || !context->frame_started)
        return false;
    /* A failed frame-generation dispatch also invalidates the source frame
     * for any provider callback that may still be pending. */
    context->upscaled_frame_valid = false;
    context->prepared_frame_valid = false;
    if (!context->frame_generation ||
        context->frame_generation_failed ||
        !context->full_context_created || !command_buffer ||
        !present || !output)
        return false;
    q2_fsr3_context_scope scope(context);

    FfxFrameGenerationDispatchDescription description = {};
    description.commandList = ffxGetCommandListVK(command_buffer);
    description.presentColor = q2_fsr3_resource(
        present, present_format, context->display_width, context->display_height,
        FFX_RESOURCE_USAGE_READ_ONLY, FFX_RESOURCE_STATE_COMPUTE_READ);
    description.outputs[0] = q2_fsr3_resource(
        output, output_format, context->display_width, context->display_height,
        FFX_RESOURCE_USAGE_UAV, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    description.numInterpolatedFrames = 1;
    description.reset = reset || context->force_reset;
    description.backBufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    description.minMaxLuminance[0] = 0.0f;
    description.minMaxLuminance[1] = 1000.0f;
    description.interpolationRect = { 0, 0,
                                      static_cast<int32_t>(context->display_width),
                                      static_cast<int32_t>(context->display_height) };
    description.frameID = context->current_frame_id;
    const FfxErrorCode error = ffxFsr3DispatchFrameGeneration(&description);
    context->last_error = error;
    if (error != FFX_OK)
        context->frame_generation_failed = true;
    return error == FFX_OK;
}

extern "C" bool Q2_FSR3_FrameGenerationEnabled(
    const q2_fsr3_context_t *context)
{
    return context && context->full_context_created &&
        !context->frame_generation_failed;
}

extern "C" bool Q2_FSR3_FrameGenerationFailed(
    const q2_fsr3_context_t *context)
{
    return context && context->frame_generation_failed;
}

#if Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_COMPILED

/* The SDK callback ABI does not carry a Vulkan queue handle. q2pro has one
 * provider per device, so keep the bound queue-submit table process-local and
 * clear it only after the provider has stopped its presentation threads. */
static PFN_vkQueueSubmit q2_provider_queue_submit;
static VkQueue q2_provider_game_queue;
static VkQueue q2_provider_async_queue;
static VkQueue q2_provider_present_queue;
static VkQueue q2_provider_image_acquire_queue;

static void q2_fsr3_provider_clear_binding(void)
{
    q2_provider_context = nullptr;
    q2_provider_queue_submit = nullptr;
    q2_provider_game_queue = VK_NULL_HANDLE;
    q2_provider_async_queue = VK_NULL_HANDLE;
    q2_provider_present_queue = VK_NULL_HANDLE;
    q2_provider_image_acquire_queue = VK_NULL_HANDLE;
}

static bool q2_fsr3_provider_frame_ready(
    const q2_fsr3_context_t *context, uint64_t frame_id)
{
    return context && context->frame_started && context->frame_generation &&
        context->full_context_created && !context->frame_generation_failed &&
        context->prepared_frame_valid && context->upscaled_frame_valid &&
        context->prepared_frame_id == frame_id &&
        context->upscaled_frame_id == frame_id &&
        context->current_frame_id == frame_id;
}

static q2_fsr3_context_t *q2_fsr3_provider_active_context()
{
    return q2_callback_context ? q2_callback_context : q2_provider_context;
}

static PFN_vkVoidFunction q2_fsr3_provider_get_device_proc(const char *name)
{
    q2_fsr3_context_t *context = q2_fsr3_provider_active_context();
    return context && context->get_device_proc_addr &&
            context->device_context.vkDevice ?
        context->get_device_proc_addr(context->device_context.vkDevice, name) :
        nullptr;
}

static PFN_vkVoidFunction q2_fsr3_provider_get_instance_proc(const char *name)
{
    q2_fsr3_context_t *context = q2_fsr3_provider_active_context();
    return context && context->get_instance_proc_addr && context->instance ?
        context->get_instance_proc_addr(context->instance, name) : nullptr;
}

#define Q2_FSR3_FORWARD_DEVICE_RESULT(name, params, args) \
    extern "C" VKAPI_ATTR VkResult VKAPI_CALL name params \
    { \
        auto function = reinterpret_cast<PFN_##name>( \
            q2_fsr3_provider_get_device_proc(#name)); \
        return function ? function args : VK_ERROR_INITIALIZATION_FAILED; \
    }

#define Q2_FSR3_FORWARD_DEVICE_VOID(name, params, args) \
    extern "C" VKAPI_ATTR void VKAPI_CALL name params \
    { \
        auto function = reinterpret_cast<PFN_##name>( \
            q2_fsr3_provider_get_device_proc(#name)); \
        if (function) \
            function args; \
    }

#define Q2_FSR3_FORWARD_INSTANCE_VOID(name, params, args) \
    extern "C" VKAPI_ATTR void VKAPI_CALL name params \
    { \
        auto function = reinterpret_cast<PFN_##name>( \
            q2_fsr3_provider_get_instance_proc(#name)); \
        if (function) \
            function args; \
    }

#define Q2_FSR3_FORWARD_INSTANCE_RESULT(name, params, args) \
    extern "C" VKAPI_ATTR VkResult VKAPI_CALL name params \
    { \
        auto function = reinterpret_cast<PFN_##name>( \
            q2_fsr3_provider_get_instance_proc(#name)); \
        return function ? function args : VK_ERROR_INITIALIZATION_FAILED; \
    }

Q2_FSR3_FORWARD_DEVICE_RESULT(vkAcquireNextImageKHR,
    (VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
     VkSemaphore semaphore, VkFence fence, uint32_t *image_index),
    (device, swapchain, timeout, semaphore, fence, image_index))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkAllocateCommandBuffers,
    (VkDevice device, const VkCommandBufferAllocateInfo *allocate_info,
     VkCommandBuffer *command_buffers),
    (device, allocate_info, command_buffers))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkAllocateDescriptorSets,
    (VkDevice device, const VkDescriptorSetAllocateInfo *allocate_info,
     VkDescriptorSet *descriptor_sets),
    (device, allocate_info, descriptor_sets))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkAllocateMemory,
    (VkDevice device, const VkMemoryAllocateInfo *allocate_info,
     const VkAllocationCallbacks *allocator, VkDeviceMemory *memory),
    (device, allocate_info, allocator, memory))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkBeginCommandBuffer,
    (VkCommandBuffer command_buffer,
     const VkCommandBufferBeginInfo *begin_info),
    (command_buffer, begin_info))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkBindImageMemory,
    (VkDevice device, VkImage image, VkDeviceMemory memory,
     VkDeviceSize memory_offset),
    (device, image, memory, memory_offset))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkCreateCommandPool,
    (VkDevice device, const VkCommandPoolCreateInfo *create_info,
     const VkAllocationCallbacks *allocator, VkCommandPool *command_pool),
    (device, create_info, allocator, command_pool))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkCreateDescriptorPool,
    (VkDevice device, const VkDescriptorPoolCreateInfo *create_info,
     const VkAllocationCallbacks *allocator, VkDescriptorPool *descriptor_pool),
    (device, create_info, allocator, descriptor_pool))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkCreateDescriptorSetLayout,
    (VkDevice device, const VkDescriptorSetLayoutCreateInfo *create_info,
     const VkAllocationCallbacks *allocator,
     VkDescriptorSetLayout *set_layout),
    (device, create_info, allocator, set_layout))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkCreateFramebuffer,
    (VkDevice device, const VkFramebufferCreateInfo *create_info,
     const VkAllocationCallbacks *allocator, VkFramebuffer *framebuffer),
    (device, create_info, allocator, framebuffer))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkCreateGraphicsPipelines,
    (VkDevice device, VkPipelineCache pipeline_cache, uint32_t count,
     const VkGraphicsPipelineCreateInfo *create_infos,
     const VkAllocationCallbacks *allocator, VkPipeline *pipelines),
    (device, pipeline_cache, count, create_infos, allocator, pipelines))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkCreateImage,
    (VkDevice device, const VkImageCreateInfo *create_info,
     const VkAllocationCallbacks *allocator, VkImage *image),
    (device, create_info, allocator, image))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkCreateImageView,
    (VkDevice device, const VkImageViewCreateInfo *create_info,
     const VkAllocationCallbacks *allocator, VkImageView *view),
    (device, create_info, allocator, view))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkCreatePipelineLayout,
    (VkDevice device, const VkPipelineLayoutCreateInfo *create_info,
     const VkAllocationCallbacks *allocator, VkPipelineLayout *layout),
    (device, create_info, allocator, layout))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkCreateRenderPass,
    (VkDevice device, const VkRenderPassCreateInfo *create_info,
     const VkAllocationCallbacks *allocator, VkRenderPass *render_pass),
    (device, create_info, allocator, render_pass))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkCreateSemaphore,
    (VkDevice device, const VkSemaphoreCreateInfo *create_info,
     const VkAllocationCallbacks *allocator, VkSemaphore *semaphore),
    (device, create_info, allocator, semaphore))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkCreateShaderModule,
    (VkDevice device, const VkShaderModuleCreateInfo *create_info,
     const VkAllocationCallbacks *allocator, VkShaderModule *shader_module),
    (device, create_info, allocator, shader_module))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkCreateSwapchainKHR,
    (VkDevice device, const VkSwapchainCreateInfoKHR *create_info,
     const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain),
    (device, create_info, allocator, swapchain))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkDeviceWaitIdle,
    (VkDevice device), (device))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkFreeDescriptorSets,
    (VkDevice device, VkDescriptorPool descriptor_pool, uint32_t count,
     const VkDescriptorSet *descriptor_sets),
    (device, descriptor_pool, count, descriptor_sets))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkGetSemaphoreCounterValue,
    (VkDevice device, VkSemaphore semaphore, uint64_t *value),
    (device, semaphore, value))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkGetSwapchainImagesKHR,
    (VkDevice device, VkSwapchainKHR swapchain, uint32_t *count,
     VkImage *images),
    (device, swapchain, count, images))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkQueuePresentKHR,
    (VkQueue queue, const VkPresentInfoKHR *present_info),
    (queue, present_info))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkQueueSubmit,
    (VkQueue queue, uint32_t submit_count, const VkSubmitInfo *submits,
     VkFence fence),
    (queue, submit_count, submits, fence))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkQueueWaitIdle,
    (VkQueue queue), (queue))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkResetCommandBuffer,
    (VkCommandBuffer command_buffer, VkCommandBufferResetFlags flags),
    (command_buffer, flags))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkResetCommandPool,
    (VkDevice device, VkCommandPool command_pool,
     VkCommandPoolResetFlags flags),
    (device, command_pool, flags))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkWaitSemaphores,
    (VkDevice device, const VkSemaphoreWaitInfo *wait_info,
     uint64_t timeout),
    (device, wait_info, timeout))

Q2_FSR3_FORWARD_DEVICE_VOID(vkDestroyCommandPool,
    (VkDevice device, VkCommandPool command_pool,
     const VkAllocationCallbacks *allocator),
    (device, command_pool, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkDestroyDescriptorPool,
    (VkDevice device, VkDescriptorPool descriptor_pool,
     const VkAllocationCallbacks *allocator),
    (device, descriptor_pool, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkDestroyDescriptorSetLayout,
    (VkDevice device, VkDescriptorSetLayout set_layout,
     const VkAllocationCallbacks *allocator),
    (device, set_layout, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkDestroyFramebuffer,
    (VkDevice device, VkFramebuffer framebuffer,
     const VkAllocationCallbacks *allocator),
    (device, framebuffer, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkDestroyImage,
    (VkDevice device, VkImage image, const VkAllocationCallbacks *allocator),
    (device, image, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkDestroyImageView,
    (VkDevice device, VkImageView view, const VkAllocationCallbacks *allocator),
    (device, view, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkDestroyPipeline,
    (VkDevice device, VkPipeline pipeline,
     const VkAllocationCallbacks *allocator),
    (device, pipeline, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkDestroyPipelineLayout,
    (VkDevice device, VkPipelineLayout layout,
     const VkAllocationCallbacks *allocator),
    (device, layout, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkDestroyRenderPass,
    (VkDevice device, VkRenderPass render_pass,
     const VkAllocationCallbacks *allocator),
    (device, render_pass, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkDestroySemaphore,
    (VkDevice device, VkSemaphore semaphore,
     const VkAllocationCallbacks *allocator),
    (device, semaphore, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkDestroyShaderModule,
    (VkDevice device, VkShaderModule shader_module,
     const VkAllocationCallbacks *allocator),
    (device, shader_module, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkDestroySwapchainKHR,
    (VkDevice device, VkSwapchainKHR swapchain,
     const VkAllocationCallbacks *allocator),
    (device, swapchain, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkFreeCommandBuffers,
    (VkDevice device, VkCommandPool command_pool, uint32_t count,
     const VkCommandBuffer *command_buffers),
    (device, command_pool, count, command_buffers))
Q2_FSR3_FORWARD_DEVICE_VOID(vkFreeMemory,
    (VkDevice device, VkDeviceMemory memory,
     const VkAllocationCallbacks *allocator),
    (device, memory, allocator))
Q2_FSR3_FORWARD_DEVICE_VOID(vkGetImageMemoryRequirements,
    (VkDevice device, VkImage image, VkMemoryRequirements *memory_requirements),
    (device, image, memory_requirements))
Q2_FSR3_FORWARD_DEVICE_VOID(vkUpdateDescriptorSets,
    (VkDevice device, uint32_t descriptor_write_count,
     const VkWriteDescriptorSet *descriptor_writes,
     uint32_t descriptor_copy_count,
     const VkCopyDescriptorSet *descriptor_copies),
    (device, descriptor_write_count, descriptor_writes,
     descriptor_copy_count, descriptor_copies))
Q2_FSR3_FORWARD_DEVICE_VOID(vkCmdBeginRenderPass,
    (VkCommandBuffer command_buffer, const VkRenderPassBeginInfo *render_pass_begin,
     VkSubpassContents contents),
    (command_buffer, render_pass_begin, contents))
Q2_FSR3_FORWARD_DEVICE_VOID(vkCmdBindDescriptorSets,
    (VkCommandBuffer command_buffer, VkPipelineBindPoint pipeline_bind_point,
     VkPipelineLayout layout, uint32_t first_set, uint32_t descriptor_set_count,
     const VkDescriptorSet *descriptor_sets, uint32_t dynamic_offset_count,
     const uint32_t *dynamic_offsets),
    (command_buffer, pipeline_bind_point, layout, first_set,
     descriptor_set_count, descriptor_sets, dynamic_offset_count,
     dynamic_offsets))
Q2_FSR3_FORWARD_DEVICE_VOID(vkCmdBindPipeline,
    (VkCommandBuffer command_buffer, VkPipelineBindPoint pipeline_bind_point,
     VkPipeline pipeline),
    (command_buffer, pipeline_bind_point, pipeline))
Q2_FSR3_FORWARD_DEVICE_VOID(vkCmdCopyImage,
    (VkCommandBuffer command_buffer, VkImage src_image,
     VkImageLayout src_image_layout, VkImage dst_image,
     VkImageLayout dst_image_layout, uint32_t region_count,
     const VkImageCopy *regions),
    (command_buffer, src_image, src_image_layout, dst_image,
     dst_image_layout, region_count, regions))
Q2_FSR3_FORWARD_DEVICE_VOID(vkCmdDraw,
    (VkCommandBuffer command_buffer, uint32_t vertex_count,
     uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance),
    (command_buffer, vertex_count, instance_count, first_vertex, first_instance))
Q2_FSR3_FORWARD_DEVICE_VOID(vkCmdEndRenderPass,
    (VkCommandBuffer command_buffer), (command_buffer))
Q2_FSR3_FORWARD_DEVICE_VOID(vkCmdPipelineBarrier,
    (VkCommandBuffer command_buffer, VkPipelineStageFlags src_stage_mask,
     VkPipelineStageFlags dst_stage_mask, VkDependencyFlags dependency_flags,
     uint32_t memory_barrier_count, const VkMemoryBarrier *memory_barriers,
     uint32_t buffer_memory_barrier_count,
     const VkBufferMemoryBarrier *buffer_memory_barriers,
     uint32_t image_memory_barrier_count,
     const VkImageMemoryBarrier *image_memory_barriers),
    (command_buffer, src_stage_mask, dst_stage_mask, dependency_flags,
     memory_barrier_count, memory_barriers, buffer_memory_barrier_count,
     buffer_memory_barriers, image_memory_barrier_count,
     image_memory_barriers))
Q2_FSR3_FORWARD_DEVICE_VOID(vkCmdPushConstants,
    (VkCommandBuffer command_buffer, VkPipelineLayout layout,
     VkShaderStageFlags stage_flags, uint32_t offset, uint32_t size,
     const void *values),
    (command_buffer, layout, stage_flags, offset, size, values))
Q2_FSR3_FORWARD_DEVICE_VOID(vkCmdSetScissor,
    (VkCommandBuffer command_buffer, uint32_t first_scissor,
     uint32_t scissor_count, const VkRect2D *scissors),
    (command_buffer, first_scissor, scissor_count, scissors))
Q2_FSR3_FORWARD_DEVICE_VOID(vkCmdSetViewport,
    (VkCommandBuffer command_buffer, uint32_t first_viewport,
     uint32_t viewport_count, const VkViewport *viewports),
    (command_buffer, first_viewport, viewport_count, viewports))
Q2_FSR3_FORWARD_DEVICE_RESULT(vkEndCommandBuffer,
    (VkCommandBuffer command_buffer), (command_buffer))

Q2_FSR3_FORWARD_INSTANCE_VOID(vkGetPhysicalDeviceQueueFamilyProperties,
    (VkPhysicalDevice physical_device, uint32_t *property_count,
     VkQueueFamilyProperties *properties),
    (physical_device, property_count, properties))
Q2_FSR3_FORWARD_INSTANCE_RESULT(vkGetPhysicalDeviceSurfaceSupportKHR,
    (VkPhysicalDevice physical_device, uint32_t queue_family_index,
     VkSurfaceKHR surface, VkBool32 *supported),
    (physical_device, queue_family_index, surface, supported))

#undef Q2_FSR3_FORWARD_DEVICE_RESULT
#undef Q2_FSR3_FORWARD_DEVICE_VOID
#undef Q2_FSR3_FORWARD_INSTANCE_VOID
#undef Q2_FSR3_FORWARD_INSTANCE_RESULT

static VkResult q2_provider_submit(PFN_vkQueueSubmit submit,
                                   VkQueue queue, uint32_t submit_count,
                                   const VkSubmitInfo *submits, VkFence fence)
{
    return submit && queue ? submit(queue, submit_count, submits, fence) :
        VK_ERROR_INITIALIZATION_FAILED;
}

static VkResult q2_provider_submit_game(uint32_t count,
                                        const VkSubmitInfo *submits,
                                        VkFence fence)
{
    return q2_provider_submit(q2_provider_queue_submit,
                              q2_provider_game_queue, count, submits, fence);
}

static VkResult q2_provider_submit_async(uint32_t count,
                                         const VkSubmitInfo *submits,
                                         VkFence fence)
{
    return q2_provider_submit(q2_provider_queue_submit,
                              q2_provider_async_queue, count, submits, fence);
}

static VkResult q2_provider_submit_present(uint32_t count,
                                           const VkSubmitInfo *submits,
                                           VkFence fence)
{
    return q2_provider_submit(q2_provider_queue_submit,
                              q2_provider_present_queue, count, submits, fence);
}

static VkResult q2_provider_submit_image_acquire(uint32_t count,
                                                 const VkSubmitInfo *submits,
                                                 VkFence fence)
{
    return q2_provider_submit(q2_provider_queue_submit,
                              q2_provider_image_acquire_queue,
                              count, submits, fence);
}

static FfxErrorCode q2_provider_frame_generation_callback(
    const FfxFrameGenerationDispatchDescription *description, void *userdata)
{
    q2_fsr3_context_t *context = static_cast<q2_fsr3_context_t *>(userdata);
    if (!description || !q2_fsr3_provider_frame_ready(
            context, description ? description->frameID : 0))
        return FFX_ERROR_INVALID_POINTER;

    q2_fsr3_context_scope scope(context);
    const FfxErrorCode error = ffxFsr3DispatchFrameGeneration(description);
    context->last_error = error;
    if (error == FFX_OK)
        context->provider_dispatch_count.fetch_add(1,
                                                   std::memory_order_relaxed);
    else {
        context->frame_generation_failed = true;
        context->prepared_frame_valid = false;
        context->upscaled_frame_valid = false;
    }
    return error;
}

extern "C" q2_fsr3_provider_t *Q2_FSR3_CreateProvider(
    q2_fsr3_context_t *context,
    VkSwapchainKHR native_swapchain,
    const VkSwapchainCreateInfoKHR *create_info,
    VkQueue game_queue, uint32_t game_queue_family,
    VkQueue async_compute_queue, uint32_t async_compute_queue_family,
    VkQueue present_queue, uint32_t present_queue_family,
    VkQueue image_acquire_queue, uint32_t image_acquire_queue_family,
    VkSwapchainKHR *provider_swapchain,
    q2_fsr3_provider_functions_t *functions,
    const char **failure_reason)
{
    static const char *invalid_argument = "invalid provider argument";
    static const char *missing_submit = "vkQueueSubmit is unavailable";
    static const char *replacement_failed = "FSR3 swapchain replacement failed";
    static const char *function_query_failed =
        "FSR3 swapchain replacement functions unavailable";

    if (provider_swapchain)
        *provider_swapchain = VK_NULL_HANDLE;
    if (functions)
        *functions = {};
    if (failure_reason)
        *failure_reason = nullptr;

    if (!context || !context->device_context.vkDevice || !native_swapchain ||
        !create_info || !game_queue || !present_queue ||
        !image_acquire_queue || !provider_swapchain || !functions) {
        if (failure_reason)
            *failure_reason = invalid_argument;
        return nullptr;
    }

    q2_fsr3_context_scope scope(context);
    auto submit = reinterpret_cast<PFN_vkQueueSubmit>(
        context->get_device_proc_addr(context->device_context.vkDevice,
                                      "vkQueueSubmit"));
    if (!submit) {
        if (failure_reason)
            *failure_reason = missing_submit;
        return nullptr;
    }

    FfxDevice device = ffxGetDeviceVK(&context->device_context);
    q2_fsr3_provider_t *provider = new q2_fsr3_provider;
    provider->context = context;
    if (ffxGetSwapchainReplacementFunctionsVK(device,
                                               &provider->replacement) != FFX_OK) {
        delete provider;
        if (failure_reason)
            *failure_reason = function_query_failed;
        return nullptr;
    }

    q2_provider_queue_submit = submit;
    q2_provider_game_queue = game_queue;
    q2_provider_async_queue = async_compute_queue;
    q2_provider_present_queue = present_queue;
    q2_provider_image_acquire_queue = image_acquire_queue;

    VkFrameInterpolationInfoFFX info = {};
    info.physicalDevice = context->device_context.vkPhysicalDevice;
    info.device = context->device_context.vkDevice;
    info.gameQueue = {
        game_queue, game_queue_family, q2_provider_submit_game
    };
    info.asyncComputeQueue = {
        async_compute_queue, async_compute_queue_family,
        async_compute_queue ? q2_provider_submit_async : nullptr
    };
    info.presentQueue = {
        present_queue, present_queue_family, q2_provider_submit_present
    };
    info.imageAcquireQueue = {
        image_acquire_queue, image_acquire_queue_family,
        q2_provider_submit_image_acquire
    };
    info.compositionMode = VK_COMPOSITION_MODE_NOT_FORCED_FFX;

    provider->swapchain = ffxGetSwapchainVK(native_swapchain);
    q2_provider_context = context;
    const FfxErrorCode error = ffxReplaceSwapchainForFrameinterpolationVK(
        ffxGetCommandQueueVK(game_queue), provider->swapchain,
        create_info, &info);
    if (error != FFX_OK || !provider->swapchain) {
        q2_fsr3_provider_clear_binding();
        delete provider;
        if (failure_reason)
            *failure_reason = replacement_failed;
        return nullptr;
    }

    *provider_swapchain = ffxGetVKSwapchain(provider->swapchain);
    *functions = {
        provider->replacement.destroySwapchainKHR,
        provider->replacement.getSwapchainImagesKHR,
        provider->replacement.acquireNextImageKHR,
        provider->replacement.queuePresentKHR,
        provider->replacement.setHdrMetadataEXT,
    };
    if (!functions->destroy_swapchain || !functions->get_swapchain_images ||
        !functions->acquire_next_image || !functions->queue_present) {
        provider->replacement.destroySwapchainKHR(
            context->device_context.vkDevice, *provider_swapchain, nullptr);
        *provider_swapchain = VK_NULL_HANDLE;
        q2_fsr3_provider_clear_binding();
        delete provider;
        if (failure_reason)
            *failure_reason = function_query_failed;
        return nullptr;
    }
    return provider;
}

extern "C" bool Q2_FSR3_ConfigureProvider(
    q2_fsr3_context_t *context, q2_fsr3_provider_t *provider,
    bool enabled, bool allow_async_workloads, uint64_t frame_id)
{
    if (!context || !provider || provider->context != context ||
        !provider->swapchain || !context->full_context_created)
        return false;

    /* The provider's queue-present callback can run after this call. When
     * enabling it, bind that callback to the exact frame whose prepare and
     * upscale both completed successfully. */
    if (enabled && !q2_fsr3_provider_frame_ready(context, frame_id)) {
        context->last_error = FFX_ERROR_INVALID_POINTER;
        return false;
    }

    q2_fsr3_context_scope scope(context);
    FfxFrameGenerationConfig config = {};
    config.swapChain = provider->swapchain;
    config.frameGenerationEnabled = enabled;
    config.allowAsyncWorkloads = allow_async_workloads;
    config.frameGenerationCallback = enabled ?
        q2_provider_frame_generation_callback : nullptr;
    config.frameGenerationCallbackContext = enabled ? context : nullptr;
    config.frameID = frame_id;
    config.interpolationRect = {
        0, 0, static_cast<int32_t>(context->display_width),
        static_cast<int32_t>(context->display_height)
    };
    const FfxErrorCode error = ffxFsr3ConfigureFrameGeneration(
        &context->full_context, &config);
    context->last_error = error;
    /* Configuration failures are recoverable state changes (for example,
     * pausing the provider or switching queues). Keep the fatal latch for
     * failed prepare/dispatch/callback work only, so a later configuration
     * attempt can recover without recreating the FSR3 context. */
    return error == FFX_OK;
}

extern "C" bool Q2_FSR3_WaitProvider(q2_fsr3_context_t *context,
                                      q2_fsr3_provider_t *provider)
{
    if (!context || !provider || provider->context != context ||
        !provider->swapchain)
        return false;
    q2_fsr3_context_scope scope(context);
    const FfxErrorCode error = ffxWaitForPresents(provider->swapchain);
    context->last_error = error;
    return error == FFX_OK;
}

extern "C" void Q2_FSR3_DestroyProvider(q2_fsr3_context_t *context,
                                         q2_fsr3_provider_t *provider,
                                         bool device_lost)
{
    if (!provider)
        return;
    if (!device_lost && context && provider->context == context) {
        q2_fsr3_context_scope scope(context);
        ffxWaitForPresents(provider->swapchain);
        provider->replacement.destroySwapchainKHR(
            context->device_context.vkDevice,
            ffxGetVKSwapchain(provider->swapchain), nullptr);
    }
    if (q2_provider_context == context || q2_provider_game_queue ||
        q2_provider_present_queue || q2_provider_image_acquire_queue)
        q2_fsr3_provider_clear_binding();
    delete provider;
}

#else

extern "C" q2_fsr3_provider_t *Q2_FSR3_CreateProvider(
    q2_fsr3_context_t *, VkSwapchainKHR, const VkSwapchainCreateInfoKHR *,
    VkQueue, uint32_t, VkQueue, uint32_t, VkQueue, uint32_t,
    VkQueue, uint32_t, VkSwapchainKHR *, q2_fsr3_provider_functions_t *,
    const char **)
{
    return nullptr;
}

extern "C" bool Q2_FSR3_ConfigureProvider(
    q2_fsr3_context_t *, q2_fsr3_provider_t *, bool, bool, uint64_t)
{
    return false;
}

extern "C" bool Q2_FSR3_WaitProvider(q2_fsr3_context_t *,
                                      q2_fsr3_provider_t *)
{
    return false;
}

extern "C" void Q2_FSR3_DestroyProvider(q2_fsr3_context_t *,
                                         q2_fsr3_provider_t *, bool)
{
}

#endif

#endif
