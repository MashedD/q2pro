/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Types and state shared by the Vulkan renderer's source files.

#pragma once

#include "shared/shared.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include "common/bsp.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/math.h"
#include "common/prompt.h"
#include "common/utils.h"
#include "common/zone.h"
#include "client/client.h"
#include "client/video.h"
#include "format/md2.h"
#if USE_MD3
#include "format/md3.h"
#endif
#include "format/sp2.h"
#include "images.h"
#include "gl.h"
#include "refresh/refresh.h"
#include "system/system.h"
#include "vk_backend.h"

#if USE_VULKAN

#define VK_MAX_INSTANCE_EXTENSIONS 16
#define VK_MAX_DEBUG_LINE_VERTICES TESS_MAX_VERTICES
#define VK_MAX_DEBUG_TEXT_CHARS    (TESS_MAX_VERTICES / 4)
#define VK_MAX_DEBUG_TEXT_VERTICES (VK_MAX_DEBUG_TEXT_CHARS * 4)
#define VK_MAX_DEBUG_TEXT_INDICES  (VK_MAX_DEBUG_TEXT_CHARS * 6)
#define VK_MAX_PARTICLE_VERTICES   (MAX_PARTICLES * 6)
#define VK_MAX_DUST_MOTES          48
#define VK_MAX_LAVA_EMBER_ANCHORS  96
#define VK_MAX_LAVA_EMBERS         48
#define VK_MAX_SHOCKWAVES          8
#define VK_MAX_IMPACT_MARKS        16
#define VK_MAX_SPLASH_RIPPLES      12
#define VK_MAX_WATER_WAKES         6
#define VK_MAX_TELEPORT_VORTICES   4
#define VK_MAX_ITEM_RESPAWNS       8
#define VK_MAX_ELECTRIC_FILAMENTS  8
#define VK_MAX_LANDING_DUST        6
#define VK_MAX_ENERGY_COLLAPSES    5
#define VK_MAX_RAIL_IONIZATIONS    6
#define VK_MAX_LIGHTMAP_EXTENTS    513
#define VK_MAX_FRAMES_IN_FLIGHT    3
#define VK_MAX_CUBEMAPS            16

#if USE_VULKAN_RAYTRACING
#define VK_RT_REQUIRED_EXTENSION_COUNT 3
#endif

typedef struct {
    float rect[4];
    float color[4];
    float screen[2];
    float _pad[2];
    float uv[4];
} vk_draw_push_t;

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkDescriptorSet descriptor_set;
    uint32_t width;
    uint32_t height;
    uint32_t mip_levels;
} vk_texture_t;

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
} vk_buffer_t;

#if USE_VULKAN_RAYTRACING
typedef struct {
    VkAccelerationStructureKHR handle;
    vk_buffer_t storage;
} vk_acceleration_structure_t;
#endif

typedef struct {
    float position[3];
    float color[4];
    float uv[2];
    float normal[3];
} vk_vertex_t;

typedef enum {
    VK_DRAW_OTHER,
    VK_DRAW_WORLD,
    VK_DRAW_ENTITY,
    VK_DRAW_PARTICLE,
} vk_draw_scope_t;


typedef struct {
    vk_buffer_t vertices;
    vk_buffer_t indices;
    uint32_t vertex_count;
    uint32_t index_count;
} vk_mesh_t;

typedef struct {
    uint32_t first_index;
    uint32_t index_count;
    uint32_t texture_index;
    uint32_t first_face;
    uint32_t face_count;
} vk_world_batch_t;

typedef struct {
    uint32_t first_vertex;
    uint32_t edge_count;
    uint32_t texture_index;
    vec3_t center;
    mface_t *face;
} vk_world_build_face_t;

typedef enum {
    VK_WORLD_OPAQUE,
    VK_WORLD_ALPHA,
    VK_WORLD_ENTITY_ALPHA,
} vk_world_pass_t;

#define VK_WORLD_MAX_DLIGHTS 3
#define VK_RT_AFTERGLOW_LIGHTS 8
#define VK_RT_MATERIAL_RULES 256

typedef struct {
    dlight_t light;
    float last_seen;
    uint32_t seen_frame;
    bool valid;
} vk_rt_afterglow_light_t;

typedef struct {
    char name[MAX_TEXNAME];
    float reflect;
    float roughness;
} vk_rt_material_rule_t;

typedef struct {
    mface_t *face;
    uint32_t first_vertex;
    uint32_t edge_count;
    uint32_t first_index;
    uint32_t index_count;
    vec3_t center;
    uint16_t pixel_lm_x, pixel_lm_y;
    uint16_t pixel_lm_w, pixel_lm_h;
#if USE_VULKAN_RAYTRACING
    uint32_t rt_light_offset;
    uint8_t rt_light_count;
#endif
} vk_world_face_t;

typedef struct {
    mface_t *face;
    vec3_t origin;
    vec3_t normal;
    uint32_t hash;
} vk_lava_ember_anchor_t;

typedef struct {
    int width;
    int height;
    int origin_x;
    int origin_y;
    image_t *image;
} vk_sprite_frame_t;

typedef struct {
    vec3_t bounds[2];
    float radius;
} vk_alias_frame_t;

typedef struct {
    uint32_t first_index;
    uint32_t index_count;
    int skin_start;
    int skin_count;
} vk_alias_batch_t;

