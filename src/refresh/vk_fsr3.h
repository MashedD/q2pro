/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "config.h"
#include <vulkan/vulkan.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#if USE_VULKAN
#ifdef __cplusplus
extern "C" {
#endif
typedef struct q2_fsr3_context q2_fsr3_context_t;
typedef struct q2_fsr3_provider q2_fsr3_provider_t;

typedef struct {
    PFN_vkDestroySwapchainKHR destroy_swapchain;
    PFN_vkGetSwapchainImagesKHR get_swapchain_images;
    PFN_vkAcquireNextImageKHR acquire_next_image;
    PFN_vkQueuePresentKHR queue_present;
    PFN_vkSetHdrMetadataEXT set_hdr_metadata;
} q2_fsr3_provider_functions_t;
typedef struct {
    bool fp16;
    uint32_t subgroup_size; /* 0 = native; otherwise enabled required size */
} q2_fsr3_capabilities_t;

/* The reason is a stable, read-only string literal owned by the library. */
typedef struct {
    bool supported;
    const char *reason;
    const char *missing_function;
    q2_fsr3_capabilities_t capabilities;
} q2_fsr3_preflight_result_t;

/* Performs only local handle/dimension/proc-address validation. It does not
 * create an SDK context, dispatch work, or query a swapchain provider. */
q2_fsr3_preflight_result_t Q2_FSR3_Preflight(
    VkPhysicalDevice physical_device, VkDevice device, VkInstance instance,
    PFN_vkGetInstanceProcAddr get_instance_proc_addr,
    PFN_vkGetDeviceProcAddr get_device_proc_addr,
    uint32_t render_width, uint32_t render_height,
    uint32_t display_width, uint32_t display_height,
    VkFormat display_format, bool frame_generation,
    const q2_fsr3_capabilities_t *capabilities);

q2_fsr3_context_t *Q2_FSR3_Create(VkPhysicalDevice physical_device,
                                   VkDevice device,
                                   VkInstance instance,
                                   PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                                   PFN_vkGetDeviceProcAddr get_device_proc_addr,
                                   uint32_t render_width,
                                   uint32_t render_height,
                                   uint32_t display_width,
                                   uint32_t display_height,
                                   VkFormat display_format,
                                   bool frame_generation,
                                   const q2_fsr3_capabilities_t *capabilities);
void Q2_FSR3_Destroy(q2_fsr3_context_t *context);
/* Captures one logical renderer frame. All subsequent dispatches use its ID. */
void Q2_FSR3_BeginFrame(q2_fsr3_context_t *context);
/* Call only after the fence for slot has completed; timestamps are opt-in. */
void Q2_FSR3_ProfileBegin(q2_fsr3_context_t *context, VkCommandBuffer cmd,
                         uint32_t slot, bool enabled, float timestamp_period);
bool Q2_FSR3_GetJitter(q2_fsr3_context_t *context, float *x, float *y);
bool Q2_FSR3_GetJitterPhase(const q2_fsr3_context_t *context,
                            uint32_t *phase, uint32_t *phase_count);
uint64_t Q2_FSR3_GetCurrentFrameId(const q2_fsr3_context_t *context);
/* Number of provider callbacks that recorded interpolation work successfully. */
uint64_t Q2_FSR3_GetProviderDispatchCount(const q2_fsr3_context_t *context);
int Q2_FSR3_GetLastError(const q2_fsr3_context_t *context);
bool Q2_FSR3_Dispatch(q2_fsr3_context_t *context, VkCommandBuffer command_buffer,
                      VkImage color, VkImageView color_view, VkFormat color_format,
                      VkImage depth, VkImageView depth_view, VkFormat depth_format,
                      VkImage motion, VkImageView motion_view, VkFormat motion_format,
                      VkImage reactive, VkImageView reactive_view, VkFormat reactive_format,
                      VkImage composition,
                      VkImage output, VkImageView output_view, VkFormat output_format,
                      float jitter_x, float jitter_y, float sharpness,
                      float frame_time_ms, float vertical_fov_radians,
                      float camera_near, float camera_far, bool reset);
bool Q2_FSR3_PrepareFrameGeneration(q2_fsr3_context_t *context,
                                    VkCommandBuffer command_buffer,
                                    VkImage depth, VkFormat depth_format,
                                    VkImage motion, VkFormat motion_format,
                                    float jitter_x, float jitter_y,
                                    float frame_time_ms,
                                    float vertical_fov_radians,
                                    float camera_near, float camera_far);
bool Q2_FSR3_DispatchFrameGeneration(q2_fsr3_context_t *context,
                                     VkCommandBuffer command_buffer,
                                     VkImage present, VkFormat present_format,
                                     VkImage output, VkFormat output_format,
                                     bool reset);
bool Q2_FSR3_FrameGenerationEnabled(const q2_fsr3_context_t *context);
bool Q2_FSR3_FrameGenerationFailed(const q2_fsr3_context_t *context);

/* The FSR3 frame-interpolation provider owns a pseudo swapchain and its
 * acquire/present implementation. These calls keep the C Vulkan backend
 * independent of the SDK's C++ reference APIs. Unsupported builds return
 * NULL/false and retain the native swapchain path. */
q2_fsr3_provider_t *Q2_FSR3_CreateProvider(
    q2_fsr3_context_t *context,
    VkSwapchainKHR native_swapchain,
    const VkSwapchainCreateInfoKHR *create_info,
    VkQueue game_queue, uint32_t game_queue_family,
    VkQueue async_compute_queue, uint32_t async_compute_queue_family,
    VkQueue present_queue, uint32_t present_queue_family,
    VkQueue image_acquire_queue, uint32_t image_acquire_queue_family,
    VkSwapchainKHR *provider_swapchain,
    q2_fsr3_provider_functions_t *functions,
    const char **failure_reason);
bool Q2_FSR3_ConfigureProvider(
    q2_fsr3_context_t *context,
    q2_fsr3_provider_t *provider,
    bool enabled, bool allow_async_workloads, uint64_t frame_id,
    VkImage hudless_color, VkFormat hudless_color_format,
    uint32_t hudless_color_width, uint32_t hudless_color_height);
bool Q2_FSR3_WaitProvider(q2_fsr3_context_t *context,
                          q2_fsr3_provider_t *provider);
void Q2_FSR3_DestroyProvider(q2_fsr3_context_t *context,
                             q2_fsr3_provider_t *provider,
                             bool device_lost);

#ifdef __cplusplus
}
#endif
#endif
