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

#if USE_VULKAN
#ifdef __cplusplus
extern "C" {
#endif
typedef struct q2_fsr3_context q2_fsr3_context_t;

q2_fsr3_context_t *Q2_FSR3_Create(VkPhysicalDevice physical_device,
                                   VkDevice device,
                                   VkInstance instance,
                                   PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                                   PFN_vkGetDeviceProcAddr get_device_proc_addr,
                                   uint32_t render_width,
                                   uint32_t render_height,
                                   uint32_t display_width,
                                   uint32_t display_height);
void Q2_FSR3_Destroy(q2_fsr3_context_t *context);
bool Q2_FSR3_GetJitter(q2_fsr3_context_t *context, float *x, float *y);
int Q2_FSR3_GetLastError(const q2_fsr3_context_t *context);
bool Q2_FSR3_Dispatch(q2_fsr3_context_t *context, VkCommandBuffer command_buffer,
                      VkImage color, VkImageView color_view, VkFormat color_format,
                      VkImage depth, VkImageView depth_view, VkFormat depth_format,
                      VkImage motion, VkImageView motion_view, VkFormat motion_format,
                      VkImage reactive, VkImageView reactive_view, VkFormat reactive_format,
                      VkImage output, VkImageView output_view, VkFormat output_format,
                      float jitter_x, float jitter_y,
                      float frame_time_ms, float vertical_fov_radians,
                      float camera_near, float camera_far, bool reset);

#ifdef __cplusplus
}
#endif
#endif