typedef struct {
    enum {
        VK_MODEL_FREE,
        VK_MODEL_ALIAS,
        VK_MODEL_SPRITE,
    } type;
    char name[MAX_QPATH];
    unsigned registration_sequence;
    vk_sprite_frame_t *frames;
    vk_alias_frame_t *alias_frames;
    vk_alias_batch_t *alias_batches;
    int frame_count;
    int alias_batch_count;
    vk_mesh_t mesh;
    vk_buffer_t alias_line_indices;
    uint32_t alias_line_index_count;
    image_t **skins;
    maliasskinname_t *skin_names;
    int skin_count;
    uint32_t vertex_count;
#if USE_MD5
    vk_mesh_t md5_mesh;
    vk_buffer_t md5_line_indices;
    uint32_t md5_line_index_count;
    vk_alias_frame_t *md5_frames;
    vk_alias_batch_t *md5_batches;
    image_t **md5_skins;
    int md5_frame_count;
    int md5_batch_count;
    int md5_skin_count;
    uint32_t md5_vertex_count;
#endif
} vk_model_t;

#if USE_VULKAN_RAYTRACING
#define VK_MAX_SURFACE_LIGHTS 64
#define VK_MAX_FACE_SURFACE_LIGHTS 12
typedef struct {
    vec3_t origin;
    vec3_t normal;
    vec3_t color;
    float range;
    float strength;
    float radius;
} vk_surface_light_t;

typedef struct {
    float origin_range[4];
    float color_strength[4];
    float normal[4];
} vk_gpu_surface_light_t;

typedef struct {
    uint32_t light_count;
    uint32_t index_count;
    uint32_t reserved[2];
    vk_gpu_surface_light_t lights[VK_MAX_SURFACE_LIGHTS];
} vk_gpu_surface_light_header_t;

typedef struct {
    uint32_t offset;
    uint32_t packed;
} vk_gpu_face_light_t;

typedef struct {
    float position[3];
    float normal[3];
    float uv[2];
    uint32_t rt_data[2];
} vk_rt_bake_vertex_t;
#endif

typedef struct {
    bsp_t *cache;
    uint16_t *texinfo_widths;
    uint16_t *texinfo_heights;
    vk_mesh_t mesh;
    vk_buffer_t pixel_lmuv_buffer;
    vk_texture_t pixel_lightmap_texture;
    vk_buffer_t *pixel_lightmap_staging;
    void **pixel_lightmap_staging_mapped;
    uint32_t pixel_lightmap_staging_count;
    VkDeviceSize pixel_lightmap_staging_size;
    VkBufferImageCopy *pixel_lightmap_copies;
    uint32_t pixel_lightmap_copy_count;
    uint32_t pixel_lightmap_copy_capacity;
    bool pixel_lightmap_update_pending;
    vk_buffer_t line_indices;
    vk_buffer_t batch_indices;
    void *batch_index_mapped;
    uint32_t *batch_index_data;
    uint32_t batch_index_capacity;
    vk_world_batch_t *batches;
    vk_world_face_t *faces;
    uint32_t batch_count;
    uint32_t face_count;
    uint32_t line_index_count;
    float size;
    vk_rt_material_rule_t rt_material_rules[VK_RT_MATERIAL_RULES];
    uint32_t rt_material_rule_count;
    bool rt_material_file_loaded;
    vk_buffer_t rt_light_buffer;
    vk_buffer_t rt_light_indices;
    bool surface_lights_ready;
#if USE_VULKAN_RAYTRACING
    vk_buffer_t rt_indices;
    uint32_t rt_index_count;
    vk_acceleration_structure_t rt_blas;
    vk_texture_t rt_ao_texture;
    VkFramebuffer rt_ao_framebuffer;
    uint32_t rt_ao_samples;
    uint32_t rt_emissive_samples;
    uint32_t rt_bake_msec;
    uint32_t rt_coverage_texels;
    uint32_t rt_occluded_texels;
    uint32_t rt_emissive_texels;
    byte rt_ao_peak;
    byte rt_emissive_peak;
    bool rt_ao_ready;
    vk_surface_light_t surface_lights[VK_MAX_SURFACE_LIGHTS];
    uint32_t surface_light_count;
    uint32_t *surface_light_indices;
    uint32_t surface_light_index_count;
#endif
    cplane_t frustum[4];
    vec3_t vieworg;
    int viewcluster;
    int nolm_mask;
    unsigned drawframe;
    unsigned visframe;
    float lightstyles[MAX_LIGHTSTYLES];
    bool lightstyle_used[MAX_LIGHTSTYLES];
    bool lightstyles_valid;
    bool has_sky;
    bool sky_visible;
    vk_lava_ember_anchor_t lava_ember_anchors[VK_MAX_LAVA_EMBER_ANCHORS];
    uint32_t lava_ember_anchor_count;
} vk_world_t;

typedef struct {
    float rect[4];
    float color[4];
    float screen[2];
} vk_rect_push_t;

typedef struct {
    float projection[4];
    float control[4];
    float view_up[4];
} vk_ssr_push_t;

typedef struct {
    mat4_t mvp;
    float color[4];
} vk_color3d_push_t;

typedef struct {
    image_t *image;
    vk_texture_t faces[6];
} vk_cubemap_t;

typedef struct {
    mat4_t mvp;
    float color[4];
    float scroll[4];
    float dlight[4];
    float fog[4];
    float intensity;
    float desaturation;
} vk_world_push_t;

typedef char vk_world_compact_fog_offset_check[
    offsetof(vk_world_push_t, fog) == 112 ? 1 : -1];
typedef char vk_world_compact_intensity_offset_check[
    offsetof(vk_world_push_t, intensity) == 128 ? 1 : -1];
typedef char vk_world_compact_desaturation_offset_check[
    offsetof(vk_world_push_t, desaturation) == 132 ? 1 : -1];

typedef struct {
    mat4_t mvp;
    float color[4];
    float scroll[4];
    float dlight[4];
    float dlight_origins[VK_WORLD_MAX_DLIGHTS][4];
    float dlight_colors[VK_WORLD_MAX_DLIGHTS][4];
    float fog[4];
    float intensity;
    float desaturation;
    float _rt_padding[2];
    float rt_enabled;
    float _rt_pad;
    float _rt_tail[2];
} vk_world_lit_push_t;

