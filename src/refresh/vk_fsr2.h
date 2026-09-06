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
typedef struct q2_fsr2_context q2_fsr2_context_t;

q2_fsr2_context_t *Q2_FSR2_Create(VkPhysicalDevice physical_device,
                                   VkDevice device,
                                   PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                                   PFN_vkGetDeviceProcAddr get_device_proc_addr,
                                   uint32_t render_width,
                                   uint32_t render_height,
                                   uint32_t display_width,
                                   uint32_t display_height);
void Q2_FSR2_Destroy(q2_fsr2_context_t *context);
bool Q2_FSR2_Dispatch(q2_fsr2_context_t *context, VkCommandBuffer command_buffer,
                      VkImage color, VkImageView color_view, VkFormat color_format,
                      VkImage depth, VkImageView depth_view, VkFormat depth_format,
                      VkImage motion, VkImageView motion_view, VkFormat motion_format,
                      VkImage output, VkImageView output_view, VkFormat output_format,
                      float frame_time_ms, float vertical_fov_radians, bool reset);

#ifdef __cplusplus
}
#endif
#endif