typedef char vk_world_dlight_origins_offset_check[
    offsetof(vk_world_lit_push_t, dlight_origins) == 112 ? 1 : -1];
typedef char vk_world_dlight_colors_offset_check[
    offsetof(vk_world_lit_push_t, dlight_colors) == 160 ? 1 : -1];
typedef char vk_world_fog_offset_check[
    offsetof(vk_world_lit_push_t, fog) == 208 ? 1 : -1];
typedef char vk_world_desaturation_offset_check[
    offsetof(vk_world_lit_push_t, desaturation) == 228 ? 1 : -1];
typedef char vk_world_lit_push_size_check[
    sizeof(vk_world_lit_push_t) == 256 ? 1 : -1];

typedef struct {
    mat4_t mvp;
    float color[4];
    float scroll[4];
    float dlight[4];
    float dlight_origins[VK_WORLD_MAX_DLIGHTS][4];
    float dlight_colors[VK_WORLD_MAX_DLIGHTS][4];
    float fog[4];
    float intensity;
    float desaturation;
    float lm_scale[2];
    float rt_params[4];
} vk_world_pixel_push_t;

typedef char vk_world_pixel_lm_scale_offset_check[
    offsetof(vk_world_pixel_push_t, lm_scale) == 232 ? 1 : -1];
typedef char vk_world_pixel_rt_params_offset_check[
    offsetof(vk_world_pixel_push_t, rt_params) == 240 ? 1 : -1];

typedef struct {
    mat4_t mvp;
    float color[4];
    float shadedir[4];
    float backlerp;
    float shellscale;
    float depthscale;
    float _pad;
    float fog[4];
    float intensity;
    float desaturation;
    float _pad2[2];         // skin_tint starts on a 16-byte boundary
    float skin_tint[4];     // rgb multiplier, alpha 1 when tinted
    float skin_tint_dark[4]; // rgb, alpha = gamma; 0 tints the lit color
} vk_alias_push_t;

// Must match the push block of vk_alias.vert and vk_alias*.frag.
typedef char vk_alias_push_size_check[
    sizeof(vk_alias_push_t) == 176 ? 1 : -1];

typedef struct {
    mat4_t mvp;
    float color[4];
    float shadedir[4];
    float backlerp;
    float shellscale;
    float depthscale;
    float _pad;
    float fog[4];
    float intensity;
    float _pad2[3];
    float height_x[4];
    float height_y[4];
    float height_z[4];
    float heightfog_start[4];
    float heightfog_end[4];
    float heightfog_view[4];
    float heightfog_params[4];
} vk_alias_shadow_push_t;

typedef struct {
    uint32_t frame;
    uint32_t oldframe;
    float backlerp;
} vk_alias_lerp_t;

typedef struct {
    uint32_t graphics_family;
    uint32_t present_family;
    bool has_graphics;
    bool has_present;
} vk_queue_families_t;

typedef struct {
    void *library;

    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
    PFN_vkCreateInstance CreateInstance;
    PFN_vkEnumerateInstanceVersion EnumerateInstanceVersion;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;

    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkCreateDevice CreateDevice;
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceProperties2 GetPhysicalDeviceProperties2;
    PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures;
    PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
    PFN_vkGetPhysicalDeviceFormatProperties GetPhysicalDeviceFormatProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR;
    PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties;

    PFN_vkDestroyDevice DestroyDevice;
    PFN_vkGetDeviceQueue GetDeviceQueue;
    PFN_vkDeviceWaitIdle DeviceWaitIdle;
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR;
    PFN_vkCreateImageView CreateImageView;
    PFN_vkDestroyImageView DestroyImageView;
    PFN_vkCreateRenderPass CreateRenderPass;
    PFN_vkDestroyRenderPass DestroyRenderPass;
    PFN_vkCreateFramebuffer CreateFramebuffer;
    PFN_vkDestroyFramebuffer DestroyFramebuffer;
    PFN_vkCreateShaderModule CreateShaderModule;
    PFN_vkDestroyShaderModule DestroyShaderModule;
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool CreateDescriptorPool;
    PFN_vkDestroyDescriptorPool DestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets;
    PFN_vkFreeDescriptorSets FreeDescriptorSets;
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets;
    PFN_vkCreateSampler CreateSampler;
    PFN_vkDestroySampler DestroySampler;
    PFN_vkCreatePipelineLayout CreatePipelineLayout;
    PFN_vkDestroyPipelineLayout DestroyPipelineLayout;
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines;
    PFN_vkDestroyPipeline DestroyPipeline;
    PFN_vkCreateQueryPool CreateQueryPool;
    PFN_vkDestroyQueryPool DestroyQueryPool;
    PFN_vkGetQueryPoolResults GetQueryPoolResults;
    PFN_vkCreateCommandPool CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkFreeCommandBuffers FreeCommandBuffers;
    PFN_vkResetCommandBuffer ResetCommandBuffer;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkCmdBeginRenderPass CmdBeginRenderPass;
    PFN_vkCmdEndRenderPass CmdEndRenderPass;
    PFN_vkCmdClearAttachments CmdClearAttachments;
    PFN_vkCmdBindPipeline CmdBindPipeline;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets;
    PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers;
    PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer;
    PFN_vkCmdSetViewport CmdSetViewport;
    PFN_vkCmdSetScissor CmdSetScissor;
    PFN_vkCmdSetLineWidth CmdSetLineWidth;
    PFN_vkCmdPushConstants CmdPushConstants;
    PFN_vkCmdDraw CmdDraw;
    PFN_vkCmdDrawIndexed CmdDrawIndexed;
    PFN_vkCmdBeginQuery CmdBeginQuery;
    PFN_vkCmdEndQuery CmdEndQuery;
    PFN_vkCmdResetQueryPool CmdResetQueryPool;
    PFN_vkCmdWriteTimestamp CmdWriteTimestamp;
    PFN_vkCreateBuffer CreateBuffer;
    PFN_vkDestroyBuffer DestroyBuffer;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkCreateImage CreateImage;
    PFN_vkDestroyImage DestroyImage;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;
    PFN_vkAllocateMemory AllocateMemory;
    PFN_vkFreeMemory FreeMemory;
    PFN_vkBindBufferMemory BindBufferMemory;
    PFN_vkBindImageMemory BindImageMemory;
    PFN_vkMapMemory MapMemory;
    PFN_vkUnmapMemory UnmapMemory;
    PFN_vkCmdCopyBuffer CmdCopyBuffer;
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage;
    PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer;
    PFN_vkCreateSemaphore CreateSemaphore;
    PFN_vkDestroySemaphore DestroySemaphore;
    PFN_vkCreateFence CreateFence;
    PFN_vkDestroyFence DestroyFence;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkResetFences ResetFences;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkQueueWaitIdle QueueWaitIdle;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR;
    PFN_vkQueuePresentKHR QueuePresentKHR;
#if USE_VULKAN_RAYTRACING
    PFN_vkGetBufferDeviceAddress GetBufferDeviceAddress;
    PFN_vkCreateAccelerationStructureKHR CreateAccelerationStructureKHR;
    PFN_vkDestroyAccelerationStructureKHR DestroyAccelerationStructureKHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR GetAccelerationStructureBuildSizesKHR;
    PFN_vkCmdBuildAccelerationStructuresKHR CmdBuildAccelerationStructuresKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR GetAccelerationStructureDeviceAddressKHR;
#endif

    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkPhysicalDeviceFeatures physical_device_features;
    uint32_t instance_api_version;
    bool raytracing_active;     // always false without ray tracing support
#if USE_VULKAN_RAYTRACING
    bool raytracing_supported;
    bool raytracing_pixel_ready;
    bool raytracing_quad_sharing;
    char raytracing_reason[160];
#endif
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkCommandPool command_pool;
    VkDescriptorSetLayout texture_set_layout;
    VkDescriptorSetLayout ssr_set_layout;
    VkDescriptorSet ssr_descriptor_set;
    VkDescriptorSet ssr_resolve_descriptor_set;
#if USE_VULKAN_RAYTRACING
    VkDescriptorSetLayout rt_set_layout;
    VkDescriptorSet rt_descriptor_set;
    VkPipelineLayout rt_ao_pipeline_layout;
    VkRenderPass rt_ao_render_pass;
    VkPipeline rt_ao_pipeline;
    vk_acceleration_structure_t rt_tlas;
    vk_buffer_t rt_instance_buffer;
#endif
    VkDescriptorPool descriptor_pool;
    VkSampler sampler;
    VkSampler sky_sampler;
    VkSampler postprocess_sampler;
    VkSampler nearest_sampler;
    VkSampler sky_nearest_sampler;
    VkPipelineLayout rect_pipeline_layout;
    VkPipelineLayout ssr_pipeline_layout;
    VkPipelineLayout pixel_world_pipeline_layout;
    VkPipeline rect_pipeline;
    VkPipeline vignette_pipeline;
    VkPipeline texture_pipeline;
    VkPipeline scene_pipeline;
    VkPipeline waterwarp_pipeline;
    VkPipeline bloom_downscale_pipeline;
    VkPipeline bloom_blur_pipeline;
    VkPipeline bloom_add_pipeline;
    VkPipeline ssr_pipeline;
    VkPipeline ssr_resolve_pipeline;
    VkPipeline color3d_pipeline;
    VkPipeline line3d_pipeline;
    VkPipeline debug_line_pipeline;
    VkPipeline debug_text_pipeline;
    VkPipeline beam_pipeline;
    VkPipeline beam_bloom_pipeline;
    VkPipeline beam_add_pipeline;
    VkPipeline beam_add_bloom_pipeline;
    VkPipeline world_pipeline;
    VkPipeline world_alpha_pipeline;
    VkPipeline world_blend_pipeline;
    VkPipeline world_glow_pipeline;
    VkPipeline pixel_world_pipeline;
    VkPipeline pixel_world_alpha_pipeline;
    VkPipeline pixel_world_fast_pipeline;
    VkPipeline pixel_world_glow_pipeline;
    VkPipeline pixel_world_glow_alpha_pipeline;
    VkPipeline pixel_world_glow_fast_pipeline;
    VkPipeline pixel_world_rt_pipeline;
    VkPipeline pixel_world_rt_alpha_pipeline;
    VkPipeline pixel_world_rt_glow_pipeline;
    VkPipeline pixel_world_rt_glow_alpha_pipeline;
    VkPipeline sky_pipeline;
    VkPipeline sprite_pipeline;
    VkPipeline sprite_alpha_pipeline;
    VkPipeline sprite_bloom_pipeline;
    VkPipeline particle_add_pipeline;
    VkPipeline alias_pipeline;
    VkPipeline alias_bloom_pipeline;
    VkPipeline alias_shell_bloom_pipeline;  // back faces culled, for shells
    VkPipeline alias_alpha_pipeline;
    VkPipeline alias_depth_pipeline;
    VkPipeline alias_blend_pipeline;
    VkPipeline alias_shadow_pipeline;
    VkPipeline alias_cel_pipeline;
    VkPipeline alias_line_pipeline;
    VkQueryPool timestamp_query_pool;
    VkSwapchainKHR swapchain;
    VkRenderPass render_pass;
    VkRenderPass bloom_render_pass;
    VkFormat swapchain_format;
    VkFormat depth_format;
    VkSampleCountFlagBits sample_count;
    VkExtent2D swapchain_extent;
    VkImage multisample_image;
    VkDeviceMemory multisample_memory;
    VkImageView multisample_view;
    VkImage bloom_multisample_image;
    VkDeviceMemory bloom_multisample_memory;
    VkImageView bloom_multisample_view;
    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;
    VkImageView depth_sample_view;
    VkImage *swapchain_images;
    VkImageView *swapchain_views;
    VkFramebuffer *framebuffers;
    VkPresentModeKHR present_mode;
    VkFence *image_fences;
    VkFramebuffer scene_framebuffer;
    VkFramebuffer bloom_framebuffer;
    VkFramebuffer blur_framebuffer;
    VkFramebuffer ssr_framebuffer;
    VkFramebuffer ssr_resolve_framebuffer;
    VkImageLayout *swapchain_layouts;
    VkImageLayout scene_layout;
    VkImageLayout bloom_source_layout;
    VkImageLayout bloom_layout;
    VkImageLayout blur_layout;
    VkImageLayout depth_layout;
    VkImageLayout ssr_layout;
    VkImageLayout ssr_resolve_layout;
    VkCommandBuffer *command_buffers;
    uint32_t swapchain_image_count;
    unsigned swapchain_retry_time;
    bool swapchain_transfer_src;

    // screenshot copied from the frame being presented (VKR_ReadPixels)
    vk_buffer_t screenshot_readback;
    void *screenshot_mapped;
    uint32_t screenshot_width;
    uint32_t screenshot_height;
    bool screenshot_recorded;
    bool swapchain_deferred;
    bool swapchain_recreate_failed;
    vk_queue_families_t queues;
    VkSemaphore image_available[VK_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore *render_finished;   // one per swapchain image
    VkFence frame_fence[VK_MAX_FRAMES_IN_FLIGHT];
    uint32_t frame_index;
    uint32_t current_image;
    bool image_acquired;
    bool frame_active;
    bool render_pass_active;
    bool mrt_bloom;
    bool drawing_bloom;
    vk_draw_scope_t draw_scope;
    bool frame_bloom;
    bool frame_waterwarp;
    bool frame_ssr;
    bool ssr_ready;
    float scale;
    color_t color;
    color_t alt_color;
    bool color_set;
    clipRect_t clip;
    bool clip_set;
    vk_texture_t raw_texture;
    vk_texture_t scene_texture;
    vk_texture_t bloom_source_texture;
    vk_texture_t bloom_texture;
    vk_texture_t blur_texture;
    vk_texture_t ssr_texture;
    vk_texture_t ssr_resolve_texture;
    vk_texture_t particle_texture;
    vk_texture_t beam_texture;
    vk_texture_t shockwave_texture;
    vk_texture_t impact_texture;
    vk_mesh_t skybox;
    vk_mesh_t sprite_quad;
    vk_buffer_t sprite_quad_line_indices;
    uint32_t sprite_quad_line_index_count;
    vk_mesh_t null_model;
    vk_mesh_t beam_cylinder;
    vk_buffer_t beam_cylinder_line_indices;
    uint32_t beam_cylinder_line_index_count;
    vk_buffer_t debug_lines;
    vk_buffer_t debug_text_vertices;
    vk_buffer_t debug_text_indices;
    vk_buffer_t particle_vertices;
    void *particle_vertices_mapped;
    vk_vertex_t particle_batch[VK_MAX_PARTICLE_VERTICES];
    uint32_t sky_images[6];
    vk_cubemap_t cubemaps[VK_MAX_CUBEMAPS];
    int sky_cubemap;
    float sky_rotate;
    bool sky_autorotate;
    vec3_t sky_axis;
    refdef_t fd;
    bool fd_valid;
    int view_liquid_kind;
    vk_rt_afterglow_light_t afterglow_lights[VK_RT_AFTERGLOW_LIGHTS];
    uint32_t afterglow_frame;
    float afterglow_time;
    vk_world_t world;
    vk_model_t models[MAX_MODELS];
    uint32_t model_count;
    vk_texture_t textures[MAX_RIMAGES];
    float flare_fracs[MAX_EDICTS];
    uint32_t flare_times[MAX_EDICTS];
    float perf_stats_time;
    uint64_t frame_start_usec;
    unsigned wait_usec;
    unsigned acquire_usec;
    unsigned record_usec;
    unsigned submit_usec;
    unsigned present_usec;
    unsigned gpu_frame_usec;
    bool timestamp_valid[VK_MAX_FRAMES_IN_FLIGHT];
    VkPipeline bound_pipeline;
    VkDescriptorSet bound_texture_descriptor;
    VkDescriptorSet bound_pixel_world_descriptors[4];
    VkBuffer bound_vertex_buffers[3];
    VkDeviceSize bound_vertex_offsets[3];
    VkBuffer bound_index_buffer;
    VkDeviceSize bound_index_offset;
    VkIndexType bound_index_type;
} vk_state_t;

// Entity passes, in drawing order; see vk_entity_in_pass().
typedef enum {
    VK_ENTITY_BMODEL,
    VK_ENTITY_OPAQUE,
    VK_ENTITY_ALPHA_BACK,
    VK_ENTITY_BMODEL_ALPHA,
    VK_ENTITY_BEAM,
    VK_ENTITY_ALPHA_FRONT,
    VK_ENTITY_BLOOM_ONLY,
    VK_ENTITY_BLOOM_SOURCE,
} vk_entity_pass_t;

extern vk_state_t vk;

// vk_backend.c
#if USE_VULKAN_RAYTRACING
bool vk_build_mesh_blas(vk_acceleration_structure_t *as,
                        const vk_buffer_t *vertices, uint32_t vertex_count,
                        const vk_buffer_t *indices, uint32_t index_count);
void vk_destroy_acceleration_structure(vk_acceleration_structure_t *as);
#endif
extern bool vk_pixel_lightmaps_warned;
extern byte vk_gammatable[256];
extern const VkPipelineDynamicStateCreateInfo vk_3d_dynamic_state;
extern cvar_t *vk_anisotropy;
extern cvar_t *vk_beamstyle;
extern cvar_t *vk_bilerp_chars;
extern cvar_t *vk_bilerp_pics;
extern cvar_t *vk_bilerp_skies;
extern cvar_t *vk_bloom_downsample;
extern cvar_t *vk_bloom_lamps;
extern cvar_t *vk_bloom_sigma;
extern cvar_t *vk_brightness;
extern cvar_t *vk_celshading;
extern cvar_t *vk_coloredlightmaps;
extern cvar_t *vk_cull_models;
extern cvar_t *vk_cull_nodes;
extern cvar_t *vk_dlight_falloff;
extern cvar_t *vk_dotshading;
extern cvar_t *vk_doublelight_entities;
extern cvar_t *vk_downsample_skins;
extern cvar_t *vk_drawentities;
extern cvar_t *vk_draworder;
extern cvar_t *vk_drawsky;
extern cvar_t *vk_dynamic;
extern cvar_t *vk_flarespeed;
extern cvar_t *vk_fog;
extern cvar_t *vk_fontshadow;
extern cvar_t *vk_fullbright;
extern cvar_t *vk_gamma_scale_pics;
extern cvar_t *vk_intensity;
extern cvar_t *vk_invert;
extern cvar_t *vk_lightgrid;
extern cvar_t *vk_lightmap;
extern cvar_t *vk_lockpvs;
extern cvar_t *vk_md5_distance;
extern cvar_t *vk_md5_use;
extern cvar_t *vk_modulate;
extern cvar_t *vk_modulate_entities;
extern cvar_t *vk_modulate_world;
extern cvar_t *vk_nobind;
extern cvar_t *vk_novis;
extern cvar_t *vk_partscale;
extern cvar_t *vk_partshape;
extern cvar_t *vk_partstyle;
extern cvar_t *vk_picmip;
extern cvar_t *vk_pixel_lightmaps;
extern cvar_t *vk_raytracing;
extern cvar_t *vk_round_down;
extern cvar_t *vk_rt_afterglow;
extern cvar_t *vk_rt_ao;
extern cvar_t *vk_rt_atmosphere;
extern cvar_t *vk_rt_bounce;
extern cvar_t *vk_rt_caustics;
extern cvar_t *vk_rt_debug;
extern cvar_t *vk_rt_dust;
extern cvar_t *vk_rt_dust_lighting;
extern cvar_t *vk_rt_electric_filaments;
extern cvar_t *vk_rt_emissive;
extern cvar_t *vk_rt_emissive_flicker;
extern cvar_t *vk_rt_emissive_halo;
extern cvar_t *vk_rt_emissive_motes;
extern cvar_t *vk_rt_energy_collapse;
extern cvar_t *vk_rt_environment;
extern cvar_t *vk_rt_fog_turbulence;
extern cvar_t *vk_rt_impact_marks;
extern cvar_t *vk_rt_item_respawn;
extern cvar_t *vk_rt_landing_dust;
extern cvar_t *vk_rt_lava_embers;
extern cvar_t *vk_rt_light_coronas;
extern cvar_t *vk_rt_light_ripples;
extern cvar_t *vk_rt_light_scattering;
extern cvar_t *vk_rt_liquids;
extern cvar_t *vk_rt_rail_impact;
extern cvar_t *vk_rt_reflection_dispersion;
extern cvar_t *vk_rt_reflection_glints;
extern cvar_t *vk_rt_reflections;
extern cvar_t *vk_rt_shadow_fringe;
extern cvar_t *vk_rt_shockwaves;
extern cvar_t *vk_rt_skylight;
extern cvar_t *vk_rt_specular;
extern cvar_t *vk_rt_splash_ripples;
extern cvar_t *vk_rt_sunlight;
extern cvar_t *vk_rt_teleport_vortex;
extern cvar_t *vk_rt_underwater_absorption;
extern cvar_t *vk_rt_water_wakes;
extern cvar_t *vk_saturation;
extern cvar_t *vk_shadows;
extern cvar_t *vk_showbloom;
extern cvar_t *vk_showorigins;
extern cvar_t *vk_texturemode;
extern cvar_t *vk_vertexlight;
extern cvar_t *vk_waterwarp;
VkClearColorValue vk_frame_clear_color(void);
bool vk_alias_model_culled(const vk_model_t *model, const entity_t *ent,
                           const vec3_t axis[3], uint32_t frame,
                           uint32_t oldframe);
bool vk_alias_model_has_glowmap(const vk_model_t *model);
bool vk_bake_world_rt_ao(const bsp_t *bsp, const vk_world_face_t *faces,
                         uint32_t face_count, const vk_vertex_t *vertices,
                         const uint32_t *indices, const float *lmuv_data);
bool vk_begin_immediate(VkCommandBuffer *cmd);
bool vk_build_world_tlas(void);
bool vk_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, VkBuffer *buffer,
                      VkDeviceMemory *memory);
bool vk_end_immediate(VkCommandBuffer cmd);
bool vk_entity_mirrored(const entity_t *ent);
bool vk_fail_result(const char *what, VkResult result);
bool vk_shadow_stencil_enabled(void);
bool vk_upload_buffer(vk_buffer_t *dst, const void *data, VkDeviceSize size,
                      VkBufferUsageFlags usage);
bool vk_upload_mesh(vk_mesh_t *mesh, const vk_vertex_t *vertices,
                    uint32_t vertex_count, const uint32_t *indices,
                    uint32_t index_count);
bool vk_world_face_is_inline(const bsp_t *bsp, const mface_t *face);
const vk_rt_material_rule_t *vk_rt_material_rule(const char *name);
int vk_2d_height(void);
int vk_2d_width(void);
int vk_pixel_lightmap_mode(void);
int vk_upscale_level(uint32_t width, uint32_t height, imagetype_t type,
                     imageflags_t flags);
uint32_t *vk_build_line_indices(const uint32_t *indices, uint32_t index_count,
                                uint32_t *line_index_count);
uint32_t vk_find_memory_type(uint32_t type_bits,
                             VkMemoryPropertyFlags properties);
uint32_t vk_mip_level_count(uint32_t width, uint32_t height);
uint64_t vk_time_usec(void);
vk_alias_lerp_t vk_alias_lerp_for_entity(const vk_model_t *model,
                                         const entity_t *ent,
                                         const refdef_t *fd);
void vk_begin_render_pass(VkRenderPass render_pass, VkFramebuffer framebuffer,
                          VkClearColorValue color);
void vk_begin_render_pass_sized(VkRenderPass render_pass,
                                VkFramebuffer framebuffer,
                                VkClearColorValue color, uint32_t width,
                                uint32_t height);
void vk_bind_index_buffer(VkCommandBuffer cmd, VkBuffer buffer,
                          VkDeviceSize offset, VkIndexType type);
void vk_bind_pipeline(VkCommandBuffer cmd, VkPipelineBindPoint bind_point,
                      VkPipeline pipeline);
void vk_bind_pixel_world_descriptor(VkCommandBuffer cmd, uint32_t set_index,
                                    VkDescriptorSet set);
void vk_bind_texture_descriptor(VkCommandBuffer cmd, VkDescriptorSet set);
void vk_bind_vertex_buffers(VkCommandBuffer cmd, uint32_t first, uint32_t count,
                            const VkBuffer *buffers,
                            const VkDeviceSize *offsets);
void vk_clear_rect(int x, int y, int w, int h, uint32_t color);
void vk_count_batch3d(void);
void vk_destroy_buffer(vk_buffer_t *buffer);
void vk_destroy_mesh(vk_mesh_t *mesh);
void vk_draw_null_model(const entity_t *ent, const refdef_t *fd);
void vk_draw_texture_resource(int x, int y, int w, int h, float s1, float t1,
                              float s2, float t2, const vk_texture_t *texture);
void vk_entity_axis(const entity_t *ent, vec3_t axis[3]);
void vk_entity_model_matrix(mat4_t model, const entity_t *ent,
                            const vec3_t axis[3]);
void vk_entity_mvp(mat4_t out, const refdef_t *fd, const entity_t *ent,
                   const vec3_t axis[3]);
void vk_free_world(void);
void vk_load_rt_materials(void);
void vk_matrix_multiply(mat4_t out, const mat4_t a, const mat4_t b);
void vk_mip_map(byte *out, const byte *in, uint32_t width, uint32_t height);
void vk_model_mvp(mat4_t out, const refdef_t *fd, const mat4_t model);
void vk_projection_matrix(mat4_t m, float fov_x, float fov_y, int rdflags);
void vk_push_constants(VkCommandBuffer cmd, uint32_t size, const void *data);
void vk_push_pixel_world_constants(VkCommandBuffer cmd, uint32_t size,
                                   const void *data);
void vk_reset_bind_cache(void);
void vk_rt_prefix_error(const char *stage);
void vk_set_3d_viewport(const refdef_t *fd);
void vk_transition_color_target(VkCommandBuffer cmd, vk_texture_t *texture,
                                VkImageLayout *layout, VkImageLayout new_layout,
                                VkAccessFlags dst_access,
                                VkPipelineStageFlags dst_stage);
void vk_transition_depth(VkCommandBuffer cmd, VkImageLayout new_layout,
                         VkAccessFlags dst_access,
                         VkPipelineStageFlags dst_stage);
void vk_transition_scene(VkCommandBuffer cmd, VkImageLayout new_layout,
                         VkAccessFlags dst_access,
                         VkPipelineStageFlags dst_stage);
void vk_unmap_world_batch_indices(void);
void vk_update_rt_ao_descriptor(void);
void vk_view_matrix(mat4_t matrix, const refdef_t *fd);
#if USE_MD5
extern cvar_t *vk_md5_load;
#endif

// vk_entity.c
float vk_entity_light_modulate(void);
void vk_add_dynamic_lights(const refdef_t *fd, const vec3_t origin,
                           vec3_t color);
void vk_adjust_entity_light(vec3_t color);
void vk_draw_entities(const refdef_t *fd, vk_entity_pass_t pass);
void vk_trace_bmodel_light_points(const refdef_t *fd, const bsp_t *bsp,
                                  const vec3_t origin, const vec3_t end,
                                  lightpoint_t *point);

// vk_model.c
vk_model_t *vk_model_for_handle(qhandle_t handle);
void vk_free_models(bool all);
void vk_model_list_f(void);

// vk_particles.c
void vk_draw_particles(const refdef_t *fd);

// vk_pipeline.c
VkPipelineDepthStencilStateCreateInfo vk_shadow_depth_stencil_state(void);
VkShaderModule vk_create_shader_module(const uint32_t *code, size_t code_size);
bool vk_create_alias_pipeline(VkPipeline *pipeline, bool depth_write, bool blend,
                              bool color_write, bool alpha_test, bool color_only,
                              VkCullModeFlags cull_mode,
                              VkPolygonMode polygon_mode,
                              VkPrimitiveTopology topology,
                              VkCompareOp depth_compare, bool depth_bias_enable,
                              float depth_bias_constant, float depth_bias_slope,
                              VkPipelineDepthStencilStateCreateInfo *stencil_state);
bool vk_create_color3d_pipeline(VkPipeline *pipeline, bool depth_test,
                                bool depth_write, bool blend, bool additive,
                                VkPrimitiveTopology topology);
bool vk_create_pixel_world_pipeline(VkPipeline *pipeline, bool alpha_test,
                                    bool glowmap, bool fast_path,
                                    bool ray_query);
bool vk_create_pixel_world_pipeline_layout(void);
bool vk_create_rect_pipeline(void);
bool vk_create_rect_pipeline_ex(VkPipeline *pipeline, const uint32_t *vert_spv,
                                size_t vert_size);
bool vk_create_render_pass(void);
bool vk_create_ssr_pipeline(void);
bool vk_create_texture_pipeline(void);
bool vk_create_texture_pipeline_ex(VkPipeline *pipeline,
                                   const uint32_t *frag_spv, size_t frag_size,
                                   bool additive, VkExtent2D extent,
                                   bool postprocess);
bool vk_create_world_pipeline(VkPipeline *pipeline, bool depth_test,
                              bool depth_write, bool blend, bool alpha_test,
                              bool additive, bool glowmap, bool smooth_dlights,
                              bool color_write);
bool vk_validate_pixel_lightmap_shaders(void);

// vk_postprocess.c
bool vk_bloom_enabled_for_frame(void);
bool vk_bloom_requested(void);
bool vk_bloom_supported(void);
bool vk_ssr_enabled_for_frame(void);
bool vk_waterwarp_enabled_for_frame(void);
float vk_bloom_blur_sigma(void);
uint32_t vk_bloom_downsample_value(void);
void vk_composite_scene_texture(void);
void vk_draw_fullscreen_texture_sized(VkPipeline pipeline,
                                      const vk_texture_t *texture,
                                      const vec4_t color, uint32_t width,
                                      uint32_t height);
void vk_draw_refdef_texture(VkPipeline pipeline, const vk_texture_t *texture,
                            const vec4_t color);
void vk_finish_postprocess_scene(void);

// vk_texture.c
VkSampler vk_sampler_for_pic_flags(imageflags_t flags);
bool vk_create_beam_texture(void);
bool vk_create_default_texture(void);
bool vk_create_impact_texture(void);
bool vk_create_nearest_sampler(VkSampler *sampler,
                               VkSamplerAddressMode address_mode);
bool vk_create_particle_texture(void);
bool vk_create_postprocess_sampler(VkSampler *sampler);
bool vk_create_sampler(VkSampler *sampler);
bool vk_create_shell_texture(void);
bool vk_create_shockwave_texture(void);
bool vk_create_sky_sampler(VkSampler *sampler);
bool vk_upload_cubemap(image_t *image, const byte *pic);
bool vk_upload_texture(image_t *image, byte *pic);
bool vk_upload_texture_data(vk_texture_t *texture, uint32_t width,
                            uint32_t height, const void *pixels, bool mipmaps);
const vk_texture_t *vk_texture_for_index(unsigned index, bool allow_nobind);
vk_cubemap_t *vk_find_cubemap(const image_t *image);
void vk_destroy_texture(image_t *image);
void vk_destroy_texture_resource(vk_texture_t *texture);
void vk_sampler_selection_changed(cvar_t *self);
void vk_texture_barrier(VkCommandBuffer cmd, VkImage image,
                        VkImageLayout old_layout, VkImageLayout new_layout,
                        VkAccessFlags src_access, VkAccessFlags dst_access,
                        VkPipelineStageFlags src_stage,
                        VkPipelineStageFlags dst_stage);
void vk_texturemode_changed(cvar_t *self);
void vk_texturemode_g(genctx_t *ctx);
void vk_unload_cubemap(image_t *image);
void vk_unload_image(image_t *image);
void vk_update_texture_descriptor(vk_texture_t *texture);
void vk_update_texture_descriptor_with_sampler(vk_texture_t *texture,
                                               VkSampler sampler);
void vk_upload_image(image_t *image, byte *pic);

// vk_world.c
bool vk_dynamic_lights_enabled(void);
bool vk_static_light_point(const vec3_t origin, const refdef_t *fd,
                           vec3_t light);
float vk_glowmap_intensity(void);
float vk_lightstyle_value(const refdef_t *fd, byte style);
float vk_texture_intensity(void);
int vk_view_liquid_kind(const refdef_t *fd);
int vk_world_liquid_kind(const mface_t *face);
void vk_draw_bmodel(const entity_t *ent, const refdef_t *fd,
                    bool translucent_faces);
void vk_draw_fx_outlines(const vk_mesh_t *mesh, const vk_buffer_t *indices,
                         uint32_t index_count, const mat4_t mvp);
void vk_draw_skybox(const refdef_t *fd);
void vk_draw_world_mesh(const mat4_t mvp, bool marked_only, VkPipeline pipeline,
                        vk_world_pass_t pass, float entity_alpha,
                        const refdef_t *fd, const entity_t *ent,
                        const vec3_t axis[3]);
void vk_draw_world_outlines(const mat4_t mvp, bool marked_only,
                            vk_world_pass_t pass, const refdef_t *fd,
                            const entity_t *ent);
void vk_fog_params(const refdef_t *fd, float fog[4]);
void vk_height_fog_params(const refdef_t *fd, float start[4], float end[4],
                          float view[4], float params[4]);
void vk_mark_world_faces(const refdef_t *fd);
void vk_setup_world_frustum(const refdef_t *fd);
void vk_update_afterglow(const refdef_t *fd);
void vk_update_world_view(const refdef_t *fd);

// vk_world_load.c
float vk_lava_ember_random(uint32_t value);
void vk_destroy_pixel_lightmap_staging(void);
void vk_load_world(const char *name);
void vk_rebuild_world_lighting(void);

#endif // USE_VULKAN
