/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/shared.h"
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
#define VK_MAX_LIGHTMAP_EXTENTS    513
#define VK_MAX_FRAMES_IN_FLIGHT    3
#define VK_MAX_CUBEMAPS            16

#if USE_VULKAN_RAYTRACING
#define VK_RT_REQUIRED_EXTENSION_COUNT 3
#endif

static const uint32_t vk_rect_vert_spv[] =
#include "vk_rect_vert_spv.h"
;

static const uint32_t vk_rect_frag_spv[] =
#include "vk_rect_frag_spv.h"
;

static const uint32_t vk_vignette_vert_spv[] =
#include "vk_vignette_vert_spv.h"
;

static const uint32_t vk_tex_vert_spv[] =
#include "vk_tex_vert_spv.h"
;

static const uint32_t vk_tex_frag_spv[] =
#include "vk_tex_frag_spv.h"
;

static const uint32_t vk_scene_frag_spv[] =
#include "vk_scene_frag_spv.h"
;

static const uint32_t vk_waterwarp_frag_spv[] =
#include "vk_waterwarp_frag_spv.h"
;

static const uint32_t vk_bloom_blur_frag_spv[] =
#include "vk_bloom_blur_frag_spv.h"
;

static const uint32_t vk_bloom_downscale_frag_spv[] =
#include "vk_bloom_downscale_frag_spv.h"
;

static const uint32_t vk_ssr_vert_spv[] =
#include "vk_ssr_vert_spv.h"
;

static const uint32_t vk_ssr_frag_spv[] =
#include "vk_ssr_frag_spv.h"
;

static const uint32_t vk_ssr_resolve_frag_spv[] =
#include "vk_ssr_resolve_frag_spv.h"
;

static const uint32_t vk_color3d_vert_spv[] =
#include "vk_color3d_vert_spv.h"
;

static const uint32_t vk_color3d_frag_spv[] =
#include "vk_color3d_frag_spv.h"
;

static const uint32_t vk_world_vert_spv[] =
#include "vk_world_vert_spv.h"
;

static const uint32_t vk_world_lit_vert_spv[] =
#include "vk_world_lit_vert_spv.h"
;

static const uint32_t vk_world_frag_spv[] =
#include "vk_world_frag_spv.h"
;

static const uint32_t vk_world_lit_frag_spv[] =
#include "vk_world_lit_frag_spv.h"
;

#if USE_VULKAN_RAYTRACING
static const uint32_t vk_rt_ao_bake_vert_spv[] =
#include "vk_rt_ao_bake_vert_spv.h"
;
static const uint32_t vk_rt_ao_bake_frag_spv[] =
#include "vk_rt_ao_bake_frag_spv.h"
;
static const uint32_t vk_world_lit_rt_frag_spv[] =
#include "vk_world_lit_rt_frag_spv.h"
;
static const uint32_t vk_world_pixel_rt_frag_spv[] =
#include "vk_world_pixel_rt_frag_spv.h"
;
static const uint32_t vk_world_pixel_rt_vert_spv[] =
#include "vk_world_pixel_rt_vert_spv.h"
;
static const uint32_t vk_world_pixel_rt_alpha_frag_spv[] =
#include "vk_world_pixel_rt_alpha_frag_spv.h"
;
static const uint32_t vk_world_pixel_rt_glow_frag_spv[] =
#include "vk_world_pixel_rt_glow_frag_spv.h"
;
static const uint32_t vk_world_pixel_rt_glow_alpha_frag_spv[] =
#include "vk_world_pixel_rt_glow_alpha_frag_spv.h"
;
static const uint32_t vk_world_pixel_rt_quad_frag_spv[] =
#include "vk_world_pixel_rt_quad_frag_spv.h"
;
static const uint32_t vk_world_pixel_rt_quad_glow_frag_spv[] =
#include "vk_world_pixel_rt_quad_glow_frag_spv.h"
;
#endif

static const uint32_t vk_world_alpha_frag_spv[] =
#include "vk_world_alpha_frag_spv.h"
;

static const uint32_t vk_world_lit_alpha_frag_spv[] =
#include "vk_world_lit_alpha_frag_spv.h"
;

static const uint32_t vk_world_pixel_vert_spv[] =
#include "vk_world_pixel_vert_spv.h"
;

static const uint32_t vk_world_pixel_frag_spv[] =
#include "vk_world_pixel_frag_spv.h"
;

static const uint32_t vk_world_pixel_alpha_frag_spv[] =
#include "vk_world_pixel_alpha_frag_spv.h"
;
static const uint32_t vk_world_pixel_glow_frag_spv[] =
#include "vk_world_pixel_glow_frag_spv.h"
;
static const uint32_t vk_world_pixel_glow_alpha_frag_spv[] =
#include "vk_world_pixel_glow_alpha_frag_spv.h"
;

static const uint32_t vk_world_glow_frag_spv[] =
#include "vk_world_glow_frag_spv.h"
;

static const uint32_t vk_alias_vert_spv[] =
#include "vk_alias_vert_spv.h"
;

static const uint32_t vk_alias_shadow_vert_spv[] =
#include "vk_alias_shadow_vert_spv.h"
;

static const uint32_t vk_alias_shadow_frag_spv[] =
#include "vk_alias_shadow_frag_spv.h"
;
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
} vk_alias_push_t;

typedef char vk_alias_push_size_check[
    sizeof(vk_alias_push_t) == 136 ? 1 : -1];

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
#if USE_VULKAN_RAYTRACING
    bool raytracing_supported;
    bool raytracing_active;
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
    VkPipeline glare_pipeline;
    VkPipeline glare_occlusion_pipeline;
    VkPipeline alias_pipeline;
    VkPipeline alias_bloom_pipeline;
    VkPipeline alias_alpha_pipeline;
    VkPipeline alias_depth_pipeline;
    VkPipeline alias_blend_pipeline;
    VkPipeline alias_shadow_pipeline;
    VkPipeline alias_cel_pipeline;
    VkPipeline alias_line_pipeline;
    VkQueryPool glare_query_pool;
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
    bool swapchain_deferred;
    bool swapchain_recreate_failed;
    vk_queue_families_t queues;
    VkSemaphore image_available[VK_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore render_finished[VK_MAX_FRAMES_IN_FLIGHT];
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
    uint32_t glare_query_counts[VK_MAX_FRAMES_IN_FLIGHT];
    uint32_t glare_query_time;
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

static vk_state_t vk;
static cvar_t *vk_drawentities;
static cvar_t *vk_drawsky;
static cvar_t *vk_swapinterval;
static cvar_t *vk_present_mode;
static bool vk_present_mode_warned;
static cvar_t *vk_finish;
static cvar_t *vk_texturemode;
static cvar_t *vk_anisotropy;
static cvar_t *vk_round_down;
static cvar_t *vk_picmip;
static cvar_t *vk_downsample_skins;
static cvar_t *vk_bilerp_chars;
static cvar_t *vk_bilerp_pics;
static cvar_t *vk_bilerp_skies;
static cvar_t *vk_cubemaps;
static cvar_t *vk_saturation;
static cvar_t *vk_invert;
static cvar_t *vk_gamma;
static cvar_t *vk_gamma_scale_pics;
static cvar_t *vk_upscale_pcx;
#if USE_MD5
static cvar_t *vk_md5_load;
static cvar_t *vk_md5_use;
static cvar_t *vk_md5_distance;
#endif
static cvar_t *vk_partscale;
static cvar_t *vk_partstyle;
static cvar_t *vk_partshape;
static cvar_t *vk_beamstyle;
static cvar_t *vk_flarespeed;
static cvar_t *vk_lightgrid;
static cvar_t *vk_fullbright;
static cvar_t *vk_cull_models;
static cvar_t *vk_shadows;
static cvar_t *vk_stencilbits;
static cvar_t *vk_celshading;
static cvar_t *vk_dotshading;
static cvar_t *vk_draworder;
static cvar_t *vk_showorigins;
static cvar_t *vk_showtearing;
static cvar_t *vk_showbloom;
static cvar_t *vk_waterwarp;
static cvar_t *vk_bloom_sigma;
static cvar_t *vk_bloom_downsample;
static cvar_t *vk_bloom_streaks;
static cvar_t *vk_bloom_ghosts;
static cvar_t *vk_bloom_shafts;
static cvar_t *vk_glare;
static cvar_t *vk_glare_threshold;
static cvar_t *vk_glare_size;
static cvar_t *vk_glare_intensity;
static cvar_t *vk_perf_stats;
static cvar_t *vk_frames_in_flight;
static cvar_t *vk_device;
static cvar_t *vk_devicelist;
static cvar_t *vk_raytracing;
static cvar_t *vk_rt_emissive;
static cvar_t *vk_rt_ao;
static cvar_t *vk_rt_skylight;
static cvar_t *vk_rt_environment;
static cvar_t *vk_rt_afterglow;
static cvar_t *vk_rt_sunlight;
static cvar_t *vk_rt_reflections;
static cvar_t *vk_rt_specular;
static cvar_t *vk_rt_liquids;
static cvar_t *vk_rt_bounce;
static cvar_t *vk_rt_caustics;
static cvar_t *vk_rt_shadow_fringe;
static cvar_t *vk_rt_debug;
#if USE_DEBUG
static cvar_t *vk_showstats;
#endif
static cvar_t *vk_modulate;
static cvar_t *vk_modulate_entities;
static cvar_t *vk_doublelight_entities;
static cvar_t *vk_fontshadow;
static cvar_t *vk_modulate_world;
static cvar_t *vk_coloredlightmaps;
static cvar_t *vk_dynamic;
static cvar_t *vk_dlight_falloff;
static cvar_t *vk_brightness;
static cvar_t *vk_fog;
static cvar_t *vk_intensity;
static cvar_t *vk_shaders;
static cvar_t *vk_znear;
static cvar_t *vk_drawworld;
static cvar_t *vk_novis;
static cvar_t *vk_lockpvs;
static cvar_t *vk_lightmap;
static cvar_t *vk_pixel_lightmaps;
static cvar_t *vk_vertexlight;
static cvar_t *vk_nobind;
static cvar_t *vk_clear;
static cvar_t *vk_clearcolor;
static cvar_t *vk_polyblend;
static cvar_t *vk_damageblend_frac;
static cvar_t *vk_cull_nodes;
#if USE_DEBUG
static cvar_t *vk_debug_distfrac;
static cvar_t *vk_debug_linewidth;
#endif
static byte vk_gammatable[256];
static bool vk_pixel_lightmaps_warned;

static bool vk_upload_texture(image_t *image, byte *pic);
static bool vk_upload_cubemap(image_t *image, const byte *pic);
static void vk_unload_cubemap(image_t *image);
static void vk_destroy_texture(image_t *image);
static bool vk_upload_mesh(vk_mesh_t *mesh, const vk_vertex_t *vertices,
                           uint32_t vertex_count, const uint32_t *indices,
                           uint32_t index_count);
static uint32_t *vk_build_line_indices(const uint32_t *indices,
                                       uint32_t index_count,
                                       uint32_t *line_index_count);
static void vk_begin_render_pass(VkRenderPass render_pass,
                                 VkFramebuffer framebuffer,
                                 VkClearColorValue color);
static void vk_finish_postprocess_scene(void);
static bool vk_upload_texture_data(vk_texture_t *texture, uint32_t width,
                                   uint32_t height, const void *pixels,
                                   bool mipmaps);
static void vk_destroy_texture_resource(vk_texture_t *texture);
static VkShaderModule vk_create_shader_module(const uint32_t *code,
                                              size_t code_size);
static uint64_t vk_time_usec(void);
static void vk_destroy_pixel_lightmap_staging(void);
static bool vk_create_particle_texture(void);
static bool vk_create_beam_texture(void);
static uint32_t vk_frames_in_flight_value(void);
static uint32_t vk_bloom_downsample_value(void);
static void vk_entity_axis(const entity_t *ent, vec3_t axis[3]);
static void vk_entity_mvp(mat4_t out, const refdef_t *fd,
                          const entity_t *ent, const vec3_t axis[3]);
static bool vk_create_swapchain(int width, int height);
static bool vk_recreate_swapchain(const char *reason);
static const char *vk_device_type_string(VkPhysicalDeviceType type);
static void vk_destroy_mesh(vk_mesh_t *mesh);
#if USE_VULKAN_RAYTRACING
static void vk_destroy_acceleration_structure(vk_acceleration_structure_t *as);
static bool vk_build_mesh_blas(vk_acceleration_structure_t *as,
                               const vk_buffer_t *vertices,
                               uint32_t vertex_count,
                               const vk_buffer_t *indices,
                               uint32_t index_count);
static bool vk_build_world_tlas(void);
static bool vk_bake_world_rt_ao(const bsp_t *bsp,
                                const vk_world_face_t *faces,
                                uint32_t face_count,
                                const vk_vertex_t *vertices,
                                const uint32_t *indices,
                                const float *lmuv_data);
#endif
static void vk_free_world(void);
static void vk_build_glare_list(bsp_t *bsp);
static void vk_load_world(const char *name);
static bool vk_static_light_point(const vec3_t origin, const refdef_t *fd,
                                  vec3_t light);

static void vk_resample_texture(const byte *in, int inwidth, int inheight,
                                byte *out, int outwidth, int outheight)
{
    unsigned p1[MAX_TEXTURE_SIZE], p2[MAX_TEXTURE_SIZE];
    unsigned fracstep = inwidth * 0x10000 / outwidth;
    unsigned frac = fracstep >> 2;

    Q_assert(outwidth <= MAX_TEXTURE_SIZE);

    for (int i = 0; i < outwidth; i++) {
        p1[i] = 4 * (frac >> 16);
        frac += fracstep;
    }
    frac = 3 * (fracstep >> 2);
    for (int i = 0; i < outwidth; i++) {
        p2[i] = 4 * (frac >> 16);
        frac += fracstep;
    }

    float height_scale = (float)inheight / outheight;
    int row_stride = inwidth << 2;
    for (int i = 0; i < outheight; i++) {
        const byte *inrow1 = in + row_stride * (int)((i + 0.25f) * height_scale);
        const byte *inrow2 = in + row_stride * (int)((i + 0.75f) * height_scale);
        for (int j = 0; j < outwidth; j++) {
            const byte *pix1 = inrow1 + p1[j];
            const byte *pix2 = inrow1 + p2[j];
            const byte *pix3 = inrow2 + p1[j];
            const byte *pix4 = inrow2 + p2[j];
            out[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
            out[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
            out[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
            out[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
            out += 4;
        }
    }
}

static void vk_color_transform_texture(byte *pic, int width, int height,
                                       imagetype_t type, imageflags_t flags)
{
    bool world = type == IT_WALL && !(flags & IF_TURBULENT);
    bool invert = world && vk_invert && vk_invert->integer;
    bool software_gamma = !(r_config.flags & QVF_GAMMARAMP);
    bool scale_gamma = software_gamma &&
        (type == IT_WALL || type == IT_SKIN ||
         (vk_gamma_scale_pics && vk_gamma_scale_pics->integer));

    if (!world && !scale_gamma)
        return;
    if (world && !invert && !scale_gamma)
        return;

    byte *p = pic;
    int count = width * height;
    for (int i = 0; i < count; i++, p += 4) {
        float r = p[0];
        float g = p[1];
        float b = p[2];

        p[0] = Q_clipf(r, 0.0f, 255.0f);
        p[1] = Q_clipf(g, 0.0f, 255.0f);
        p[2] = Q_clipf(b, 0.0f, 255.0f);

        if (scale_gamma) {
            p[0] = vk_gammatable[p[0]];
            p[1] = vk_gammatable[p[1]];
            p[2] = vk_gammatable[p[2]];
        }

        if (invert) {
            p[0] = 255 - p[0];
            p[1] = 255 - p[1];
            p[2] = 255 - p[2];
        }
    }
}

static void vk_build_gamma_table(void)
{
    float gamma = vk_gamma ? vk_gamma->value : 1.0f;

    if (gamma == 1.0f) {
        for (int i = 0; i < 256; i++)
            vk_gammatable[i] = i;
        return;
    }

    for (int i = 0; i < 256; i++) {
        float value = 255.0f * powf((i + 0.5f) / 255.5f, gamma) + 0.5f;
        vk_gammatable[i] = min(value, 255);
    }
}

static void vk_gamma_changed(cvar_t *self)
{
    (void)self;

    vk_build_gamma_table();

    if ((r_config.flags & QVF_GAMMARAMP) && vid && vid->update_gamma)
        vid->update_gamma(vk_gammatable);
}

static void vk_upload_image(image_t *image, byte *pic)
{
    if (image->flags & IF_CUBEMAP) {
        if (!vk_upload_cubemap(image, pic))
            Com_WPrintf("Couldn't upload Vulkan cubemap %s: %s\n",
                        image->name, Com_GetLastError());
        return;
    }

    if (!vk_upload_texture(image, pic)) {
        Com_WPrintf("Couldn't upload Vulkan texture %s: %s\n",
                    image->name, Com_GetLastError());
    }
}

static void vk_unload_image(image_t *image)
{
    if (image->flags & IF_CUBEMAP) {
        vk_unload_cubemap(image);
        return;
    }

    vk_destroy_texture(image);
}

static const image_upload_t vk_image_upload = {
    .load = vk_upload_image,
    .unload = vk_unload_image,
    .glowmaps = true,
};

static bool vk_fail_result(const char *what, VkResult result)
{
    Com_SetLastError(va("%s failed: Vulkan error %d", what, result));
    return false;
}

static uint32_t vk_find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory;

    vk.GetPhysicalDeviceMemoryProperties(vk.physical_device, &memory);

    for (uint32_t i = 0; i < memory.memoryTypeCount; i++) {
        if ((type_bits & BIT(i)) &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return UINT32_MAX;
}

static bool vk_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags properties,
                             VkBuffer *buffer, VkDeviceMemory *memory)
{
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult result = vk.CreateBuffer(vk.device, &buffer_info, NULL, buffer);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateBuffer", result);

    VkMemoryRequirements req;
    vk.GetBufferMemoryRequirements(vk.device, *buffer, &req);

    uint32_t memory_type = vk_find_memory_type(req.memoryTypeBits, properties);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("No suitable Vulkan buffer memory type");
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_type,
    };
#if USE_VULKAN_RAYTRACING
    VkMemoryAllocateFlagsInfo alloc_flags = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
    };
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        alloc_info.pNext = &alloc_flags;
#endif
    result = vk.AllocateMemory(vk.device, &alloc_info, NULL, memory);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateMemory", result);

    result = vk.BindBufferMemory(vk.device, *buffer, *memory, 0);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkBindBufferMemory", result);

    return true;
}

static bool vk_create_texture_image(uint32_t width, uint32_t height,
                                    uint32_t mip_levels,
                                    VkImage *image, VkDeviceMemory *memory)
{
#if USE_BGRA
    const VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
#else
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
#endif

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { width, height, 1 },
        .mipLevels = mip_levels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult result = vk.CreateImage(vk.device, &image_info, NULL, image);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImage", result);

    VkMemoryRequirements req;
    vk.GetImageMemoryRequirements(vk.device, *image, &req);

    uint32_t memory_type = vk_find_memory_type(req.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("No suitable Vulkan image memory type");
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_type,
    };
    result = vk.AllocateMemory(vk.device, &alloc_info, NULL, memory);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateMemory", result);

    result = vk.BindImageMemory(vk.device, *image, *memory, 0);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkBindImageMemory", result);

    return true;
}

static bool vk_depth_format_has_stencil(VkFormat format)
{
    return format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

static const char *vk_format_name(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return "D32_SFLOAT_S8_UINT";
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return "D24_UNORM_S8_UINT";
    case VK_FORMAT_D32_SFLOAT:
        return "D32_SFLOAT";
    case VK_FORMAT_D16_UNORM:
        return "D16_UNORM";
    default:
        return "unknown";
    }
}

static bool vk_shadow_stencil_requested(void)
{
    return vk_stencilbits && Cvar_ClampInteger(vk_stencilbits, 0, 8) > 0;
}

static bool vk_shadow_stencil_enabled(void)
{
    return vk_shadow_stencil_requested() &&
        vk_depth_format_has_stencil(vk.depth_format);
}

static VkFormat vk_choose_depth_format(bool stencil)
{
    static const VkFormat stencil_candidates[] = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D16_UNORM,
    };
    static const VkFormat depth_candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D16_UNORM,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    const VkFormat *candidates = stencil ? stencil_candidates : depth_candidates;
    size_t count = stencil ? q_countof(stencil_candidates) :
        q_countof(depth_candidates);

    for (size_t i = 0; i < count; i++) {
        VkFormatProperties props;

        vk.GetPhysicalDeviceFormatProperties(vk.physical_device,
                                             candidates[i], &props);
        if (props.optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return candidates[i];
        }
    }

    return VK_FORMAT_UNDEFINED;
}

static VkSampleCountFlagBits vk_choose_sample_count(void)
{
    int requested = Cvar_ClampInteger(Cvar_Get("gl_multisamples", "0", CVAR_REFRESH),
                                      0, 32);
    if (requested < 2)
        return VK_SAMPLE_COUNT_1_BIT;

    VkSampleCountFlags supported =
        vk.physical_device_properties.limits.framebufferColorSampleCounts &
        vk.physical_device_properties.limits.framebufferDepthSampleCounts;
    static const VkSampleCountFlagBits counts[] = {
        VK_SAMPLE_COUNT_32_BIT,
        VK_SAMPLE_COUNT_16_BIT,
        VK_SAMPLE_COUNT_8_BIT,
        VK_SAMPLE_COUNT_4_BIT,
        VK_SAMPLE_COUNT_2_BIT,
    };

    for (size_t i = 0; i < q_countof(counts); i++) {
        if ((int)counts[i] <= requested && (supported & counts[i]))
            return counts[i];
    }

    Com_WPrintf("Vulkan device does not support requested %dx multisampling\n",
                requested);
    return VK_SAMPLE_COUNT_1_BIT;
}

static bool vk_begin_immediate(VkCommandBuffer *cmd)
{
    *cmd = VK_NULL_HANDLE;

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkResult result = vk.AllocateCommandBuffers(vk.device, &alloc_info, cmd);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateCommandBuffers", result);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    result = vk.BeginCommandBuffer(*cmd, &begin_info);
    if (result != VK_SUCCESS) {
        vk.FreeCommandBuffers(vk.device, vk.command_pool, 1, cmd);
        *cmd = VK_NULL_HANDLE;
        return vk_fail_result("vkBeginCommandBuffer", result);
    }

    return true;
}

static bool vk_end_immediate(VkCommandBuffer cmd)
{
    VkResult result = vk.EndCommandBuffer(cmd);
    if (result != VK_SUCCESS) {
        vk.FreeCommandBuffers(vk.device, vk.command_pool, 1, &cmd);
        return vk_fail_result("vkEndCommandBuffer", result);
    }

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    result = vk.QueueSubmit(vk.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        vk.FreeCommandBuffers(vk.device, vk.command_pool, 1, &cmd);
        return vk_fail_result("vkQueueSubmit", result);
    }

    result = vk.QueueWaitIdle(vk.graphics_queue);
    if (result != VK_SUCCESS) {
        vk.FreeCommandBuffers(vk.device, vk.command_pool, 1, &cmd);
        return vk_fail_result("vkQueueWaitIdle", result);
    }

    vk.FreeCommandBuffers(vk.device, vk.command_pool, 1, &cmd);
    return true;
}

static void vk_destroy_buffer(vk_buffer_t *buffer)
{
    if (!vk.device)
        return;

    if (buffer->buffer)
        vk.DestroyBuffer(vk.device, buffer->buffer, NULL);
    if (buffer->memory)
        vk.FreeMemory(vk.device, buffer->memory, NULL);

    memset(buffer, 0, sizeof(*buffer));
}

static void vk_unmap_world_batch_indices(void)
{
    if (vk.device && vk.world.batch_index_mapped) {
        vk.UnmapMemory(vk.device, vk.world.batch_indices.memory);
        vk.world.batch_index_mapped = NULL;
    }
}

static bool vk_upload_buffer(vk_buffer_t *dst, const void *data,
                             VkDeviceSize size, VkBufferUsageFlags usage)
{
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkBuffer device_buffer = VK_NULL_HANDLE;
    VkDeviceMemory device_memory = VK_NULL_HANDLE;

    if (!data || !size)
        return true;

    if (!vk_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &staging, &staging_memory))
        goto fail;

    void *mapped;
    VkResult result = vk.MapMemory(vk.device, staging_memory, 0, size, 0, &mapped);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkMapMemory", result);
        goto fail;
    }
    memcpy(mapped, data, size);
    vk.UnmapMemory(vk.device, staging_memory);

    if (!vk_create_buffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &device_buffer, &device_memory))
        goto fail;

    VkCommandBuffer cmd;
    if (!vk_begin_immediate(&cmd))
        goto fail;

    VkBufferCopy copy = {
        .size = size,
    };
    vk.CmdCopyBuffer(cmd, staging, device_buffer, 1, &copy);

    if (!vk_end_immediate(cmd))
        goto fail;

    vk_destroy_buffer(dst);
    dst->buffer = device_buffer;
    dst->memory = device_memory;
    dst->size = size;

    vk.DestroyBuffer(vk.device, staging, NULL);
    vk.FreeMemory(vk.device, staging_memory, NULL);
    return true;

fail:
    if (staging)
        vk.DestroyBuffer(vk.device, staging, NULL);
    if (staging_memory)
        vk.FreeMemory(vk.device, staging_memory, NULL);
    if (device_buffer)
        vk.DestroyBuffer(vk.device, device_buffer, NULL);
    if (device_memory)
        vk.FreeMemory(vk.device, device_memory, NULL);
    return false;
}

static void vk_destroy_mesh(vk_mesh_t *mesh)
{
    vk_destroy_buffer(&mesh->vertices);
    vk_destroy_buffer(&mesh->indices);
    mesh->vertex_count = 0;
    mesh->index_count = 0;
}

static const vk_texture_t *vk_texture_for_index(unsigned index, bool allow_nobind)
{
    if (allow_nobind && vk_nobind && vk_nobind->integer &&
        vk.textures[0].descriptor_set) {
        return &vk.textures[0];
    }

    if (index >= MAX_RIMAGES || !vk.textures[index].descriptor_set)
        return NULL;

    return &vk.textures[index];
}

static void vk_free_world(void)
{
    glr.num_glare_sources = 0;
#if USE_VULKAN_RAYTRACING
    vk_destroy_acceleration_structure(&vk.rt_tlas);
    vk_destroy_buffer(&vk.rt_instance_buffer);
    vk_destroy_acceleration_structure(&vk.world.rt_blas);
    vk_destroy_buffer(&vk.world.rt_indices);
    vk.world.rt_index_count = 0;
    if (vk.world.rt_ao_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.world.rt_ao_framebuffer, NULL);
        vk.world.rt_ao_framebuffer = VK_NULL_HANDLE;
    }
    vk_destroy_texture_resource(&vk.world.rt_ao_texture);
    vk.world.rt_ao_samples = 0;
    vk.world.rt_emissive_samples = 0;
    vk.world.rt_bake_msec = 0;
    vk.world.rt_coverage_texels = 0;
    vk.world.rt_occluded_texels = 0;
    vk.world.rt_emissive_texels = 0;
    vk.world.rt_ao_peak = 0;
    vk.world.rt_emissive_peak = 0;
    vk.world.rt_ao_ready = false;
    vk_destroy_buffer(&vk.world.rt_light_buffer);
    vk_destroy_buffer(&vk.world.rt_light_indices);
    if (vk.world.surface_light_indices) {
        Z_Free(vk.world.surface_light_indices);
        vk.world.surface_light_indices = NULL;
    }
    vk.world.surface_light_count = 0;
    vk.world.surface_light_index_count = 0;
    vk.world.surface_lights_ready = false;
#endif
    vk_destroy_mesh(&vk.world.mesh);
    vk_destroy_buffer(&vk.world.pixel_lmuv_buffer);
    vk_destroy_texture_resource(&vk.world.pixel_lightmap_texture);
    vk_destroy_pixel_lightmap_staging();
    vk_destroy_buffer(&vk.world.line_indices);
    vk_unmap_world_batch_indices();
    vk_destroy_buffer(&vk.world.batch_indices);
    if (vk.world.batch_index_data) {
        Z_Free(vk.world.batch_index_data);
        vk.world.batch_index_data = NULL;
    }
    vk.world.batch_index_capacity = 0;
    if (vk.world.batches) {
        Z_Free(vk.world.batches);
        vk.world.batches = NULL;
    }
    if (vk.world.faces) {
        Z_Free(vk.world.faces);
        vk.world.faces = NULL;
    }
    if (vk.world.texinfo_widths) {
        Z_Free(vk.world.texinfo_widths);
        vk.world.texinfo_widths = NULL;
    }
    if (vk.world.texinfo_heights) {
        Z_Free(vk.world.texinfo_heights);
        vk.world.texinfo_heights = NULL;
    }
    vk.world.batch_count = 0;
    vk.world.face_count = 0;
    vk.world.line_index_count = 0;
    vk.world.size = 0.0f;
    vk.world.viewcluster = -1;
    vk.world.drawframe = 0;
    vk.world.visframe = 0;
    vk.world.lightstyles_valid = false;

    if (vk.world.cache) {
        BSP_Free(vk.world.cache);
        vk.world.cache = NULL;
    }
}

static void vk_rt_material_normalize(char *name)
{
    if (!Q_stricmpn(name, "textures/", 9))
        memmove(name, name + 9, strlen(name + 9) + 1);
    COM_StripExtension(name, name, MAX_TEXNAME);
}

static const vk_rt_material_rule_t *vk_rt_material_rule(const char *name)
{
    char normalized[MAX_TEXNAME];
    if (!name)
        return NULL;
    Q_strlcpy(normalized, name, sizeof(normalized));
    vk_rt_material_normalize(normalized);
    for (uint32_t i = 0; i < vk.world.rt_material_rule_count; i++) {
        if (!Q_stricmp(vk.world.rt_material_rules[i].name, normalized))
            return &vk.world.rt_material_rules[i];
    }
    return NULL;
}

static void vk_load_rt_materials(void)
{
    byte *data = NULL;
    int len = FS_LoadFile("materials/rt_reflections.mat", (void **)&data);
    vk.world.rt_material_rule_count = 0;
    vk.world.rt_material_file_loaded = len >= 0 && data != NULL;
    if (!vk.world.rt_material_file_loaded)
        return;

    char *cursor = (char *)data;
    char *line = cursor;
    vk_rt_material_rule_t *current = NULL;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *comment = strstr(line, "//");
        if (comment)
            *comment = 0;
        while (*line && isspace((unsigned char)*line))
            line++;
        char *end = line + strlen(line);
        while (end > line && isspace((unsigned char)end[-1]))
            *--end = 0;
        if (*line) {
            char key[32], value[64];
            if (line[strlen(line) - 1] == ':') {
                line[strlen(line) - 1] = 0;
                vk_rt_material_normalize(line);
                if (vk.world.rt_material_rule_count < VK_RT_MATERIAL_RULES) {
                    current = &vk.world.rt_material_rules[vk.world.rt_material_rule_count++];
                    memset(current, 0, sizeof(*current));
                    Q_strlcpy(current->name, line, sizeof(current->name));
                    current->reflect = 1.0f;
                    current->roughness = 0.5f;
                } else {
                    current = NULL;
                }
            } else if (current && sscanf(line, "%31s %63s", key, value) == 2) {
                float v = strtof(value, NULL);
                if (!Q_stricmp(key, "reflect"))
                    current->reflect = min(max(v, 0.0f), 1.0f);
                else if (!Q_stricmp(key, "roughness"))
                    current->roughness = min(max(v, 0.0f), 1.0f);
                else
                    Com_DPrintf("Unknown RT material key '%s'\n", key);
            }
        }
        line = next;
    }
    FS_FreeFile(data);
    Com_Printf("Vulkan RT materials: %u reflection rules loaded\n",
               vk.world.rt_material_rule_count);
}

static void vk_free_model(vk_model_t *model)
{
    if (model->frames) {
        Z_Free(model->frames);
        model->frames = NULL;
    }
    if (model->alias_frames) {
        Z_Free(model->alias_frames);
        model->alias_frames = NULL;
    }
    if (model->alias_batches) {
        Z_Free(model->alias_batches);
        model->alias_batches = NULL;
    }
    if (model->skins) {
        Z_Free(model->skins);
        model->skins = NULL;
    }
    if (model->skin_names) {
        Z_Free(model->skin_names);
        model->skin_names = NULL;
    }
#if USE_MD5
    if (model->md5_frames)
        Z_Free(model->md5_frames);
    if (model->md5_batches)
        Z_Free(model->md5_batches);
    if (model->md5_skins)
        Z_Free(model->md5_skins);
    vk_destroy_mesh(&model->md5_mesh);
    vk_destroy_buffer(&model->md5_line_indices);
#endif
    vk_destroy_mesh(&model->mesh);
    vk_destroy_buffer(&model->alias_line_indices);
    model->alias_line_index_count = 0;
    memset(model, 0, sizeof(*model));
}

static void vk_free_models(bool all)
{
    for (uint32_t i = 0; i < vk.model_count; i++) {
        vk_model_t *model = &vk.models[i];

        if (model->type == VK_MODEL_FREE)
            continue;
        if (!all && model->registration_sequence == r_registration_sequence)
            continue;

        vk_free_model(model);
    }

    if (all)
        vk.model_count = 0;
}

static void vk_model_list_f(void)
{
    static const char types[] = "FAS";
    int count = 0;

    Com_Printf("------------------\n");
    for (uint32_t i = 0; i < vk.model_count; i++) {
        const vk_model_t *model = &vk.models[i];

        if (model->type == VK_MODEL_FREE)
            continue;

        Com_Printf("%c %5u verts %5u idx",
                   types[model->type],
                   model->vertex_count,
                   model->mesh.index_count);
        if (model->type == VK_MODEL_ALIAS) {
            Com_Printf(" %2d frm %2d bat %2d skin",
                       model->frame_count,
                       model->alias_batch_count,
                       model->skin_count);
        } else if (model->type == VK_MODEL_SPRITE) {
            Com_Printf(" %2d frm", model->frame_count);
        }
        Com_Printf(" : %s\n", model->name);
        count++;
    }
    Com_Printf("Total Vulkan models: %d (out of %u slots)\n",
               count, vk.model_count);
}

#if USE_DEBUG
static size_t vk_viewcluster_m(char *buffer, size_t size)
{
    return Q_snprintf(buffer, size, "%d", vk.world.viewcluster);
}

static size_t vk_viewleaf_m(char *buffer, size_t size)
{
    const bsp_t *bsp = vk.world.cache;

    if (bsp && bsp->nodes && bsp->leafs) {
        const mleaf_t *leaf = BSP_PointLeaf(bsp->nodes, vk.world.vieworg);

        if (leaf) {
            return Q_snprintf(buffer, size, "%td %d %d %d %#x",
                              leaf - bsp->leafs,
                              leaf->cluster,
                              leaf->numleafbrushes,
                              leaf->numleaffaces,
                              leaf->contents[0]);
        }
    }

    return Q_strlcpy(buffer, "", size);
}
#endif

static vk_model_t *vk_find_model(const char *name)
{
    for (uint32_t i = 0; i < vk.model_count; i++) {
        vk_model_t *model = &vk.models[i];

        if (model->type != VK_MODEL_FREE && !FS_pathcmp(model->name, name))
            return model;
    }

    return NULL;
}

static vk_model_t *vk_alloc_model(void)
{
    for (uint32_t i = 0; i < vk.model_count; i++) {
        if (vk.models[i].type == VK_MODEL_FREE)
            return &vk.models[i];
    }

    if (vk.model_count == q_countof(vk.models))
        return NULL;

    return &vk.models[vk.model_count++];
}

static qhandle_t vk_load_sprite_model(const char *name, const byte *rawdata, size_t length)
{
    dsp2header_t header;
    vk_model_t *model;
    const dsp2frame_t *src_frame;

    if (length < sizeof(header))
        return 0;

    header.ident = LittleLong(((const dsp2header_t *)rawdata)->ident);
    header.version = LittleLong(((const dsp2header_t *)rawdata)->version);
    header.numframes = LittleLong(((const dsp2header_t *)rawdata)->numframes);

    if (header.ident != SP2_IDENT || header.version != SP2_VERSION)
        return 0;
    if (header.numframes < 1 || header.numframes > SP2_MAX_FRAMES)
        return 0;
    if (sizeof(dsp2header_t) + sizeof(dsp2frame_t) * header.numframes > length)
        return 0;

    model = vk_alloc_model();
    if (!model)
        return 0;

    Q_strlcpy(model->name, name, sizeof(model->name));
    model->type = VK_MODEL_SPRITE;
    model->registration_sequence = r_registration_sequence;
    model->frame_count = header.numframes;
    model->frames = Z_Mallocz(sizeof(model->frames[0]) * model->frame_count);

    src_frame = (const dsp2frame_t *)(rawdata + sizeof(dsp2header_t));
    for (int i = 0; i < model->frame_count; i++) {
        char image_name[SP2_MAX_FRAMENAME];
        vk_sprite_frame_t *dst_frame = &model->frames[i];

        dst_frame->width = (int32_t)LittleLong(src_frame[i].width);
        dst_frame->height = (int32_t)LittleLong(src_frame[i].height);
        dst_frame->origin_x = (int32_t)LittleLong(src_frame[i].origin_x);
        dst_frame->origin_y = (int32_t)LittleLong(src_frame[i].origin_y);

        if (!Q_memccpy(image_name, src_frame[i].name, 0, sizeof(image_name)))
            dst_frame->image = R_NOTEXTURE;
        else
            dst_frame->image = IMG_Find(image_name, IT_SPRITE, IF_NONE);
    }

    return (model - vk.models) + 1;
}

static bool vk_check_md2_bounds(const dmd2header_t *header, size_t length)
{
    if (header->skinwidth < 1 || header->skinheight < 1 ||
        header->skinwidth > MD2_MAX_SKINWIDTH ||
        header->skinheight > MD2_MAX_SKINHEIGHT)
        return false;
    if (header->framesize < sizeof(dmd2frame_t) ||
        header->framesize > MD2_MAX_FRAMESIZE)
        return false;
    if (header->num_skins > MD2_MAX_SKINS ||
        header->num_xyz < 3 || header->num_xyz > MD2_MAX_VERTS ||
        header->num_st < 3 || header->num_tris < 1 ||
        header->num_tris > MD2_MAX_TRIANGLES ||
        header->num_frames < 1 || header->num_frames > MD2_MAX_FRAMES)
        return false;
    if ((uint64_t)header->ofs_skins + header->num_skins * MD2_MAX_SKINNAME > length ||
        (uint64_t)header->ofs_st + header->num_st * sizeof(dmd2stvert_t) > length ||
        (uint64_t)header->ofs_tris + header->num_tris * sizeof(dmd2triangle_t) > length ||
        (uint64_t)header->ofs_frames + header->num_frames * header->framesize > length ||
        header->ofs_end > length)
        return false;

    return true;
}

#if USE_MD5
static bool vk_load_md5_variant(vk_model_t *model)
{
    memhunk_t hunk = { 0 };
    md5_model_t *md5;
    vk_vertex_t *vertices = NULL;
    uint32_t *indices = NULL;
    uint32_t *line_indices = NULL;
    uint32_t line_index_count = 0;
    uint32_t vertex_count = 0, index_count = 0;
    bool ok = false;

    if (!model || !vk_md5_load || !vk_md5_load->integer ||
        !model->skin_names || !model->alias_batch_count)
        return false;

    int skin_count = model->alias_batches[0].skin_count;
    md5 = MOD_LoadMD5Replacement(model->name, model->frame_count,
                                 skin_count, model->skin_names, &hunk);
    if (!md5)
        return false;

    for (int i = 0; i < md5->num_meshes; i++) {
        const md5_mesh_t *mesh = &md5->meshes[i];
        if (mesh->num_verts < 1 || mesh->num_indices < 3 ||
            UINT32_MAX - vertex_count < (uint32_t)mesh->num_verts ||
            UINT32_MAX - index_count < (uint32_t)mesh->num_indices)
            goto out;
        vertex_count += mesh->num_verts;
        index_count += mesh->num_indices;
    }
    if (!vertex_count || !index_count || md5->num_frames < 1 ||
        (uint64_t)vertex_count * md5->num_frames > UINT32_MAX)
        goto out;

    vertices = Z_Malloc(sizeof(*vertices) * vertex_count * md5->num_frames);
    indices = Z_Malloc(sizeof(*indices) * index_count);
    model->md5_frames = Z_Mallocz(sizeof(*model->md5_frames) * md5->num_frames);
    model->md5_batches = Z_Mallocz(sizeof(*model->md5_batches) * md5->num_meshes);
    if (md5->num_skins) {
        model->md5_skins = Z_Malloc(sizeof(*model->md5_skins) * md5->num_skins);
        memcpy(model->md5_skins, md5->skins,
               sizeof(*model->md5_skins) * md5->num_skins);
    }

    uint32_t vertex_base = 0, first_index = 0;
    for (int mesh_index = 0; mesh_index < md5->num_meshes; mesh_index++) {
        const md5_mesh_t *mesh = &md5->meshes[mesh_index];
        model->md5_batches[mesh_index] = (vk_alias_batch_t) {
            .first_index = first_index,
            .index_count = mesh->num_indices,
            .skin_start = 0,
            .skin_count = md5->num_skins,
        };

        for (int i = 0; i < mesh->num_indices; i++) {
            if (mesh->indices[i] >= mesh->num_verts)
                goto out;
            indices[first_index + i] = vertex_base + mesh->indices[i];
        }

        for (int frame = 0; frame < md5->num_frames; frame++) {
            const md5_joint_t *skeleton =
                &md5->skeleton_frames[frame * md5->num_joints];
            vk_alias_frame_t *frame_info = &model->md5_frames[frame];
            if (mesh_index == 0)
                ClearBounds(frame_info->bounds[0], frame_info->bounds[1]);

            for (int vert_index = 0; vert_index < mesh->num_verts; vert_index++) {
                const md5_vertex_t *vert = &mesh->vertices[vert_index];
                vk_vertex_t *dst = &vertices[frame * vertex_count +
                                             vertex_base + vert_index];
                VectorClear(dst->position);
                VectorClear(dst->normal);

                for (int weight_index = 0; weight_index < vert->count; weight_index++) {
                    int index = vert->start + weight_index;
                    const md5_weight_t *weight = &mesh->weights[index];
                    const md5_joint_t *joint = &skeleton[mesh->jointnums[index]];
                    vec3_t transformed;

                    VectorRotate(weight->pos, joint->axis, transformed);
                    VectorMA(joint->pos, joint->scale, transformed, transformed);
                    VectorMA(dst->position, weight->bias, transformed, dst->position);
                    VectorRotate(vert->normal, joint->axis, transformed);
                    VectorMA(dst->normal, weight->bias, transformed, dst->normal);
                }

                VectorNormalize(dst->normal);
                Vector4Set(dst->color, 1.0f, 1.0f, 1.0f, 1.0f);
                dst->uv[0] = mesh->tcoords[vert_index].st[0];
                dst->uv[1] = mesh->tcoords[vert_index].st[1];
                AddPointToBounds(dst->position, frame_info->bounds[0],
                                 frame_info->bounds[1]);
            }
        }

        vertex_base += mesh->num_verts;
        first_index += mesh->num_indices;
    }

    for (int frame = 0; frame < md5->num_frames; frame++)
        model->md5_frames[frame].radius =
            RadiusFromBounds(model->md5_frames[frame].bounds[0],
                             model->md5_frames[frame].bounds[1]);

    if (!vk_upload_mesh(&model->md5_mesh, vertices,
                        vertex_count * md5->num_frames, indices, index_count))
        goto out;
    line_indices = vk_build_line_indices(indices, index_count, &line_index_count);
    if (!line_indices ||
        !vk_upload_buffer(&model->md5_line_indices, line_indices,
                          sizeof(*line_indices) * line_index_count,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
        goto out;

    model->md5_line_index_count = line_index_count;
    model->md5_frame_count = md5->num_frames;
    model->md5_batch_count = md5->num_meshes;
    model->md5_skin_count = md5->num_skins;
    model->md5_vertex_count = vertex_count;
    ok = true;

out:
    if (!ok) {
        if (model->md5_frames) {
            Z_Free(model->md5_frames);
            model->md5_frames = NULL;
        }
        if (model->md5_batches) {
            Z_Free(model->md5_batches);
            model->md5_batches = NULL;
        }
        if (model->md5_skins) {
            Z_Free(model->md5_skins);
            model->md5_skins = NULL;
        }
        vk_destroy_mesh(&model->md5_mesh);
        vk_destroy_buffer(&model->md5_line_indices);
    }
    if (vertices)
        Z_Free(vertices);
    if (indices)
        Z_Free(indices);
    if (line_indices)
        Z_Free(line_indices);
    Hunk_Free(&hunk);
    return ok;
}
#endif

static qhandle_t vk_load_md2_model(const char *name, const byte *rawdata, size_t length)
{
    dmd2header_t header;
    const dmd2triangle_t *src_tri;
    const dmd2stvert_t *src_tc;
    vk_model_t *model = NULL;
    uint16_t *vert_indices = NULL;
    uint16_t *tc_indices = NULL;
    uint32_t *final_indices = NULL;
    uint16_t *remap = NULL;
    vk_vertex_t *vertices = NULL;
    uint32_t *indices = NULL;
    uint32_t *line_indices = NULL;
    uint32_t line_index_count = 0;
    vk_alias_frame_t *alias_frames = NULL;
    uint32_t numindices = 0;
    uint32_t numverts = 0;
    qhandle_t handle = 0;

    if (length < sizeof(header))
        return 0;

    const dmd2header_t *src_header = (const dmd2header_t *)rawdata;
    header.ident = LittleLong(src_header->ident);
    header.version = LittleLong(src_header->version);
    header.skinwidth = LittleLong(src_header->skinwidth);
    header.skinheight = LittleLong(src_header->skinheight);
    header.framesize = LittleLong(src_header->framesize);
    header.num_skins = LittleLong(src_header->num_skins);
    header.num_xyz = LittleLong(src_header->num_xyz);
    header.num_st = LittleLong(src_header->num_st);
    header.num_tris = LittleLong(src_header->num_tris);
    header.num_glcmds = LittleLong(src_header->num_glcmds);
    header.num_frames = LittleLong(src_header->num_frames);
    header.ofs_skins = LittleLong(src_header->ofs_skins);
    header.ofs_st = LittleLong(src_header->ofs_st);
    header.ofs_tris = LittleLong(src_header->ofs_tris);
    header.ofs_frames = LittleLong(src_header->ofs_frames);
    header.ofs_glcmds = LittleLong(src_header->ofs_glcmds);
    header.ofs_end = LittleLong(src_header->ofs_end);

    if (header.ident != MD2_IDENT || header.version != MD2_VERSION ||
        !vk_check_md2_bounds(&header, length))
        return 0;

    uint32_t max_indices = header.num_tris * 3;
    vert_indices = Z_Malloc(sizeof(*vert_indices) * max_indices);
    tc_indices = Z_Malloc(sizeof(*tc_indices) * max_indices);
    final_indices = Z_Malloc(sizeof(*final_indices) * max_indices);
    remap = Z_Malloc(sizeof(*remap) * max_indices);

    src_tri = (const dmd2triangle_t *)(rawdata + header.ofs_tris);
    for (uint32_t i = 0; i < header.num_tris; i++) {
        uint32_t base = numindices;
        uint32_t j;

        for (j = 0; j < 3; j++) {
            uint16_t idx_xyz = LittleShort(src_tri[i].index_xyz[j]);
            uint16_t idx_st = LittleShort(src_tri[i].index_st[j]);

            if (idx_xyz >= header.num_xyz || idx_st >= header.num_st)
                break;

            vert_indices[base + j] = idx_xyz;
            tc_indices[base + j] = idx_st;
        }
        if (j == 3)
            numindices += 3;
    }

    if (numindices < 3)
        goto out;

    for (uint32_t i = 0; i < numindices; i++)
        remap[i] = UINT16_MAX;

    src_tc = (const dmd2stvert_t *)(rawdata + header.ofs_st);
    for (uint32_t i = 0; i < numindices; i++) {
        if (remap[i] != UINT16_MAX)
            continue;

        for (uint32_t j = i + 1; j < numindices; j++) {
            if (vert_indices[i] == vert_indices[j] &&
                src_tc[tc_indices[i]].s == src_tc[tc_indices[j]].s &&
                src_tc[tc_indices[i]].t == src_tc[tc_indices[j]].t) {
                remap[j] = i;
                final_indices[j] = numverts;
            }
        }

        remap[i] = i;
        final_indices[i] = numverts++;
    }

    if (!numverts || (uint64_t)numverts * header.num_frames > UINT32_MAX)
        goto out;

    vertices = Z_Malloc(sizeof(*vertices) * numverts * header.num_frames);
    indices = Z_Malloc(sizeof(*indices) * numindices);
    alias_frames = Z_Mallocz(sizeof(*alias_frames) * header.num_frames);

    for (uint32_t i = 0; i < numindices; i++)
        indices[i] = final_indices[i];

    float scale_s = 1.0f / header.skinwidth;
    float scale_t = 1.0f / header.skinheight;
    for (uint32_t frame = 0; frame < header.num_frames; frame++) {
        const dmd2frame_t *src_frame =
            (const dmd2frame_t *)(rawdata + header.ofs_frames + frame * header.framesize);
        vec3_t scale, translate, radius_mins, radius_maxs;

        LittleVector(src_frame->scale, scale);
        LittleVector(src_frame->translate, translate);
        ClearBounds(radius_mins, radius_maxs);
        ClearBounds(alias_frames[frame].bounds[0], alias_frames[frame].bounds[1]);

        for (uint32_t i = 0; i < numindices; i++) {
            if (remap[i] != i)
                continue;

            const dmd2trivertx_t *src_vert = &src_frame->verts[vert_indices[i]];
            vk_vertex_t *dst = &vertices[frame * numverts + final_indices[i]];
            vec3_t relative;

            relative[0] = src_vert->v[0] * scale[0];
            relative[1] = src_vert->v[1] * scale[1];
            relative[2] = src_vert->v[2] * scale[2];
            VectorAdd(relative, translate, dst->position);
            AddPointToBounds(relative, radius_mins, radius_maxs);
            AddPointToBounds(dst->position, alias_frames[frame].bounds[0],
                             alias_frames[frame].bounds[1]);
            dst->color[0] = 1.0f;
            dst->color[1] = 1.0f;
            dst->color[2] = 1.0f;
            dst->color[3] = 1.0f;
            dst->uv[0] = (int16_t)LittleShort(src_tc[tc_indices[i]].s) * scale_s;
            dst->uv[1] = (int16_t)LittleShort(src_tc[tc_indices[i]].t) * scale_t;
            if (src_vert->lightnormalindex < NUMVERTEXNORMALS)
                VectorCopy(bytedirs[src_vert->lightnormalindex], dst->normal);
            else
                VectorSet(dst->normal, 0.0f, 0.0f, 1.0f);
        }

        alias_frames[frame].radius = RadiusFromBounds(radius_mins, radius_maxs);
    }

    model = vk_alloc_model();
    if (!model)
        goto out;

    Q_strlcpy(model->name, name, sizeof(model->name));
    model->type = VK_MODEL_ALIAS;
    model->registration_sequence = r_registration_sequence;
    model->frame_count = header.num_frames;
    model->alias_frames = alias_frames;
    alias_frames = NULL;
    model->alias_batch_count = 1;
    model->alias_batches = Z_Mallocz(sizeof(model->alias_batches[0]));
    model->alias_batches[0] = (vk_alias_batch_t) {
        .first_index = 0,
        .index_count = numindices,
        .skin_start = 0,
        .skin_count = header.num_skins,
    };
    model->vertex_count = numverts;
    model->skin_count = header.num_skins;
    if (model->skin_count) {
        model->skins = Z_Mallocz(sizeof(model->skins[0]) * model->skin_count);
        model->skin_names = Z_Mallocz(sizeof(model->skin_names[0]) * model->skin_count);
    }

    const char *src_skin = (const char *)rawdata + header.ofs_skins;
    for (int i = 0; i < model->skin_count; i++) {
        char skin_name[MD2_MAX_SKINNAME];

        if (!Q_memccpy(skin_name, src_skin, 0, sizeof(skin_name)))
            model->skins[i] = R_NOTEXTURE;
        else {
            Q_strlcpy(model->skin_names[i], skin_name,
                      sizeof(model->skin_names[i]));
            model->skins[i] = IMG_Find(skin_name, IT_SKIN, IF_NONE);
        }
        src_skin += MD2_MAX_SKINNAME;
    }

    if (!vk_upload_mesh(&model->mesh, vertices, numverts * header.num_frames,
                        indices, numindices)) {
        vk_free_model(model);
        goto out;
    }

    line_indices = vk_build_line_indices(indices, numindices, &line_index_count);
    if (!line_indices ||
        !vk_upload_buffer(&model->alias_line_indices, line_indices,
                          sizeof(*line_indices) * line_index_count,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        vk_free_model(model);
        goto out;
    }
    model->alias_line_index_count = line_index_count;

#if USE_MD5
    vk_load_md5_variant(model);
#endif

    handle = (model - vk.models) + 1;

out:
    if (vert_indices)
        Z_Free(vert_indices);
    if (tc_indices)
        Z_Free(tc_indices);
    if (final_indices)
        Z_Free(final_indices);
    if (remap)
        Z_Free(remap);
    if (vertices)
        Z_Free(vertices);
    if (indices)
        Z_Free(indices);
    if (line_indices)
        Z_Free(line_indices);
    if (alias_frames)
        Z_Free(alias_frames);
    return handle;
}

#if USE_MD3
static bool vk_check_md3_bounds(const dmd3header_t *header, size_t length)
{
    if (header->num_frames < 1 || header->num_frames > MD3_MAX_FRAMES ||
        header->num_meshes < 1 || header->num_meshes > MD3_MAX_MESHES)
        return false;
    if ((uint64_t)header->ofs_frames +
        header->num_frames * sizeof(dmd3frame_t) > length)
        return false;
    if (header->ofs_meshes > length)
        return false;

    return true;
}

static bool vk_check_md3_mesh_bounds(const dmd3mesh_t *mesh, uint32_t frame_count,
                                     size_t length)
{
    if (mesh->ident != MD3_IDENT)
        return false;
    if (mesh->meshsize < sizeof(*mesh) || mesh->meshsize > length)
        return false;
    if (mesh->num_frames != frame_count)
        return false;
    if (mesh->num_verts < 3 || mesh->num_verts > MD3_MAX_VERTS ||
        mesh->num_tris < 1 || mesh->num_tris > MD3_MAX_TRIANGLES ||
        mesh->num_skins > MD3_MAX_SKINS)
        return false;
    if ((uint64_t)mesh->ofs_indexes +
        mesh->num_tris * 3 * sizeof(uint32_t) > mesh->meshsize ||
        (uint64_t)mesh->ofs_skins +
        mesh->num_skins * sizeof(dmd3skin_t) > mesh->meshsize ||
        (uint64_t)mesh->ofs_tcs +
        mesh->num_verts * sizeof(dmd3coord_t) > mesh->meshsize ||
        (uint64_t)mesh->ofs_verts +
        mesh->num_verts * frame_count * sizeof(dmd3vertex_t) > mesh->meshsize)
        return false;

    return true;
}

static void vk_md3_normal(uint8_t lat_byte, uint8_t lng_byte, vec3_t normal)
{
    float lat = lat_byte * (2.0f * M_PIf / 255.0f);
    float lng = lng_byte * (2.0f * M_PIf / 255.0f);

    normal[0] = sinf(lat) * cosf(lng);
    normal[1] = sinf(lat) * sinf(lng);
    normal[2] = cosf(lat);
}

static qhandle_t vk_load_md3_model(const char *name, const byte *rawdata, size_t length)
{
    typedef struct {
        dmd3mesh_t header;
        const byte *data;
        uint32_t vertex_base;
        uint32_t first_index;
        int skin_start;
    } vk_md3_mesh_info_t;

    dmd3header_t header;
    vk_md3_mesh_info_t mesh_info[MD3_MAX_MESHES];
    vk_vertex_t *vertices = NULL;
    uint32_t *indices = NULL;
    uint32_t *line_indices = NULL;
    uint32_t line_index_count = 0;
    vk_alias_frame_t *alias_frames = NULL;
    vk_alias_batch_t *batches = NULL;
    image_t **skins = NULL;
    maliasskinname_t *skin_names = NULL;
    vk_model_t *model = NULL;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    int skin_count = 0;
    qhandle_t handle = 0;

    if (length < sizeof(header))
        return 0;

    const dmd3header_t *src_header = (const dmd3header_t *)rawdata;
    header.ident = LittleLong(src_header->ident);
    header.version = LittleLong(src_header->version);
    header.flags = LittleLong(src_header->flags);
    header.num_frames = LittleLong(src_header->num_frames);
    header.num_tags = LittleLong(src_header->num_tags);
    header.num_meshes = LittleLong(src_header->num_meshes);
    header.num_skins = LittleLong(src_header->num_skins);
    header.ofs_frames = LittleLong(src_header->ofs_frames);
    header.ofs_tags = LittleLong(src_header->ofs_tags);
    header.ofs_meshes = LittleLong(src_header->ofs_meshes);
    header.ofs_end = LittleLong(src_header->ofs_end);

    if (header.ident != MD3_IDENT || header.version != MD3_VERSION ||
        !vk_check_md3_bounds(&header, length))
        return 0;

    const byte *mesh_data = rawdata + header.ofs_meshes;
    size_t remaining = length - header.ofs_meshes;
    for (uint32_t i = 0; i < header.num_meshes; i++) {
        if (remaining < sizeof(dmd3mesh_t))
            goto out;

        const dmd3mesh_t *src_mesh = (const dmd3mesh_t *)mesh_data;
        dmd3mesh_t *mesh = &mesh_info[i].header;

        mesh->ident = LittleLong(src_mesh->ident);
        mesh->flags = LittleLong(src_mesh->flags);
        mesh->num_frames = LittleLong(src_mesh->num_frames);
        mesh->num_skins = LittleLong(src_mesh->num_skins);
        mesh->num_verts = LittleLong(src_mesh->num_verts);
        mesh->num_tris = LittleLong(src_mesh->num_tris);
        mesh->ofs_indexes = LittleLong(src_mesh->ofs_indexes);
        mesh->ofs_skins = LittleLong(src_mesh->ofs_skins);
        mesh->ofs_tcs = LittleLong(src_mesh->ofs_tcs);
        mesh->ofs_verts = LittleLong(src_mesh->ofs_verts);
        mesh->meshsize = LittleLong(src_mesh->meshsize);

        if (!vk_check_md3_mesh_bounds(mesh, header.num_frames, remaining))
            goto out;
        if (UINT32_MAX - vertex_count < mesh->num_verts ||
            UINT32_MAX - index_count < mesh->num_tris * 3)
            goto out;
        if (skin_count > INT_MAX - (int)mesh->num_skins)
            goto out;

        mesh_info[i].data = mesh_data;
        mesh_info[i].vertex_base = vertex_count;
        mesh_info[i].first_index = index_count;
        mesh_info[i].skin_start = skin_count;
        vertex_count += mesh->num_verts;
        index_count += mesh->num_tris * 3;
        skin_count += mesh->num_skins;

        mesh_data += mesh->meshsize;
        remaining -= mesh->meshsize;
    }

    if (!vertex_count || !index_count ||
        (uint64_t)vertex_count * header.num_frames > UINT32_MAX)
        goto out;

    vertices = Z_Malloc(sizeof(*vertices) * vertex_count * header.num_frames);
    indices = Z_Malloc(sizeof(*indices) * index_count);
    alias_frames = Z_Mallocz(sizeof(*alias_frames) * header.num_frames);
    batches = Z_Mallocz(sizeof(*batches) * header.num_meshes);
    if (skin_count) {
        skins = Z_Mallocz(sizeof(*skins) * skin_count);
        skin_names = Z_Mallocz(sizeof(*skin_names) * skin_count);
    }

    const dmd3frame_t *src_frame =
        (const dmd3frame_t *)(rawdata + header.ofs_frames);
    for (uint32_t frame = 0; frame < header.num_frames; frame++)
        ClearBounds(alias_frames[frame].bounds[0],
                    alias_frames[frame].bounds[1]);

    for (uint32_t mesh_index = 0; mesh_index < header.num_meshes; mesh_index++) {
        const vk_md3_mesh_info_t *info = &mesh_info[mesh_index];
        const dmd3mesh_t *mesh = &info->header;
        const dmd3coord_t *src_tc =
            (const dmd3coord_t *)(info->data + mesh->ofs_tcs);
        const uint32_t *src_index =
            (const uint32_t *)(info->data + mesh->ofs_indexes);
        const dmd3skin_t *src_skin =
            (const dmd3skin_t *)(info->data + mesh->ofs_skins);

        batches[mesh_index] = (vk_alias_batch_t) {
            .first_index = info->first_index,
            .index_count = mesh->num_tris * 3,
            .skin_start = info->skin_start,
            .skin_count = mesh->num_skins,
        };

        for (uint32_t skin = 0; skin < mesh->num_skins; skin++) {
            char skin_name[MD3_MAX_PATH];

            if (!Q_memccpy(skin_name, src_skin[skin].name, 0, sizeof(skin_name)))
                skins[info->skin_start + skin] = R_NOTEXTURE;
            else {
                Q_strlcpy(skin_names[info->skin_start + skin], skin_name,
                          sizeof(skin_names[info->skin_start + skin]));
                skins[info->skin_start + skin] =
                    IMG_Find(skin_name, IT_SKIN, IF_NONE);
            }
        }

        for (uint32_t i = 0; i < mesh->num_tris * 3; i++) {
            uint32_t index = LittleLong(src_index[i]);

            if (index >= mesh->num_verts)
                goto out;
            indices[info->first_index + i] = info->vertex_base + index;
        }

        for (uint32_t frame = 0; frame < header.num_frames; frame++) {
            const dmd3frame_t *frame_info =
                (const dmd3frame_t *)(rawdata + header.ofs_frames) + frame;
            const dmd3vertex_t *src_vert =
                (const dmd3vertex_t *)(info->data + mesh->ofs_verts) +
                frame * mesh->num_verts;
            vec3_t translate;

            LittleVector(frame_info->translate, translate);
            for (uint32_t vert = 0; vert < mesh->num_verts; vert++) {
                vk_vertex_t *dst =
                    &vertices[frame * vertex_count + info->vertex_base + vert];
                vec3_t local;

                local[0] =
                    (int16_t)LittleShort(src_vert[vert].point[0]) * MD3_XYZ_SCALE;
                local[1] =
                    (int16_t)LittleShort(src_vert[vert].point[1]) * MD3_XYZ_SCALE;
                local[2] =
                    (int16_t)LittleShort(src_vert[vert].point[2]) * MD3_XYZ_SCALE;
                VectorAdd(local, translate, dst->position);
                AddPointToBounds(local, alias_frames[frame].bounds[0],
                                 alias_frames[frame].bounds[1]);
                dst->color[0] = 1.0f;
                dst->color[1] = 1.0f;
                dst->color[2] = 1.0f;
                dst->color[3] = 1.0f;
                dst->uv[0] = LittleFloat(src_tc[vert].st[0]);
                dst->uv[1] = LittleFloat(src_tc[vert].st[1]);
                vk_md3_normal(src_vert[vert].norm[0], src_vert[vert].norm[1],
                              dst->normal);
            }
        }
    }

    // Match OpenGL by deriving bounds from decoded mesh vertices. MD3 frame
    // header bounds are exporter metadata and are not necessarily reliable.
    for (uint32_t frame = 0; frame < header.num_frames; frame++) {
        vec3_t translate;

        alias_frames[frame].radius =
            RadiusFromBounds(alias_frames[frame].bounds[0],
                             alias_frames[frame].bounds[1]);
        LittleVector(src_frame[frame].translate, translate);
        VectorAdd(alias_frames[frame].bounds[0], translate,
                  alias_frames[frame].bounds[0]);
        VectorAdd(alias_frames[frame].bounds[1], translate,
                  alias_frames[frame].bounds[1]);
    }

    model = vk_alloc_model();
    if (!model)
        goto out;

    Q_strlcpy(model->name, name, sizeof(model->name));
    model->type = VK_MODEL_ALIAS;
    model->registration_sequence = r_registration_sequence;
    model->frame_count = header.num_frames;
    model->alias_frames = alias_frames;
    alias_frames = NULL;
    model->alias_batch_count = header.num_meshes;
    model->alias_batches = batches;
    batches = NULL;
    model->vertex_count = vertex_count;
    model->skin_count = skin_count;
    model->skins = skins;
    skins = NULL;
    model->skin_names = skin_names;
    skin_names = NULL;

    if (!vk_upload_mesh(&model->mesh, vertices, vertex_count * header.num_frames,
                        indices, index_count)) {
        vk_free_model(model);
        goto out;
    }

    line_indices = vk_build_line_indices(indices, index_count, &line_index_count);
    if (!line_indices ||
        !vk_upload_buffer(&model->alias_line_indices, line_indices,
                          sizeof(*line_indices) * line_index_count,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        vk_free_model(model);
        goto out;
    }
    model->alias_line_index_count = line_index_count;

#if USE_MD5
    vk_load_md5_variant(model);
#endif

    handle = (model - vk.models) + 1;

out:
    if (vertices)
        Z_Free(vertices);
    if (indices)
        Z_Free(indices);
    if (line_indices)
        Z_Free(line_indices);
    if (alias_frames)
        Z_Free(alias_frames);
    if (batches)
        Z_Free(batches);
    if (skins)
        Z_Free(skins);
    if (skin_names)
        Z_Free(skin_names);

    return handle;
}
#endif

static bool vk_upload_mesh(vk_mesh_t *mesh, const vk_vertex_t *vertices,
                           uint32_t vertex_count, const uint32_t *indices,
                           uint32_t index_count)
{
    vk_mesh_t uploaded = { 0 };

    if (!vertex_count || !index_count)
        return true;

    VkBufferUsageFlags vertex_usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VkBufferUsageFlags index_usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
#if USE_VULKAN_RAYTRACING
    if (vk.raytracing_active) {
        vertex_usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        index_usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
#endif
    if (!vk_upload_buffer(&uploaded.vertices, vertices,
                          sizeof(*vertices) * vertex_count, vertex_usage))
        goto fail;
    if (!vk_upload_buffer(&uploaded.indices, indices,
                          sizeof(*indices) * index_count, index_usage))
        goto fail;

    uploaded.vertex_count = vertex_count;
    uploaded.index_count = index_count;
    vk_destroy_mesh(mesh);
    *mesh = uploaded;
    return true;

fail:
    vk_destroy_mesh(&uploaded);
    return false;
}

#if USE_VULKAN_RAYTRACING
static VkDeviceAddress vk_buffer_device_address(VkBuffer buffer)
{
    VkBufferDeviceAddressInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer,
    };
    return vk.GetBufferDeviceAddress(vk.device, &info);
}

static void vk_rt_prefix_error(const char *stage)
{
    char detail[MAX_STRING_CHARS];
    Q_strlcpy(detail, Com_GetLastError(), sizeof(detail));
    Com_SetLastError(va("%s: %s", stage,
                        detail[0] ? detail : "unspecified Vulkan failure"));
}

static void vk_destroy_acceleration_structure(vk_acceleration_structure_t *as)
{
    if (as->handle && vk.DestroyAccelerationStructureKHR)
        vk.DestroyAccelerationStructureKHR(vk.device, as->handle, NULL);
    as->handle = VK_NULL_HANDLE;
    vk_destroy_buffer(&as->storage);
}

static bool vk_build_mesh_blas(vk_acceleration_structure_t *as,
                               const vk_buffer_t *vertices,
                               uint32_t vertex_count,
                               const vk_buffer_t *indices,
                               uint32_t index_count)
{
    if (!vk.raytracing_active || !vertices || !vertices->buffer ||
        !indices || !indices->buffer || vertex_count < 3 || index_count < 3)
        return true;

    VkAccelerationStructureGeometryKHR geometry = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
        .geometry.triangles = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData.deviceAddress = vk_buffer_device_address(vertices->buffer),
            .vertexStride = sizeof(vk_vertex_t),
            .maxVertex = vertex_count - 1,
            .indexType = VK_INDEX_TYPE_UINT32,
            .indexData.deviceAddress = vk_buffer_device_address(indices->buffer),
        },
    };
    if (!geometry.geometry.triangles.vertexData.deviceAddress ||
        !geometry.geometry.triangles.indexData.deviceAddress) {
        Com_SetLastError("BLAS vertex or index buffer has no device address");
        return false;
    }
    uint32_t primitive_count = index_count / 3;
    VkAccelerationStructureBuildGeometryInfoKHR build = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry,
    };
    VkAccelerationStructureBuildSizesInfoKHR sizes = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    vk.GetAccelerationStructureBuildSizesKHR(
        vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &build, &primitive_count, &sizes);

    vk_acceleration_structure_t created = { 0 };
    if (!sizes.accelerationStructureSize || !sizes.buildScratchSize) {
        Com_SetLastError("Vulkan returned empty BLAS build sizes");
        goto fail;
    }
    if (!vk_create_buffer(sizes.accelerationStructureSize,
                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &created.storage.buffer, &created.storage.memory))
        goto fail;
    created.storage.size = sizes.accelerationStructureSize;

    VkAccelerationStructureCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = created.storage.buffer,
        .size = sizes.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
    };
    VkResult result = vk.CreateAccelerationStructureKHR(
        vk.device, &create_info, NULL, &created.handle);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkCreateAccelerationStructureKHR", result);
        goto fail;
    }

    vk_buffer_t scratch = { 0 };
    if (!vk_create_buffer(sizes.buildScratchSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &scratch.buffer, &scratch.memory))
        goto fail;
    scratch.size = sizes.buildScratchSize;
    build.dstAccelerationStructure = created.handle;
    build.scratchData.deviceAddress = vk_buffer_device_address(scratch.buffer);
    if (!build.scratchData.deviceAddress) {
        Com_SetLastError("BLAS scratch buffer has no device address");
        vk_destroy_buffer(&scratch);
        goto fail;
    }
    VkAccelerationStructureBuildRangeInfoKHR range = {
        .primitiveCount = primitive_count,
    };
    const VkAccelerationStructureBuildRangeInfoKHR *ranges[] = { &range };
    VkCommandBuffer cmd;
    if (!vk_begin_immediate(&cmd)) {
        vk_destroy_buffer(&scratch);
        goto fail;
    }
    vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &build, ranges);
    if (!vk_end_immediate(cmd)) {
        vk_destroy_buffer(&scratch);
        goto fail;
    }
    vk_destroy_buffer(&scratch);

    vk_destroy_acceleration_structure(as);
    *as = created;
    return true;

fail:
    vk_destroy_acceleration_structure(&created);
    return false;
}

static bool vk_build_world_tlas(void)
{
    if (!vk.raytracing_active || !vk.world.rt_blas.handle)
        return true;

    VkAccelerationStructureDeviceAddressInfoKHR address_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = vk.world.rt_blas.handle,
    };
    VkDeviceAddress blas_address =
        vk.GetAccelerationStructureDeviceAddressKHR(vk.device, &address_info);
    if (!blas_address) {
        Com_SetLastError("World BLAS has no device address");
        return false;
    }

    VkAccelerationStructureInstanceKHR instance = {
        .transform = { .matrix = {
            { 1.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f, 0.0f },
        } },
        .instanceCustomIndex = 0,
        .mask = 0xff,
        .instanceShaderBindingTableRecordOffset = 0,
        .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
        .accelerationStructureReference = blas_address,
    };
    VkBufferUsageFlags instance_usage =
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!vk_upload_buffer(&vk.rt_instance_buffer, &instance, sizeof(instance),
                          instance_usage))
        return false;

    VkAccelerationStructureGeometryKHR geometry = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry.instances = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .arrayOfPointers = VK_FALSE,
            .data.deviceAddress =
                vk_buffer_device_address(vk.rt_instance_buffer.buffer),
        },
    };
    uint32_t primitive_count = 1;
    VkAccelerationStructureBuildGeometryInfoKHR build = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry,
    };
    VkAccelerationStructureBuildSizesInfoKHR sizes = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    vk.GetAccelerationStructureBuildSizesKHR(
        vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &build, &primitive_count, &sizes);

    vk_acceleration_structure_t created = { 0 };
    vk_buffer_t scratch = { 0 };
    if (!sizes.accelerationStructureSize || !sizes.buildScratchSize) {
        Com_SetLastError("Vulkan returned empty TLAS build sizes");
        goto fail;
    }
    if (!vk_create_buffer(sizes.accelerationStructureSize,
                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &created.storage.buffer, &created.storage.memory))
        goto fail;
    created.storage.size = sizes.accelerationStructureSize;
    VkAccelerationStructureCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = created.storage.buffer,
        .size = sizes.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
    };
    VkResult result = vk.CreateAccelerationStructureKHR(
        vk.device, &create_info, NULL, &created.handle);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkCreateAccelerationStructureKHR(TLAS)", result);
        goto fail;
    }
    if (!vk_create_buffer(sizes.buildScratchSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &scratch.buffer, &scratch.memory))
        goto fail;
    scratch.size = sizes.buildScratchSize;
    build.dstAccelerationStructure = created.handle;
    build.scratchData.deviceAddress = vk_buffer_device_address(scratch.buffer);
    if (!build.scratchData.deviceAddress) {
        Com_SetLastError("TLAS scratch buffer has no device address");
        goto fail;
    }
    VkAccelerationStructureBuildRangeInfoKHR range = {
        .primitiveCount = primitive_count,
    };
    const VkAccelerationStructureBuildRangeInfoKHR *ranges[] = { &range };
    VkCommandBuffer cmd;
    if (!vk_begin_immediate(&cmd))
        goto fail;
    vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &build, ranges);
    if (!vk_end_immediate(cmd))
        goto fail;
    vk_destroy_buffer(&scratch);

    vk_destroy_acceleration_structure(&vk.rt_tlas);
    vk.rt_tlas = created;
    VkWriteDescriptorSetAccelerationStructureKHR as_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &vk.rt_tlas.handle,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = &as_write,
        .dstSet = vk.rt_descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
    };
    vk.UpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    return true;

fail:
    vk_destroy_buffer(&scratch);
    vk_destroy_acceleration_structure(&created);
    return false;
}
#endif

static uint32_t *vk_build_line_indices(const uint32_t *indices,
                                       uint32_t index_count,
                                       uint32_t *line_index_count)
{
    uint32_t *line_indices;

    *line_index_count = 0;
    if (!indices || index_count < 3)
        return NULL;
    if (index_count > UINT32_MAX / 2)
        return NULL;

    *line_index_count = (index_count / 3) * 6;
    line_indices = Z_Malloc(sizeof(*line_indices) * *line_index_count);

    for (uint32_t i = 0, j = 0; i + 2 < index_count; i += 3) {
        uint32_t a = indices[i + 0];
        uint32_t b = indices[i + 1];
        uint32_t c = indices[i + 2];

        line_indices[j++] = a;
        line_indices[j++] = b;
        line_indices[j++] = b;
        line_indices[j++] = c;
        line_indices[j++] = c;
        line_indices[j++] = a;
    }

    return line_indices;
}

static void vk_texture_barrier(VkCommandBuffer cmd, VkImage image,
                               VkImageLayout old_layout, VkImageLayout new_layout,
                               VkAccessFlags src_access, VkAccessFlags dst_access,
                               VkPipelineStageFlags src_stage,
                               VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vk.CmdPipelineBarrier(cmd, src_stage, dst_stage,
                          0, 0, NULL, 0, NULL, 1, &barrier);
}

static void vk_texture_mip_barrier(VkCommandBuffer cmd, VkImage image,
                                   uint32_t mip_levels,
                                   VkImageLayout old_layout,
                                   VkImageLayout new_layout,
                                   VkAccessFlags src_access,
                                   VkAccessFlags dst_access,
                                   VkPipelineStageFlags src_stage,
                                   VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = mip_levels,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    vk.CmdPipelineBarrier(cmd, src_stage, dst_stage,
                          0, 0, NULL, 0, NULL, 1, &barrier);
}

static void vk_update_texture_descriptor_with_sampler(vk_texture_t *texture,
                                                      VkSampler sampler)
{
    if (!texture->descriptor_set || !texture->view || !sampler)
        return;

    VkDescriptorImageInfo image_info = {
        .sampler = sampler,
        .imageView = texture->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = texture->descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
    };

    vk.UpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
}

static void vk_update_texture_descriptor(vk_texture_t *texture)
{
    vk_update_texture_descriptor_with_sampler(texture, vk.sampler);
}

static bool vk_create_color_target(vk_texture_t *texture, uint32_t width,
                                   uint32_t height, VkFormat format,
                                   VkImageUsageFlags extra_usage)
{
    vk_texture_t target = { 0 };
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT | extra_usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkResult result = vk.CreateImage(vk.device, &image_info, NULL, &target.image);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkCreateImage", result);
        goto fail;
    }

    VkMemoryRequirements req;
    vk.GetImageMemoryRequirements(vk.device, target.image, &req);
    uint32_t memory_type = vk_find_memory_type(req.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("No suitable Vulkan color target memory type");
        goto fail;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_type,
    };
    result = vk.AllocateMemory(vk.device, &alloc_info, NULL, &target.memory);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkAllocateMemory", result);
        goto fail;
    }

    result = vk.BindImageMemory(vk.device, target.image, target.memory, 0);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkBindImageMemory", result);
        goto fail;
    }

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = target.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    result = vk.CreateImageView(vk.device, &view_info, NULL, &target.view);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkCreateImageView", result);
        goto fail;
    }

    VkDescriptorSetAllocateInfo descriptor_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vk.texture_set_layout,
    };
    result = vk.AllocateDescriptorSets(vk.device, &descriptor_info,
                                       &target.descriptor_set);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkAllocateDescriptorSets", result);
        goto fail;
    }

    target.width = width;
    target.height = height;
    vk_update_texture_descriptor(&target);

    vk_destroy_texture_resource(texture);
    *texture = target;
    return true;

fail:
    vk_destroy_texture_resource(&target);
    return false;
}

#if USE_VULKAN_RAYTRACING
typedef struct {
    uint32_t ao_samples;
    uint32_t emissive_samples;
    uint32_t atlas_width;
    uint32_t seed;
} vk_rt_ao_push_t;

static bool vk_create_rt_ao_pipeline(void)
{
    if (vk.rt_ao_pipeline && vk.rt_ao_render_pass &&
        vk.rt_ao_pipeline_layout)
        return true;
    if (vk.rt_ao_pipeline) {
        vk.DestroyPipeline(vk.device, vk.rt_ao_pipeline, NULL);
        vk.rt_ao_pipeline = VK_NULL_HANDLE;
    }
    if (vk.rt_ao_pipeline_layout) {
        vk.DestroyPipelineLayout(vk.device, vk.rt_ao_pipeline_layout, NULL);
        vk.rt_ao_pipeline_layout = VK_NULL_HANDLE;
    }
    if (vk.rt_ao_render_pass) {
        vk.DestroyRenderPass(vk.device, vk.rt_ao_render_pass, NULL);
        vk.rt_ao_render_pass = VK_NULL_HANDLE;
    }

    VkAttachmentDescription attachment = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
    };
    VkSubpassDependency dependencies[] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        },
    };
    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = q_countof(dependencies),
        .pDependencies = dependencies,
    };
    VkResult result = vk.CreateRenderPass(vk.device, &render_pass_info, NULL,
                                          &vk.rt_ao_render_pass);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateRenderPass(rt_ao)", result);

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(vk_rt_ao_push_t),
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &vk.rt_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    result = vk.CreatePipelineLayout(vk.device, &layout_info, NULL,
                                     &vk.rt_ao_pipeline_layout);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreatePipelineLayout(rt_ao)", result);

    VkShaderModule vert = vk_create_shader_module(vk_rt_ao_bake_vert_spv,
                                                  sizeof(vk_rt_ao_bake_vert_spv));
    VkShaderModule frag = vk_create_shader_module(vk_rt_ao_bake_frag_spv,
                                                  sizeof(vk_rt_ao_bake_frag_spv));
    if (!vert || !frag) {
        if (vert)
            vk.DestroyShaderModule(vk.device, vert, NULL);
        if (frag)
            vk.DestroyShaderModule(vk.device, frag, NULL);
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main",
        },
    };
    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(vk_rt_bake_vertex_t),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attributes[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_rt_bake_vertex_t, position),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_rt_bake_vertex_t, normal),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_rt_bake_vertex_t, uv),
        },
        {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32G32_UINT,
            .offset = offsetof(vk_rt_bake_vertex_t, rt_data),
        },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = q_countof(attributes),
        .pVertexAttributeDescriptions = attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                          VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = q_countof(dynamic_states),
        .pDynamicStates = dynamic_states,
    };
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = vk.rt_ao_pipeline_layout,
        .renderPass = vk.rt_ao_render_pass,
    };
    result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                        &pipeline_info, NULL,
                                        &vk.rt_ao_pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines(rt_ao)", result);
    return true;
}

static bool vk_world_face_is_inline(const bsp_t *bsp, const mface_t *face)
{
    for (int i = 1; i < bsp->nummodels; i++) {
        const mmodel_t *model = &bsp->models[i];
        if (face >= model->firstface && face < model->firstface + model->numfaces)
            return true;
    }
    return false;
}

static void vk_update_rt_ao_descriptor(void)
{
    VkImageView view = vk.world.rt_ao_ready ? vk.world.rt_ao_texture.view :
        vk.world.pixel_lightmap_texture.view;
    if (!vk.rt_descriptor_set || !view || !vk.postprocess_sampler)
        return;

    VkDescriptorImageInfo image_info = {
        .sampler = vk.postprocess_sampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = vk.rt_descriptor_set,
        .dstBinding = 2,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
    };
    vk.UpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
}

static void vk_rt_static_image_barrier(VkCommandBuffer cmd, VkImage image,
                                       VkImageLayout old_layout,
                                       VkImageLayout new_layout,
                                       VkAccessFlags src_access,
                                       VkAccessFlags dst_access,
                                       VkPipelineStageFlags src_stage,
                                       VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vk.CmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL,
                          1, &barrier);
}

static bool vk_read_rt_static_stats(uint32_t width, uint32_t height)
{
    VkDeviceSize size = (VkDeviceSize)width * height * 4;
    vk_buffer_t readback = { 0 };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    void *mapped = NULL;
    bool ok = false;

    if (!size || !vk.world.rt_ao_texture.image ||
        !vk_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &readback.buffer, &readback.memory))
        return false;
    readback.size = size;

    if (!vk_begin_immediate(&cmd))
        goto out;
    vk_rt_static_image_barrier(cmd, vk.world.rt_ao_texture.image,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy = {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .imageExtent = { width, height, 1 },
    };
    vk.CmdCopyImageToBuffer(cmd, vk.world.rt_ao_texture.image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            readback.buffer, 1, &copy);
    vk_rt_static_image_barrier(cmd, vk.world.rt_ao_texture.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    if (!vk_end_immediate(cmd)) {
        cmd = VK_NULL_HANDLE;
        goto out;
    }
    cmd = VK_NULL_HANDLE;

    VkResult result = vk.MapMemory(vk.device, readback.memory, 0, size, 0,
                                   &mapped);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkMapMemory(RT static readback)", result);
        goto out;
    }

    const byte *pixels = mapped;
    uint64_t pixel_count = (uint64_t)width * height;
    uint32_t coverage = 0, occluded = 0, emissive = 0;
    byte ao_peak = 0, emissive_peak = 0;
    for (uint64_t i = 0; i < pixel_count; i++, pixels += 4) {
        if (!pixels[3])
            continue;
        coverage++;
        // Alpha zero is reserved for uncovered atlas space. Valid AO uses
        // 1..255 and is decoded back to 0..1 in the world shader.
        byte ao = pixels[3] - 1;
        if (ao) {
            occluded++;
            ao_peak = max(ao_peak, ao);
        }
        byte peak = max(pixels[0], max(pixels[1], pixels[2]));
        if (peak) {
            emissive++;
            emissive_peak = max(emissive_peak, peak);
        }
    }
    vk.world.rt_coverage_texels = coverage;
    vk.world.rt_occluded_texels = occluded;
    vk.world.rt_emissive_texels = emissive;
    vk.world.rt_ao_peak = ao_peak;
    vk.world.rt_emissive_peak = emissive_peak;
    ok = true;

out:
    if (mapped)
        vk.UnmapMemory(vk.device, readback.memory);
    if (cmd)
        vk.FreeCommandBuffers(vk.device, vk.command_pool, 1, &cmd);
    vk_destroy_buffer(&readback);
    return ok;
}

static bool vk_bake_world_rt_ao(const bsp_t *bsp,
                                const vk_world_face_t *faces,
                                uint32_t face_count,
                                const vk_vertex_t *vertices,
                                const uint32_t *indices,
                                const float *lmuv_data)
{
    vk.world.rt_ao_ready = false;
    vk.world.rt_ao_samples = 0;
    vk.world.rt_emissive_samples = 0;
    vk.world.rt_bake_msec = 0;
    vk.world.rt_coverage_texels = 0;
    vk.world.rt_occluded_texels = 0;
    vk.world.rt_emissive_texels = 0;
    vk.world.rt_ao_peak = 0;
    vk.world.rt_emissive_peak = 0;
    if (!vk.raytracing_active || !vk.rt_tlas.handle || !bsp || !faces ||
        !face_count || !vertices || !indices || !lmuv_data ||
        !vk.world.pixel_lightmap_texture.width ||
        !vk.world.pixel_lightmap_texture.height) {
        Com_SetLastError("Pixel-lightmap data is unavailable for RT static lighting");
        return false;
    }
    uint64_t bake_start = vk_time_usec();

    VkFormatProperties properties;
    vk.GetPhysicalDeviceFormatProperties(vk.physical_device,
                                         VK_FORMAT_R8G8B8A8_UNORM, &properties);
    VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((properties.optimalTilingFeatures & required) != required) {
        Com_SetLastError("RGBA8 RT lighting atlas is not supported as a sampled color target");
        return false;
    }

    uint32_t atlas_width = min(vk.world.pixel_lightmap_texture.width, 2048u);
    uint32_t atlas_height = min(vk.world.pixel_lightmap_texture.height, 2048u);
    uint32_t bake_vertex_count = 0;
    uint32_t bake_index_count = 0;
    uint64_t valid_texels = 0;
    uint64_t lit_texels = 0;
    double area_scale = ((double)atlas_width /
                         vk.world.pixel_lightmap_texture.width) *
                        ((double)atlas_height /
                         vk.world.pixel_lightmap_texture.height);
    for (uint32_t i = 0; i < face_count; i++) {
        const vk_world_face_t *face = &faces[i];
        if (!face->face || !face->pixel_lm_w || !face->pixel_lm_h ||
            (face->face->drawflags & SURF_TRANS_MASK) ||
            vk_world_face_is_inline(bsp, face->face))
            continue;
        bake_vertex_count += face->edge_count;
        bake_index_count += face->index_count;
        valid_texels += max((uint64_t)((double)face->pixel_lm_w *
                                      face->pixel_lm_h * area_scale), 1u);
        if (face->rt_light_count)
            lit_texels += max((uint64_t)((double)face->pixel_lm_w *
                                        face->pixel_lm_h * area_scale), 1u);
    }
    if (!bake_vertex_count || !bake_index_count || !valid_texels) {
        Com_SetLastError("No lightmapped opaque world triangles for RT static lighting");
        return false;
    }

    uint32_t ao_samples = (uint32_t)(4194304ull / valid_texels);
    ao_samples = Q_clip(ao_samples, 1u, 4u);
    uint32_t emissive_samples = lit_texels ?
        Q_clip((uint32_t)(4194304ull / lit_texels), 1u, 2u) : 0u;
    vk_rt_bake_vertex_t *bake_vertices =
        Z_Mallocz((size_t)bake_vertex_count * sizeof(*bake_vertices));
    uint32_t *bake_indices =
        Z_Malloc((size_t)bake_index_count * sizeof(*bake_indices));
    uint32_t dst_vertex = 0;
    uint32_t dst_index = 0;

    for (uint32_t i = 0; i < face_count; i++) {
        const vk_world_face_t *face = &faces[i];
        if (!face->face || !face->pixel_lm_w || !face->pixel_lm_h ||
            (face->face->drawflags & SURF_TRANS_MASK) ||
            vk_world_face_is_inline(bsp, face->face))
            continue;

        vec3_t normal;
        VectorCopy(face->face->plane->normal, normal);
        if (face->face->drawflags & DSURF_PLANEBACK)
            VectorNegate(normal, normal);
        VectorNormalize(normal);
        float center_uv[2] = { 0.0f, 0.0f };
        for (uint32_t j = 0; j < face->edge_count; j++) {
            uint32_t source = face->first_vertex + j;
            center_uv[0] += lmuv_data[source * 2 + 0];
            center_uv[1] += lmuv_data[source * 2 + 1];
        }
        center_uv[0] /= face->edge_count;
        center_uv[1] /= face->edge_count;
        float min_radius = 1.0e30f;
        for (uint32_t j = 0; j < face->edge_count; j++) {
            uint32_t source = face->first_vertex + j;
            float dx = (lmuv_data[source * 2 + 0] - center_uv[0]) * atlas_width;
            float dy = (lmuv_data[source * 2 + 1] - center_uv[1]) * atlas_height;
            min_radius = min(min_radius, sqrtf(dx * dx + dy * dy));
        }
        float expansion = 1.0f + 1.0f / max(min_radius, 4.0f);
        expansion = min(expansion, 1.25f);
        uint32_t base_vertex = dst_vertex;
        for (uint32_t j = 0; j < face->edge_count; j++) {
            uint32_t source = face->first_vertex + j;
            vk_rt_bake_vertex_t *dst = &bake_vertices[dst_vertex++];
            for (int axis = 0; axis < 3; axis++) {
                dst->position[axis] = face->center[axis] +
                    (vertices[source].position[axis] - face->center[axis]) *
                    expansion;
                dst->normal[axis] = normal[axis];
            }
            dst->uv[0] = center_uv[0] +
                (lmuv_data[source * 2 + 0] - center_uv[0]) * expansion;
            dst->uv[1] = center_uv[1] +
                (lmuv_data[source * 2 + 1] - center_uv[1]) * expansion;
            dst->rt_data[0] = face->rt_light_offset;
            dst->rt_data[1] = face->rt_light_count;
        }
        for (uint32_t j = 0; j < face->index_count; j++) {
            uint32_t source = indices[face->first_index + j];
            if (source < face->first_vertex ||
                source >= face->first_vertex + face->edge_count)
                continue;
            bake_indices[dst_index++] = base_vertex +
                source - face->first_vertex;
        }
    }

    vk_mesh_t bake_mesh = { 0 };
    bool ok = dst_vertex && dst_index &&
        vk_upload_buffer(&bake_mesh.vertices, bake_vertices,
                         (VkDeviceSize)dst_vertex * sizeof(*bake_vertices),
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
        vk_upload_buffer(&bake_mesh.indices, bake_indices,
                         (VkDeviceSize)dst_index * sizeof(*bake_indices),
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    bake_mesh.vertex_count = ok ? dst_vertex : 0;
    bake_mesh.index_count = ok ? dst_index : 0;
    Z_Free(bake_indices);
    Z_Free(bake_vertices);
    if (!ok)
        goto fail;

    if (!vk_create_rt_ao_pipeline() ||
        !vk_create_color_target(&vk.world.rt_ao_texture, atlas_width,
                                atlas_height, VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        goto fail;

    VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = vk.rt_ao_render_pass,
        .attachmentCount = 1,
        .pAttachments = &vk.world.rt_ao_texture.view,
        .width = atlas_width,
        .height = atlas_height,
        .layers = 1,
    };
    VkResult result = vk.CreateFramebuffer(vk.device, &framebuffer_info, NULL,
                                           &vk.world.rt_ao_framebuffer);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkCreateFramebuffer(rt_ao)", result);
        goto fail;
    }

    VkCommandBuffer cmd;
    if (!vk_begin_immediate(&cmd))
        goto fail;
    VkClearValue clear = {
        .color = { .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } }
    };
    VkRenderPassBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk.rt_ao_render_pass,
        .framebuffer = vk.world.rt_ao_framebuffer,
        .renderArea.extent = { atlas_width, atlas_height },
        .clearValueCount = 1,
        .pClearValues = &clear,
    };
    VkViewport viewport = {
        .width = atlas_width,
        .height = atlas_height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = { .extent = { atlas_width, atlas_height } };
    VkDeviceSize offset = 0;
    vk.CmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vk.CmdSetViewport(cmd, 0, 1, &viewport);
    vk.CmdSetScissor(cmd, 0, 1, &scissor);
    vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.rt_ao_pipeline);
    vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             vk.rt_ao_pipeline_layout, 0, 1,
                             &vk.rt_descriptor_set, 0, NULL);
    vk.CmdBindVertexBuffers(cmd, 0, 1, &bake_mesh.vertices.buffer, &offset);
    vk.CmdBindIndexBuffer(cmd, bake_mesh.indices.buffer, 0,
                          VK_INDEX_TYPE_UINT32);
    vk_rt_ao_push_t push = {
        .ao_samples = ao_samples,
        .emissive_samples = emissive_samples,
        .atlas_width = atlas_width,
        .seed = 0x51f15e5du,
    };
    vk.CmdPushConstants(cmd, vk.rt_ao_pipeline_layout,
                        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, dst_index, 1, 0, 0, 0);
    vk.CmdEndRenderPass(cmd);
    if (!vk_end_immediate(cmd))
        goto fail;

    if (!vk_read_rt_static_stats(atlas_width, atlas_height))
        goto fail;
    if (!vk.world.rt_coverage_texels) {
        Com_SetLastError("RT static atlas contains no rasterized texels");
        goto fail;
    }

    vk_destroy_mesh(&bake_mesh);
    vk.world.rt_ao_samples = ao_samples;
    vk.world.rt_emissive_samples = emissive_samples;
    vk.world.rt_bake_msec = (uint32_t)((vk_time_usec() - bake_start + 500) /
                                       1000);
    vk.world.rt_ao_ready = true;
    vk_update_rt_ao_descriptor();
    double ray_count = ((double)valid_texels * ao_samples +
                        (double)lit_texels * emissive_samples) / 1000000.0;
    Com_Printf("Vulkan RT static lighting: baked %ux%u RGBA atlas, "
               "AO %u + emissive %u samples/texel, %u sources, "
               "%u triangles, %.2fM rays, %.1f MiB, %u ms; "
               "coverage %u, AO %u peak %u, emissive %u peak %u\n",
               atlas_width, atlas_height, ao_samples, emissive_samples,
               vk.world.surface_light_count, dst_index / 3, ray_count,
               (double)atlas_width * atlas_height * 4.0 / (1024.0 * 1024.0),
               vk.world.rt_bake_msec, vk.world.rt_coverage_texels,
               vk.world.rt_occluded_texels, vk.world.rt_ao_peak,
               vk.world.rt_emissive_texels, vk.world.rt_emissive_peak);
    return true;

fail:
    vk_destroy_mesh(&bake_mesh);
    if (vk.world.rt_ao_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.world.rt_ao_framebuffer, NULL);
        vk.world.rt_ao_framebuffer = VK_NULL_HANDLE;
    }
    vk_destroy_texture_resource(&vk.world.rt_ao_texture);
    vk.world.rt_ao_samples = 0;
    vk.world.rt_emissive_samples = 0;
    vk.world.rt_bake_msec = 0;
    vk.world.rt_coverage_texels = 0;
    vk.world.rt_occluded_texels = 0;
    vk.world.rt_emissive_texels = 0;
    vk.world.rt_ao_peak = 0;
    vk.world.rt_emissive_peak = 0;
    vk.world.rt_ao_ready = false;
    vk_update_rt_ao_descriptor();
    return false;
}
#endif

static VkSampler vk_sampler_for_image(const image_t *image)
{
    if (!image)
        return vk.sampler;

    if (image->flags & IF_NEAREST)
        return vk.nearest_sampler ? vk.nearest_sampler : vk.sampler;

    if (image->type == IT_SKY) {
        if (vk_bilerp_skies && !vk_bilerp_skies->integer && vk.sky_nearest_sampler)
            return vk.sky_nearest_sampler;
        return vk.sky_sampler ? vk.sky_sampler : vk.sampler;
    }

    if (image->type == IT_FONT &&
        (!vk_bilerp_chars || !vk_bilerp_chars->integer))
        return vk.nearest_sampler ? vk.nearest_sampler : vk.sampler;

    if (image->type == IT_PIC && vk_bilerp_pics) {
        bool scrap = image->flags & IF_SCRAP;
        if (!vk_bilerp_pics->integer ||
            (scrap && vk_bilerp_pics->integer == 1))
            return vk.nearest_sampler ? vk.nearest_sampler : vk.sampler;
    }

    if (image->type == IT_WALL || image->type == IT_SKIN)
        return vk.sampler;

    return vk.postprocess_sampler ? vk.postprocess_sampler : vk.sampler;
}

static VkSampler vk_sampler_for_pic_flags(imageflags_t flags)
{
    image_t image = {
        .type = IT_PIC,
        .flags = flags,
    };

    return vk_sampler_for_image(&image);
}

static void vk_update_texture_descriptors(void)
{
    for (uint32_t i = 0; i < MAX_RIMAGES; i++) {
        VkSampler sampler = i < (uint32_t)r_numImages ?
            vk_sampler_for_image(&r_images[i]) : vk.sampler;
        vk_update_texture_descriptor_with_sampler(&vk.textures[i], sampler);
    }

    vk_update_texture_descriptor_with_sampler(&vk.raw_texture,
                                              vk_sampler_for_pic_flags(IF_NONE));
    vk_update_texture_descriptor_with_sampler(&vk.particle_texture,
        vk_sampler_for_pic_flags((vk_partshape && vk_partshape->integer == 1) ?
                                 IF_NEAREST : IF_NONE));
    vk_update_texture_descriptor_with_sampler(&vk.beam_texture,
                                              vk.postprocess_sampler);
    // OpenGL lightmaps always use plain bilinear filtering, independent of
    // gl_texturemode and anisotropy. Keep the atlas on the same sampling path.
    vk_update_texture_descriptor_with_sampler(&vk.world.pixel_lightmap_texture,
                                              vk.postprocess_sampler);
    for (int i = 0; i < VK_MAX_CUBEMAPS; i++) {
        if (!vk.cubemaps[i].image)
            continue;
        VkSampler sampler = (vk_bilerp_skies && !vk_bilerp_skies->integer) ?
            vk.sky_nearest_sampler : vk.sky_sampler;
        for (int face = 0; face < 6; face++)
            vk_update_texture_descriptor_with_sampler(&vk.cubemaps[i].faces[face],
                                                      sampler);
    }
}

static uint32_t vk_mip_level_count(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;

    while (width > 1 || height > 1) {
        width = max(width >> 1, 1);
        height = max(height >> 1, 1);
        levels++;
    }

    return levels;
}

static int vk_upscale_level(uint32_t width, uint32_t height,
                            imagetype_t type, imageflags_t flags)
{
    if (type != IT_PIC && type != IT_FONT && type != IT_SPRITE)
        return 0;
    if (!(flags & (IF_PALETTED | IF_SCRAP)))
        return 0;

    int level = vk_upscale_pcx ? Cvar_ClampInteger(vk_upscale_pcx, 0, 2) : 0;
    uint32_t max_size = min(vk.physical_device_properties.limits.maxImageDimension2D,
                            MAX_TEXTURE_SIZE);

    while (level && (width > (max_size >> level) ||
                     height > (max_size >> level)))
        level--;

    return level;
}

static void vk_mip_map(byte *out, const byte *in, uint32_t width, uint32_t height)
{
    uint32_t out_width = max(width >> 1, 1);
    uint32_t out_height = max(height >> 1, 1);

    for (uint32_t y = 0; y < out_height; y++) {
        for (uint32_t x = 0; x < out_width; x++) {
            uint32_t x0 = min(x * 2, width - 1);
            uint32_t x1 = min(x0 + 1, width - 1);
            uint32_t y0 = min(y * 2, height - 1);
            uint32_t y1 = min(y0 + 1, height - 1);
            const byte *p0 = in + 4 * (y0 * width + x0);
            const byte *p1 = in + 4 * (y0 * width + x1);
            const byte *p2 = in + 4 * (y1 * width + x0);
            const byte *p3 = in + 4 * (y1 * width + x1);
            byte *dst = out + 4 * (y * out_width + x);

            for (int c = 0; c < 4; c++)
                dst[c] = (p0[c] + p1[c] + p2[c] + p3[c]) >> 2;
        }
    }
}

static bool vk_upload_texture_data(vk_texture_t *texture, uint32_t width,
                                   uint32_t height, const void *pixels,
                                   bool mipmaps)
{
    uint32_t mip_levels = mipmaps ? vk_mip_level_count(width, height) : 1;
    VkDeviceSize upload_size = 0;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    vk_texture_t uploaded = { 0 };
    VkBufferImageCopy *copies = NULL;
    byte *mip_data = NULL;

    if (!pixels || !width || !height)
        return true;

    uint32_t mip_width = width;
    uint32_t mip_height = height;
    for (uint32_t level = 0; level < mip_levels; level++) {
        upload_size += (VkDeviceSize)mip_width * mip_height * 4;
        mip_width = max(mip_width >> 1, 1);
        mip_height = max(mip_height >> 1, 1);
    }

    copies = FS_AllocTempMem(sizeof(*copies) * mip_levels);
    mip_data = FS_AllocTempMem(upload_size);
    if (!copies || !mip_data)
        goto fail;

    const byte *src = pixels;
    byte *dst = mip_data;
    mip_width = width;
    mip_height = height;
    VkDeviceSize offset = 0;
    for (uint32_t level = 0; level < mip_levels; level++) {
        VkDeviceSize level_size = (VkDeviceSize)mip_width * mip_height * 4;

        memcpy(dst + offset, src, level_size);
        copies[level] = (VkBufferImageCopy) {
            .bufferOffset = offset,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = level,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageExtent = { mip_width, mip_height, 1 },
        };

        if (level + 1 < mip_levels) {
            byte *next = dst + offset + level_size;
            vk_mip_map(next, src, mip_width, mip_height);
            src = next;
        }
        offset += level_size;
        mip_width = max(mip_width >> 1, 1);
        mip_height = max(mip_height >> 1, 1);
    }

    if (!vk_create_buffer(upload_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &staging, &staging_memory))
        goto fail;

    void *mapped;
    VkResult result = vk.MapMemory(vk.device, staging_memory, 0, upload_size, 0, &mapped);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkMapMemory", result);
        goto fail;
    }
    memcpy(mapped, mip_data, upload_size);
    vk.UnmapMemory(vk.device, staging_memory);

    if (!vk_create_texture_image(width, height, mip_levels,
                                 &uploaded.image, &uploaded.memory))
        goto fail;

    VkCommandBuffer cmd;
    if (!vk_begin_immediate(&cmd))
        goto fail;

    vk_texture_mip_barrier(cmd, uploaded.image, mip_levels,
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           0,
                           VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT);
    vk.CmdCopyBufferToImage(cmd, staging, uploaded.image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            mip_levels, copies);

    vk_texture_mip_barrier(cmd, uploaded.image, mip_levels,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    if (!vk_end_immediate(cmd))
        goto fail;

#if USE_BGRA
    const VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
#else
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
#endif
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = uploaded.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = mip_levels,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    result = vk.CreateImageView(vk.device, &view_info, NULL, &uploaded.view);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkCreateImageView", result);
        goto fail;
    }

    VkDescriptorSetAllocateInfo descriptor_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vk.texture_set_layout,
    };
    result = vk.AllocateDescriptorSets(vk.device, &descriptor_info,
                                       &uploaded.descriptor_set);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkAllocateDescriptorSets", result);
        goto fail;
    }

    vk_update_texture_descriptor(&uploaded);

    uploaded.width = width;
    uploaded.height = height;
    uploaded.mip_levels = mip_levels;

    if (staging)
        vk.DestroyBuffer(vk.device, staging, NULL);
    if (staging_memory)
        vk.FreeMemory(vk.device, staging_memory, NULL);
    if (copies)
        FS_FreeTempMem(copies);
    if (mip_data)
        FS_FreeTempMem(mip_data);

    vk_destroy_texture_resource(texture);
    *texture = uploaded;
    c.texUploads++;
    return true;

fail:
    if (staging)
        vk.DestroyBuffer(vk.device, staging, NULL);
    if (staging_memory)
        vk.FreeMemory(vk.device, staging_memory, NULL);
    if (copies)
        FS_FreeTempMem(copies);
    if (mip_data)
        FS_FreeTempMem(mip_data);
    vk_destroy_texture_resource(&uploaded);
    return false;
}

static uint32_t vk_alloc_temp_texture_index(void)
{
    for (uint32_t i = MAX_RIMAGES; i-- > (uint32_t)r_numImages;) {
        if (!vk.textures[i].descriptor_set)
            return i;
    }

    return 0;
}

// Packed cubemap layouts, matching the OpenGL image loader. The first two
// bytes are atlas columns/rows, followed by six (column,row) face offsets in
// rt, lf, up, dn, bk, ft order.
static const byte vk_cubemap_layouts[][14] = {
    { 4, 3, 2, 1, 0, 1, 1, 0, 1, 2, 1, 1, 3, 1 },
    { 3, 4, 2, 1, 0, 1, 1, 0, 1, 2, 1, 1, 1, 3 },
    { 6, 1, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0 },
    { 1, 6, 0, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5 },
    { 3, 2, 0, 0, 0, 1, 1, 0, 1, 1, 2, 0, 2, 1 },
    { 2, 3, 0, 0, 1, 0, 0, 1, 1, 1, 0, 2, 1, 2 },
};

static vk_cubemap_t *vk_find_cubemap(const image_t *image)
{
    for (int i = 0; i < VK_MAX_CUBEMAPS; i++) {
        if (vk.cubemaps[i].image == image)
            return &vk.cubemaps[i];
    }
    return NULL;
}

static void vk_clear_cubemap(vk_cubemap_t *cubemap)
{
    if (!cubemap)
        return;
    for (int i = 0; i < 6; i++)
        vk_destroy_texture_resource(&cubemap->faces[i]);
    cubemap->image = NULL;
}

static void vk_unload_cubemap(image_t *image)
{
    vk_cubemap_t *cubemap = vk_find_cubemap(image);
    if (cubemap)
        vk_clear_cubemap(cubemap);
    image->texnum = image->texnum2 = 0;
}

static bool vk_upload_cubemap(image_t *image, const byte *pic)
{
    const byte *layout = NULL;
    vk_cubemap_t *cubemap = vk_find_cubemap(image);

    if (!image || !pic || !image->upload_width || !image->upload_height)
        return false;

    for (int i = 0; i < q_countof(vk_cubemap_layouts); i++) {
        const byte *candidate = vk_cubemap_layouts[i];
        if ((uint32_t)image->upload_width * candidate[1] ==
            (uint32_t)image->upload_height * candidate[0]) {
            layout = candidate;
            break;
        }
    }
    if (!layout) {
        Com_SetLastError("Unsupported cubemap atlas aspect ratio");
        return false;
    }

    uint32_t size = image->upload_width / layout[0];
    uint32_t max_size = min(vk.physical_device_properties.limits.maxImageDimension2D,
                            MAX_TEXTURE_SIZE);
    if (!size || size > max_size) {
        Com_SetLastError("Cubemap face exceeds Vulkan texture size limit");
        return false;
    }

    if (!cubemap) {
        for (int i = 0; i < VK_MAX_CUBEMAPS; i++) {
            if (!vk.cubemaps[i].image) {
                cubemap = &vk.cubemaps[i];
                break;
            }
        }
    }
    if (!cubemap) {
        Com_SetLastError("No free Vulkan cubemap slots");
        return false;
    }

    vk_clear_cubemap(cubemap);
    cubemap->image = image;
    byte *face_pixels = FS_AllocTempMem((size_t)size * size * 4);
    layout += 2;

    for (int face = 0; face < 6; face++, layout += 2) {
        uint32_t x = layout[0] * size;
        uint32_t y = layout[1] * size;
        for (uint32_t row = 0; row < size; row++) {
            memcpy(face_pixels + (size_t)row * size * 4,
                   pic + ((size_t)(y + row) * image->upload_width + x) * 4,
                   (size_t)size * 4);
        }
        vk_color_transform_texture(face_pixels, size, size,
                                   image->type, image->flags);
        if (!vk_upload_texture_data(&cubemap->faces[face], size, size,
                                    face_pixels, false)) {
            FS_FreeTempMem(face_pixels);
            vk_clear_cubemap(cubemap);
            image->texnum = 0;
            return false;
        }
        VkSampler sampler = (vk_bilerp_skies && !vk_bilerp_skies->integer) ?
            vk.sky_nearest_sampler : vk.sky_sampler;
        vk_update_texture_descriptor_with_sampler(&cubemap->faces[face], sampler);
    }

    FS_FreeTempMem(face_pixels);
    image->texnum = (cubemap - vk.cubemaps) + 1;
    image->sl = image->tl = 0.0f;
    image->sh = image->th = 1.0f;
    return true;
}

static bool vk_upload_texture(image_t *image, byte *pic)
{
    uintptr_t first = (uintptr_t)r_images;
    uintptr_t last = (uintptr_t)(r_images + MAX_RIMAGES);
    uintptr_t ptr = (uintptr_t)image;
    if (!pic)
        return true;

    uint32_t index;
    if (ptr >= first && ptr < last) {
        index = image - r_images;
    } else {
        index = vk_alloc_temp_texture_index();
        if (!index) {
            Com_SetLastError("No free Vulkan texture slots");
            return false;
        }
    }

    vk_texture_t *texture = &vk.textures[index];

    uint32_t width = image->upload_width;
    uint32_t height = image->upload_height;
    uint32_t scaled_width = width;
    uint32_t scaled_height = height;
    byte *scaled = pic;

    vk_color_transform_texture(pic, width, height, image->type, image->flags);

    int upscale_level = vk_upscale_level(width, height, image->type, image->flags);
    if (upscale_level) {
        scaled_width = width << upscale_level;
        scaled_height = height << upscale_level;
        scaled = FS_AllocTempMem((size_t)scaled_width * scaled_height * 4);
        if (upscale_level == 2)
            HQ4x_Render((uint32_t *)scaled, (const uint32_t *)pic, width, height);
        else
            HQ2x_Render((uint32_t *)scaled, (const uint32_t *)pic, width, height);
        image->flags |= IF_UPSCALED;
    }

    if (image->type == IT_WALL ||
        (image->type == IT_SKIN && (!vk_downsample_skins || vk_downsample_skins->integer))) {
        if (vk_round_down && vk_round_down->integer) {
            uint32_t pot_width = Q_npot32(scaled_width);
            uint32_t pot_height = Q_npot32(scaled_height);
            if (pot_width > scaled_width)
                scaled_width = max(pot_width >> 1, 1);
            if (pot_height > scaled_height)
                scaled_height = max(pot_height >> 1, 1);
        }

        int shift = vk_picmip ? Cvar_ClampInteger(vk_picmip, 0, 31) : 0;
        while (shift-- > 0 && (scaled_width > 1 || scaled_height > 1)) {
            scaled_width = max(scaled_width >> 1, 1);
            scaled_height = max(scaled_height >> 1, 1);
        }
    }

    uint32_t max_size = min(vk.physical_device_properties.limits.maxImageDimension2D,
                            MAX_TEXTURE_SIZE);
    while (max_size > 0 && (scaled_width > max_size || scaled_height > max_size)) {
        scaled_width = max(scaled_width >> 1, 1);
        scaled_height = max(scaled_height >> 1, 1);
    }

    if (!upscale_level && (scaled_width != width || scaled_height != height)) {
        scaled = FS_AllocTempMem((size_t)scaled_width * scaled_height * 4);
        vk_resample_texture(pic, width, height, scaled, scaled_width, scaled_height);
    }

    image->upload_width = scaled_width;
    image->upload_height = scaled_height;

    bool mipmaps = image->type == IT_WALL || image->type == IT_SKIN ||
                   upscale_level != 0;
    bool ok = vk_upload_texture_data(texture, scaled_width, scaled_height, scaled,
                                     mipmaps);
    if (scaled != pic)
        FS_FreeTempMem(scaled);

    if (!ok) {
        image->texnum = texture->descriptor_set ? index : 0;
        image->sl = 0;
        image->sh = 1;
        image->tl = 0;
        image->th = 1;
        return false;
    }

    image->texnum = index;
    image->sl = 0;
    image->sh = 1;
    image->tl = 0;
    image->th = 1;
    vk_update_texture_descriptor_with_sampler(texture, vk_sampler_for_image(image));

    return true;
}

static void vk_destroy_texture_resource(vk_texture_t *texture)
{
    if (!vk.device)
        return;

    if (!texture->image && !texture->view && !texture->memory &&
        !texture->descriptor_set)
        return;

    if (vk.DeviceWaitIdle)
        vk.DeviceWaitIdle(vk.device);

    if (texture->descriptor_set && vk.descriptor_pool)
        vk.FreeDescriptorSets(vk.device, vk.descriptor_pool, 1,
                              &texture->descriptor_set);
    if (texture->view)
        vk.DestroyImageView(vk.device, texture->view, NULL);
    if (texture->image)
        vk.DestroyImage(vk.device, texture->image, NULL);
    if (texture->memory)
        vk.FreeMemory(vk.device, texture->memory, NULL);

    memset(texture, 0, sizeof(*texture));
}

static void vk_destroy_texture(image_t *image)
{
    unsigned index = image->texnum;
    unsigned index2 = image->texnum2;

    if (index && index < MAX_RIMAGES)
        vk_destroy_texture_resource(&vk.textures[index]);
    if (index2 && index2 < MAX_RIMAGES && index2 != index)
        vk_destroy_texture_resource(&vk.textures[index2]);
    image->texnum = image->texnum2 = 0;
}

static bool vk_load_global(void)
{
#ifdef _WIN32
    const char *libname = "vulkan-1.dll";
#else
    const char *libname = "libvulkan.so.1";
#endif

    vk.GetInstanceProcAddr = Sys_LoadLibrary(libname, "vkGetInstanceProcAddr", &vk.library);
    if (!vk.GetInstanceProcAddr) {
        Com_SetLastError(va("Couldn't load Vulkan loader %s: %s", libname, Com_GetLastError()));
        return false;
    }

    vk.CreateInstance = (PFN_vkCreateInstance)
        vk.GetInstanceProcAddr(NULL, "vkCreateInstance");
    vk.EnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)
        vk.GetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
    vk.EnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)
        vk.GetInstanceProcAddr(NULL, "vkEnumerateInstanceExtensionProperties");

    if (!vk.CreateInstance || !vk.EnumerateInstanceExtensionProperties) {
        Com_SetLastError("Vulkan loader is missing required global entry points");
        return false;
    }

    return true;
}

static bool vk_load_instance(void)
{
#define LOAD(name) \
    do { \
        vk.name = (PFN_vk##name)vk.GetInstanceProcAddr(vk.instance, "vk" #name); \
        if (!vk.name) { \
            Com_SetLastError("Vulkan instance is missing vk" #name); \
            return false; \
        } \
    } while (0)

    LOAD(DestroyInstance);
    LOAD(CreateDevice);
    LOAD(GetDeviceProcAddr);
    LOAD(DestroySurfaceKHR);
    LOAD(EnumeratePhysicalDevices);
    LOAD(GetPhysicalDeviceProperties);
    LOAD(GetPhysicalDeviceFeatures);
    LOAD(GetPhysicalDeviceMemoryProperties);
    LOAD(GetPhysicalDeviceFormatProperties);
    LOAD(GetPhysicalDeviceQueueFamilyProperties);
    LOAD(GetPhysicalDeviceSurfaceSupportKHR);
    LOAD(GetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD(GetPhysicalDeviceSurfaceFormatsKHR);
    LOAD(GetPhysicalDeviceSurfacePresentModesKHR);
    LOAD(EnumerateDeviceExtensionProperties);

    vk.GetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)
        vk.GetInstanceProcAddr(vk.instance, "vkGetPhysicalDeviceFeatures2");
    vk.GetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
        vk.GetInstanceProcAddr(vk.instance, "vkGetPhysicalDeviceProperties2");

#undef LOAD

    return true;
}

static bool vk_load_device(void)
{
#define LOAD(name) \
    do { \
        vk.name = (PFN_vk##name)vk.GetDeviceProcAddr(vk.device, "vk" #name); \
        if (!vk.name) { \
            Com_SetLastError("Vulkan device is missing vk" #name); \
            return false; \
        } \
    } while (0)

    LOAD(DestroyDevice);
    LOAD(GetDeviceQueue);
    LOAD(DeviceWaitIdle);
    LOAD(CreateSwapchainKHR);
    LOAD(DestroySwapchainKHR);
    LOAD(GetSwapchainImagesKHR);
    LOAD(CreateImageView);
    LOAD(DestroyImageView);
    LOAD(CreateRenderPass);
    LOAD(DestroyRenderPass);
    LOAD(CreateFramebuffer);
    LOAD(DestroyFramebuffer);
    LOAD(CreateShaderModule);
    LOAD(DestroyShaderModule);
    LOAD(CreateDescriptorSetLayout);
    LOAD(DestroyDescriptorSetLayout);
    LOAD(CreateDescriptorPool);
    LOAD(DestroyDescriptorPool);
    LOAD(AllocateDescriptorSets);
    LOAD(FreeDescriptorSets);
    LOAD(UpdateDescriptorSets);
    LOAD(CreateSampler);
    LOAD(DestroySampler);
    LOAD(CreatePipelineLayout);
    LOAD(DestroyPipelineLayout);
    LOAD(CreateGraphicsPipelines);
    LOAD(DestroyPipeline);
    LOAD(CreateQueryPool);
    LOAD(DestroyQueryPool);
    LOAD(GetQueryPoolResults);
    LOAD(CreateCommandPool);
    LOAD(DestroyCommandPool);
    LOAD(AllocateCommandBuffers);
    LOAD(FreeCommandBuffers);
    LOAD(ResetCommandBuffer);
    LOAD(BeginCommandBuffer);
    LOAD(EndCommandBuffer);
    LOAD(CmdPipelineBarrier);
    LOAD(CmdBeginRenderPass);
    LOAD(CmdEndRenderPass);
    LOAD(CmdClearAttachments);
    LOAD(CmdBindPipeline);
    LOAD(CmdBindDescriptorSets);
    LOAD(CmdBindVertexBuffers);
    LOAD(CmdBindIndexBuffer);
    LOAD(CmdSetViewport);
    LOAD(CmdSetScissor);
    LOAD(CmdSetLineWidth);
    LOAD(CmdPushConstants);
    LOAD(CmdDraw);
    LOAD(CmdDrawIndexed);
    LOAD(CmdBeginQuery);
    LOAD(CmdEndQuery);
    LOAD(CmdResetQueryPool);
    LOAD(CmdWriteTimestamp);
    LOAD(CreateBuffer);
    LOAD(DestroyBuffer);
    LOAD(GetBufferMemoryRequirements);
    LOAD(CreateImage);
    LOAD(DestroyImage);
    LOAD(GetImageMemoryRequirements);
    LOAD(AllocateMemory);
    LOAD(FreeMemory);
    LOAD(BindBufferMemory);
    LOAD(BindImageMemory);
    LOAD(MapMemory);
    LOAD(UnmapMemory);
    LOAD(CmdCopyBuffer);
    LOAD(CmdCopyBufferToImage);
    LOAD(CmdCopyImageToBuffer);
    LOAD(CreateSemaphore);
    LOAD(DestroySemaphore);
    LOAD(CreateFence);
    LOAD(DestroyFence);
    LOAD(WaitForFences);
    LOAD(ResetFences);
    LOAD(QueueSubmit);
    LOAD(QueueWaitIdle);
    LOAD(AcquireNextImageKHR);
    LOAD(QueuePresentKHR);

#undef LOAD

#if USE_VULKAN_RAYTRACING
    if (vk.raytracing_active) {
#define LOAD_RT(name) \
        vk.name = (PFN_vk##name)vk.GetDeviceProcAddr(vk.device, "vk" #name)
        LOAD_RT(GetBufferDeviceAddress);
        LOAD_RT(CreateAccelerationStructureKHR);
        LOAD_RT(DestroyAccelerationStructureKHR);
        LOAD_RT(GetAccelerationStructureBuildSizesKHR);
        LOAD_RT(CmdBuildAccelerationStructuresKHR);
        LOAD_RT(GetAccelerationStructureDeviceAddressKHR);
#undef LOAD_RT
        if (!vk.GetBufferDeviceAddress || !vk.CreateAccelerationStructureKHR ||
            !vk.DestroyAccelerationStructureKHR ||
            !vk.GetAccelerationStructureBuildSizesKHR ||
            !vk.CmdBuildAccelerationStructuresKHR ||
            !vk.GetAccelerationStructureDeviceAddressKHR) {
            vk.raytracing_active = false;
            Q_strlcpy(vk.raytracing_reason,
                      "ray-query device entry points are missing",
                      sizeof(vk.raytracing_reason));
            Com_WPrintf("Vulkan ray tracing disabled: %s\n",
                        vk.raytracing_reason);
        }
    }
#endif

    return true;
}

static bool vk_create_instance(void)
{
    const char *extensions[VK_MAX_INSTANCE_EXTENSIONS];
    uint32_t extension_count = 0;
    VkResult result;

    if (!vid->get_vk_instance_extensions ||
        !vid->get_vk_instance_extensions(&extension_count, extensions, q_countof(extensions))) {
        Com_SetLastError("Video driver can't provide Vulkan instance extensions");
        return false;
    }

    uint32_t loader_version = VK_API_VERSION_1_0;
    if (vk.EnumerateInstanceVersion &&
        vk.EnumerateInstanceVersion(&loader_version) != VK_SUCCESS)
        loader_version = VK_API_VERSION_1_0;
    vk.instance_api_version = min(loader_version, VK_API_VERSION_1_2);

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = PRODUCT,
        .applicationVersion = VK_MAKE_VERSION(REVISION, 0, 0),
        .pEngineName = PRODUCT,
        .engineVersion = VK_MAKE_VERSION(REVISION, 0, 0),
        .apiVersion = vk.instance_api_version,
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
    };

    result = vk.CreateInstance(&create_info, NULL, &vk.instance);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateInstance", result);

    return vk_load_instance();
}

static bool vk_create_surface(void)
{
    if (!vid->create_vk_surface) {
        Com_SetLastError("Video driver can't create a Vulkan surface");
        return false;
    }

    return vid->create_vk_surface(vk.instance, &vk.surface);
}

static bool vk_has_swapchain_extension(VkPhysicalDevice device)
{
    uint32_t count = 0;
    VkResult result;

    result = vk.EnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    if (result != VK_SUCCESS || !count)
        return false;

    VkExtensionProperties *extensions = Z_Malloc(sizeof(*extensions) * count);
    result = vk.EnumerateDeviceExtensionProperties(device, NULL, &count, extensions);
    if (result != VK_SUCCESS) {
        Z_Free(extensions);
        return false;
    }

    bool found = false;
    for (uint32_t i = 0; i < count; i++) {
        if (!strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            found = true;
            break;
        }
    }

    Z_Free(extensions);
    return found;
}

static vk_queue_families_t vk_find_queues(VkPhysicalDevice device)
{
    vk_queue_families_t found = { 0 };
    uint32_t count = 0;

    vk.GetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    if (!count)
        return found;

    VkQueueFamilyProperties *families = Z_Malloc(sizeof(*families) * count);
    vk.GetPhysicalDeviceQueueFamilyProperties(device, &count, families);

    for (uint32_t i = 0; i < count; i++) {
        VkBool32 present = VK_FALSE;

        if (!found.has_graphics && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            found.graphics_family = i;
            found.has_graphics = true;
        }

        if (vk.GetPhysicalDeviceSurfaceSupportKHR(device, i, vk.surface, &present) == VK_SUCCESS &&
            present && !found.has_present) {
            found.present_family = i;
            found.has_present = true;
        }

        if (found.has_graphics && found.has_present)
            break;
    }

    Z_Free(families);
    return found;
}

static bool vk_pick_physical_device(void)
{
    uint32_t count = 0;
    VkResult result;

    result = vk.EnumeratePhysicalDevices(vk.instance, &count, NULL);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkEnumeratePhysicalDevices", result);
    if (!count) {
        Com_SetLastError("No Vulkan physical devices found");
        return false;
    }

    VkPhysicalDevice *devices = Z_Malloc(sizeof(*devices) * count);
    result = vk.EnumeratePhysicalDevices(vk.instance, &count, devices);
    if (result != VK_SUCCESS) {
        Z_Free(devices);
        return vk_fail_result("vkEnumeratePhysicalDevices", result);
    }

    size_t list_size = strlen("\"automatic\" \"\"") + 1;
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties props;
        vk.GetPhysicalDeviceProperties(devices[i], &props);
        list_size += strlen(props.deviceName) + 32;
    }
    char *device_list = Z_Malloc(list_size);
    Q_strlcpy(device_list, "\"automatic\" \"\"", list_size);

    const char *selector = vk_device ? vk_device->string : "";
    const char *mesa_selector = getenv("MESA_VK_DEVICE_SELECT");
    const char *dri_prime = getenv("DRI_PRIME");
    unsigned mesa_vendor = 0, mesa_device = 0;
    bool mesa_match = false;
    if (!selector[0] && mesa_selector && mesa_selector[0]) {
        mesa_match = sscanf(mesa_selector, "%x:%x",
                            &mesa_vendor, &mesa_device) == 2;
        if (!mesa_match)
            selector = mesa_selector;
    }
    bool numeric_selector = selector[0] != '\0';
    for (const char *p = selector; numeric_selector && *p; p++)
        numeric_selector = *p >= '0' && *p <= '9';
    int selected_index = numeric_selector ? Q_atoi(selector) : -1;
    int best_score = INT_MIN;
    bool requested_selected = false;

    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties props;
        vk.GetPhysicalDeviceProperties(devices[i], &props);
        vk_queue_families_t queues = vk_find_queues(devices[i]);
        bool suitable = queues.has_graphics && queues.has_present &&
            vk_has_swapchain_extension(devices[i]);
        Com_Printf("Vulkan device %u: %s (%s), vendor/device 0x%04x/0x%04x%s\n",
                   i, props.deviceName, vk_device_type_string(props.deviceType),
                   props.vendorID, props.deviceID, suitable ? "" : " [unsuitable]");
        if (!suitable)
            continue;

        for (char *p = props.deviceName; *p; p++) {
            if (*p == '"')
                *p = '\'';
        }
        Q_strlcat(device_list, va(" \"%s\" \"%u\"", props.deviceName, i),
                  list_size);

        bool requested = selector[0] &&
            ((numeric_selector && selected_index == (int)i) ||
             (!numeric_selector && Q_stristr(props.deviceName, selector)));
        if (!selector[0] && mesa_match)
            requested = props.vendorID == mesa_vendor &&
                props.deviceID == mesa_device;
        if (!selector[0] && !mesa_match && dri_prime && dri_prime[0] &&
            strcmp(dri_prime, "0"))
            requested = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        if ((selector[0] || mesa_match ||
             (dri_prime && dri_prime[0] && strcmp(dri_prime, "0"))) &&
            !requested)
            continue;

        int score = 0;
        switch (props.deviceType) {
        // On hybrid systems the integrated GPU normally owns the display.
        // Rendering on a discrete device that merely supports presentation
        // forces every uncapped frame through a PRIME copy.  OpenGL defaults
        // to the display GPU, so prefer the integrated present-capable device
        // as well; vk_device remains available for an explicit override.
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 400; break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 300; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 200; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: score = 100; break;
        default: score = 0; break;
        }
        score += min((int)(props.limits.maxImageDimension2D / 1024), 99);
        if ((!requested || !requested_selected) &&
            (requested || score > best_score)) {
            vk.physical_device = devices[i];
            vk.queues = queues;
            best_score = score;
            if (requested)
                requested_selected = true;
        }
    }

    Cvar_SetByVar(vk_devicelist, device_list, FROM_CODE);
    Z_Free(device_list);
    Z_Free(devices);

    if (!vk.physical_device) {
        if (mesa_match)
            Com_SetLastError(va("MESA_VK_DEVICE_SELECT device %04x:%04x is not available",
                                mesa_vendor, mesa_device));
        else if (selector[0])
            Com_SetLastError(va("Requested Vulkan device '%s' is not available",
                                selector));
        else
            Com_SetLastError("No suitable Vulkan physical device found");
        return false;
    }

    vk.GetPhysicalDeviceProperties(vk.physical_device,
                                   &vk.physical_device_properties);
    vk.GetPhysicalDeviceFeatures(vk.physical_device,
                                 &vk.physical_device_features);
    Com_Printf("Using Vulkan device: %s\n",
               vk.physical_device_properties.deviceName);
    return true;
}

static const char *vk_device_type_string(VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "CPU";
    default:
        return "other";
    }
}

static void vk_strings_f(void)
{
    const VkPhysicalDeviceProperties *props = &vk.physical_device_properties;

    if (!vk.physical_device) {
        Com_Printf("Vulkan renderer is not initialized\n");
        return;
    }

    Com_Printf("Vulkan device: %s\n", props->deviceName);
    Com_Printf("Vulkan type: %s\n",
               vk_device_type_string(props->deviceType));
    Com_Printf("Vulkan API: %u.%u.%u\n",
               VK_VERSION_MAJOR(props->apiVersion),
               VK_VERSION_MINOR(props->apiVersion),
               VK_VERSION_PATCH(props->apiVersion));
    Com_Printf("Vulkan driver: %u.%u.%u\n",
               VK_VERSION_MAJOR(props->driverVersion),
               VK_VERSION_MINOR(props->driverVersion),
               VK_VERSION_PATCH(props->driverVersion));
    Com_Printf("Vulkan vendor/device: 0x%04x/0x%04x\n",
               props->vendorID, props->deviceID);
    Com_Printf("Vulkan queue families: graphics=%u present=%u\n",
               vk.queues.graphics_family, vk.queues.present_family);
    Com_Printf("Vulkan swapchain: %ux%u, %u images\n",
               vk.swapchain_extent.width, vk.swapchain_extent.height,
               vk.swapchain_image_count);
#if USE_VULKAN_RAYTRACING
    const char *rt_status = vk.raytracing_active ? "active" :
        (vk.raytracing_supported ? "available" : "unavailable");
    if (vk.raytracing_reason[0])
        Com_Printf("Vulkan ray tracing: %s (%s)\n", rt_status,
                   vk.raytracing_reason);
    else
        Com_Printf("Vulkan ray tracing: %s\n", rt_status);
#else
    Com_Printf("Vulkan ray tracing: not compiled\n");
#endif
}

#if USE_VULKAN_RAYTRACING
static bool vk_device_has_extensions(const char *const *required,
                                     uint32_t required_count)
{
    uint32_t count = 0;
    VkResult result = vk.EnumerateDeviceExtensionProperties(
        vk.physical_device, NULL, &count, NULL);
    if (result != VK_SUCCESS || !count)
        return false;

    VkExtensionProperties *available = Z_Malloc(sizeof(*available) * count);
    result = vk.EnumerateDeviceExtensionProperties(
        vk.physical_device, NULL, &count, available);
    if (result != VK_SUCCESS) {
        Z_Free(available);
        return false;
    }

    bool found_all = true;
    for (uint32_t i = 0; i < required_count; i++) {
        bool found = false;
        for (uint32_t j = 0; j < count; j++) {
            if (!strcmp(required[i], available[j].extensionName)) {
                found = true;
                break;
            }
        }
        if (!found) {
            found_all = false;
            break;
        }
    }

    Z_Free(available);
    return found_all;
}

static void vk_probe_raytracing(void)
{
    vk.raytracing_supported = false;
    vk.raytracing_active = false;
    vk.raytracing_quad_sharing = false;
    vk.raytracing_reason[0] = '\0';

    if (vk.instance_api_version < VK_API_VERSION_1_2 ||
        !vk.GetPhysicalDeviceFeatures2 || !vk.GetPhysicalDeviceProperties2) {
        Q_strlcpy(vk.raytracing_reason, "Vulkan 1.2 is unavailable",
                  sizeof(vk.raytracing_reason));
        return;
    }

    const char *required[] = {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    };
    if (!vk_device_has_extensions(required, q_countof(required))) {
        Q_strlcpy(vk.raytracing_reason, "required device extensions are missing",
                  sizeof(vk.raytracing_reason));
        return;
    }

    VkPhysicalDeviceRayQueryFeaturesKHR ray_query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
    };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &ray_query,
    };
    VkPhysicalDeviceBufferDeviceAddressFeatures buffer_address = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .pNext = &acceleration,
    };
    VkPhysicalDeviceDescriptorIndexingFeatures descriptors = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .pNext = &buffer_address,
    };
    VkPhysicalDeviceFeatures2 features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &descriptors,
    };
    vk.GetPhysicalDeviceFeatures2(vk.physical_device, &features);

    VkPhysicalDeviceDescriptorIndexingProperties descriptor_props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES,
    };
    VkPhysicalDeviceSubgroupProperties subgroup_props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
    };
    descriptor_props.pNext = &subgroup_props;
    VkPhysicalDeviceProperties2 properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &descriptor_props,
    };
    vk.GetPhysicalDeviceProperties2(vk.physical_device, &properties);

    vk.raytracing_quad_sharing =
        (subgroup_props.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) &&
        (subgroup_props.supportedOperations & VK_SUBGROUP_FEATURE_BASIC_BIT) &&
        (subgroup_props.supportedOperations & VK_SUBGROUP_FEATURE_QUAD_BIT);

    if (!ray_query.rayQuery || !acceleration.accelerationStructure ||
        !buffer_address.bufferDeviceAddress ||
        !descriptors.runtimeDescriptorArray ||
        !descriptors.shaderSampledImageArrayNonUniformIndexing ||
        !descriptors.descriptorBindingPartiallyBound ||
        !descriptors.descriptorBindingSampledImageUpdateAfterBind ||
        descriptor_props.maxDescriptorSetUpdateAfterBindSampledImages < MAX_RIMAGES) {
        Q_strlcpy(vk.raytracing_reason, "required ray-query features or limits are missing",
                  sizeof(vk.raytracing_reason));
        return;
    }

    vk.raytracing_supported = true;
    if (vk_raytracing && vk_raytracing->integer)
        vk.raytracing_active = true;
    else
        Q_strlcpy(vk.raytracing_reason, "disabled by vk_raytracing",
                  sizeof(vk.raytracing_reason));
}
#endif

static bool vk_create_device(void)
{
    float priorities[2] = { 1.0f, 1.0f };
    VkDeviceQueueCreateInfo queue_infos[2];
    uint32_t queue_info_count = 0;
    uint32_t present_queue_index = 0;
    const char *extensions[1
#if USE_VULKAN_RAYTRACING
                           + VK_RT_REQUIRED_EXTENSION_COUNT
#endif
    ] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    uint32_t extension_count = 1;
    VkPhysicalDeviceFeatures features = { 0 };
#if USE_VULKAN_RAYTRACING
    vk_probe_raytracing();
    VkPhysicalDeviceRayQueryFeaturesKHR ray_query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
    };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &ray_query,
    };
    VkPhysicalDeviceBufferDeviceAddressFeatures buffer_address = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .pNext = &acceleration,
    };
    VkPhysicalDeviceDescriptorIndexingFeatures descriptors = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .pNext = &buffer_address,
    };
    if (vk.raytracing_active) {
        extensions[extension_count++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        extensions[extension_count++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
        extensions[extension_count++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
        ray_query.rayQuery = VK_TRUE;
        acceleration.accelerationStructure = VK_TRUE;
        buffer_address.bufferDeviceAddress = VK_TRUE;
        descriptors.runtimeDescriptorArray = VK_TRUE;
        descriptors.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        descriptors.descriptorBindingPartiallyBound = VK_TRUE;
        descriptors.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    } else if (vk_raytracing && vk_raytracing->integer) {
        Com_WPrintf("Vulkan ray tracing unavailable: %s; using raster renderer\n",
                    vk.raytracing_reason);
    }
#endif

    uint32_t family_count = 0;
    vk.GetPhysicalDeviceQueueFamilyProperties(vk.physical_device,
                                               &family_count, NULL);
    VkQueueFamilyProperties *families = family_count ?
        Z_Malloc(sizeof(*families) * family_count) : NULL;
    if (families)
        vk.GetPhysicalDeviceQueueFamilyProperties(vk.physical_device,
                                                   &family_count, families);
    bool separate_shared_present_queue =
        vk.queues.present_family == vk.queues.graphics_family && families &&
        vk.queues.graphics_family < family_count &&
        families[vk.queues.graphics_family].queueCount > 1;
    if (families)
        Z_Free(families);
    if (separate_shared_present_queue)
        present_queue_index = 1;

    queue_infos[queue_info_count++] = (VkDeviceQueueCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.queues.graphics_family,
        .queueCount = separate_shared_present_queue ? 2 : 1,
        .pQueuePriorities = priorities,
    };

    if (vk.queues.present_family != vk.queues.graphics_family) {
        queue_infos[queue_info_count++] = (VkDeviceQueueCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vk.queues.present_family,
            .queueCount = 1,
            .pQueuePriorities = priorities,
        };
    }

    if (vk.physical_device_features.samplerAnisotropy)
        features.samplerAnisotropy = VK_TRUE;
    if (vk.physical_device_features.fillModeNonSolid)
        features.fillModeNonSolid = VK_TRUE;
    if (vk.physical_device_features.wideLines)
        features.wideLines = VK_TRUE;
    if (vk.physical_device_features.occlusionQueryPrecise)
        features.occlusionQueryPrecise = VK_TRUE;
    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
#if USE_VULKAN_RAYTRACING
        .pNext = vk.raytracing_active ? &descriptors : NULL,
#endif
        .queueCreateInfoCount = queue_info_count,
        .pQueueCreateInfos = queue_infos,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
        .pEnabledFeatures = &features,
    };

    VkResult result = vk.CreateDevice(vk.physical_device, &create_info, NULL, &vk.device);
#if USE_VULKAN_RAYTRACING
    if (result != VK_SUCCESS && vk.raytracing_active) {
        Com_WPrintf("Couldn't create a ray-query Vulkan device (error %d); retrying raster-only\n",
                    result);
        vk.raytracing_active = false;
        Q_strlcpy(vk.raytracing_reason, "ray-query device creation failed",
                  sizeof(vk.raytracing_reason));
        create_info.pNext = NULL;
        create_info.enabledExtensionCount = 1;
        result = vk.CreateDevice(vk.physical_device, &create_info, NULL, &vk.device);
    }
#endif
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateDevice", result);

    if (!vk_load_device())
        return false;

    vk.GetDeviceQueue(vk.device, vk.queues.graphics_family, 0, &vk.graphics_queue);
    vk.GetDeviceQueue(vk.device, vk.queues.present_family,
                      present_queue_index, &vk.present_queue);
    Com_Printf("Vulkan queues: graphics %u:0, present %u:%u\n",
               vk.queues.graphics_family, vk.queues.present_family,
               present_queue_index);
    return true;
}

static void vk_texturemode_filters(VkFilter *min_filter, VkFilter *mag_filter)
{
    *min_filter = VK_FILTER_LINEAR;
    *mag_filter = VK_FILTER_LINEAR;

    if (!vk_texturemode)
        return;

    const char *mode = vk_texturemode->string;

    if (!Q_stricmp(mode, "GL_NEAREST")) {
        *min_filter = VK_FILTER_NEAREST;
        *mag_filter = VK_FILTER_NEAREST;
    } else if (!Q_stricmp(mode, "GL_LINEAR")) {
        *min_filter = VK_FILTER_LINEAR;
        *mag_filter = VK_FILTER_LINEAR;
    } else if (!Q_stricmp(mode, "GL_NEAREST_MIPMAP_NEAREST") ||
               !Q_stricmp(mode, "GL_NEAREST_MIPMAP_LINEAR")) {
        *min_filter = VK_FILTER_NEAREST;
        *mag_filter = VK_FILTER_NEAREST;
    } else if (!Q_stricmp(mode, "GL_LINEAR_MIPMAP_NEAREST") ||
               !Q_stricmp(mode, "GL_LINEAR_MIPMAP_LINEAR")) {
        *min_filter = VK_FILTER_LINEAR;
        *mag_filter = VK_FILTER_LINEAR;
    } else if (!Q_stricmp(mode, "MAG_NEAREST")) {
        *min_filter = VK_FILTER_LINEAR;
        *mag_filter = VK_FILTER_NEAREST;
    } else {
        Com_WPrintf("Bad texture mode: %s\n", mode);
        Cvar_Reset(vk_texturemode);
    }
}

static VkSamplerMipmapMode vk_texturemode_mipmap(void)
{
    if (!vk_texturemode)
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;

    const char *mode = vk_texturemode->string;

    if (!Q_stricmp(mode, "GL_NEAREST") ||
        !Q_stricmp(mode, "GL_LINEAR"))
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (!Q_stricmp(mode, "GL_NEAREST_MIPMAP_NEAREST") ||
        !Q_stricmp(mode, "GL_LINEAR_MIPMAP_NEAREST"))
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

static bool vk_texturemode_uses_mipmaps(void)
{
    if (!vk_texturemode)
        return true;

    const char *mode = vk_texturemode->string;

    return Q_stricmp(mode, "GL_NEAREST") && Q_stricmp(mode, "GL_LINEAR");
}

static bool vk_create_sampler(VkSampler *sampler)
{
    VkFilter min_filter, mag_filter;
    float anisotropy = 1.0f;

    vk_texturemode_filters(&min_filter, &mag_filter);

    if (vk_anisotropy && vk.physical_device_features.samplerAnisotropy) {
        anisotropy = Cvar_ClampValue(vk_anisotropy, 1.0f,
                                     vk.physical_device_properties.limits.maxSamplerAnisotropy);
    }

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = mag_filter,
        .minFilter = min_filter,
        .mipmapMode = vk_texturemode_mipmap(),
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = anisotropy > 1.0f,
        .maxAnisotropy = anisotropy,
        .maxLod = vk_texturemode_uses_mipmaps() ? VK_LOD_CLAMP_NONE : 0.0f,
    };
    VkResult result = vk.CreateSampler(vk.device, &sampler_info, NULL, sampler);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateSampler", result);

    return true;
}

static bool vk_create_sky_sampler(VkSampler *sampler)
{
    VkFilter min_filter, mag_filter;
    float anisotropy = 1.0f;

    vk_texturemode_filters(&min_filter, &mag_filter);

    if (vk_anisotropy && vk.physical_device_features.samplerAnisotropy) {
        anisotropy = Cvar_ClampValue(vk_anisotropy, 1.0f,
                                     vk.physical_device_properties.limits.maxSamplerAnisotropy);
    }

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = mag_filter,
        .minFilter = min_filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropyEnable = anisotropy > 1.0f,
        .maxAnisotropy = anisotropy,
        .maxLod = 0.0f,
    };
    VkResult result = vk.CreateSampler(vk.device, &sampler_info, NULL, sampler);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateSampler", result);

    return true;
}

static bool vk_create_postprocess_sampler(VkSampler *sampler)
{
    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    VkResult result = vk.CreateSampler(vk.device, &sampler_info, NULL, sampler);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateSampler", result);

    return true;
}

static bool vk_create_nearest_sampler(VkSampler *sampler,
                                      VkSamplerAddressMode address_mode)
{
    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = address_mode,
        .addressModeV = address_mode,
        .addressModeW = address_mode,
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    VkResult result = vk.CreateSampler(vk.device, &sampler_info, NULL, sampler);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateSampler", result);

    return true;
}

static void vk_texturemode_changed(cvar_t *self)
{
    (void)self;

    if (!vk.device || !vk.CreateSampler || !vk.DestroySampler)
        return;

    VkSampler sampler = VK_NULL_HANDLE;
    VkSampler sky_sampler = VK_NULL_HANDLE;
    if (!vk_create_sampler(&sampler) || !vk_create_sky_sampler(&sky_sampler)) {
        Com_WPrintf("Couldn't recreate Vulkan sampler: %s\n", Com_GetLastError());
        if (sampler)
            vk.DestroySampler(vk.device, sampler, NULL);
        return;
    }

    if (vk.DeviceWaitIdle)
        vk.DeviceWaitIdle(vk.device);

    if (vk.sampler)
        vk.DestroySampler(vk.device, vk.sampler, NULL);
    if (vk.sky_sampler)
        vk.DestroySampler(vk.device, vk.sky_sampler, NULL);

    vk.sampler = sampler;
    vk.sky_sampler = sky_sampler;
    vk_update_texture_descriptors();
}

static void vk_texturemode_g(genctx_t *ctx)
{
    static const char *modes[] = {
        "GL_NEAREST",
        "GL_LINEAR",
        "GL_NEAREST_MIPMAP_NEAREST",
        "GL_LINEAR_MIPMAP_NEAREST",
        "GL_NEAREST_MIPMAP_LINEAR",
        "GL_LINEAR_MIPMAP_LINEAR",
        "MAG_NEAREST",
    };

    ctx->ignorecase = true;
    for (int i = 0; i < q_countof(modes); i++)
        Prompt_AddMatch(ctx, modes[i]);
}

static void vk_sampler_selection_changed(cvar_t *self)
{
    (void)self;

    if (!vk.device)
        return;

    vk_update_texture_descriptors();
}

static void vk_swapinterval_changed(cvar_t *self)
{
    (void)self;

    if (!vk.device || !vk.swapchain)
        return;

    vk_recreate_swapchain("present mode change");
}

static void vk_present_mode_changed(cvar_t *self)
{
    vk_present_mode_warned = false;
    vk_swapinterval_changed(self);
}

static void vk_drawsky_changed(cvar_t *self)
{
    extern void CL_SetSky(void);

    (void)self;

    CL_SetSky();
}

static void vk_glare_changed(cvar_t *self)
{
    (void)self;

    for (int i = 0; i < glr.num_glare_sources; i++) {
        glare_source_t *gs = &glr.glare_sources[i];
        gs->visibility = 0.0f;
        gs->visible = false;
    }
}

static void vk_glare_threshold_changed(cvar_t *self)
{
    Cvar_ClampValue(self, 0.0f, 1.0f);
    if (vk.world.cache)
        vk_build_glare_list(vk.world.cache);
}

static bool vk_create_glare_query_pool(void)
{
    VkQueryPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_OCCLUSION,
        .queryCount = MAX_GLARE_SOURCES * VK_MAX_FRAMES_IN_FLIGHT,
    };
    VkResult result = vk.CreateQueryPool(vk.device, &info, NULL,
                                         &vk.glare_query_pool);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateQueryPool", result);
    memset(vk.glare_query_counts, 0, sizeof(vk.glare_query_counts));
    return true;
}

static bool vk_create_timestamp_query_pool(void)
{
    VkQueryPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = VK_MAX_FRAMES_IN_FLIGHT * 2,
    };
    VkResult result = vk.CreateQueryPool(vk.device, &info, NULL,
                                         &vk.timestamp_query_pool);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateQueryPool(timestamp)", result);
    memset(vk.timestamp_valid, 0, sizeof(vk.timestamp_valid));
    return true;
}

static bool vk_create_frame_resources(void)
{
    VkDescriptorSetLayoutBinding sampler_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &sampler_binding,
    };
    VkResult result = vk.CreateDescriptorSetLayout(vk.device, &set_layout_info,
                                                   NULL, &vk.texture_set_layout);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateDescriptorSetLayout", result);

    VkDescriptorSetLayoutBinding ssr_bindings[3] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };
    VkDescriptorSetLayoutCreateInfo ssr_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = q_countof(ssr_bindings),
        .pBindings = ssr_bindings,
    };
    result = vk.CreateDescriptorSetLayout(vk.device, &ssr_layout_info,
                                           NULL, &vk.ssr_set_layout);
    if (result != VK_SUCCESS)
        Com_WPrintf("Couldn't create Vulkan SSR descriptor layout; reflections will be disabled\n");

#if USE_VULKAN_RAYTRACING
    if (vk.raytracing_active) {
        VkDescriptorSetLayoutBinding rt_bindings[] = {
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };
        VkDescriptorSetLayoutCreateInfo rt_layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = q_countof(rt_bindings),
            .pBindings = rt_bindings,
        };
        result = vk.CreateDescriptorSetLayout(vk.device, &rt_layout_info,
                                               NULL, &vk.rt_set_layout);
        if (result != VK_SUCCESS) {
            Com_WPrintf("Couldn't create Vulkan ray-query descriptor layout; disabling ray tracing\n");
            vk.raytracing_active = false;
            Q_strlcpy(vk.raytracing_reason, "descriptor layout creation failed",
                      sizeof(vk.raytracing_reason));
        }
    }
#endif

    const uint32_t texture_descriptor_count =
        MAX_RIMAGES * 2 + VK_MAX_CUBEMAPS * 6 + 17;
    VkDescriptorPoolSize pool_sizes[3] = { {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = texture_descriptor_count,
    } };
    uint32_t pool_size_count = 1;
#if USE_VULKAN_RAYTRACING
    if (vk.raytracing_active) {
        pool_sizes[pool_size_count++] = (VkDescriptorPoolSize) {
            .type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            .descriptorCount = 1,
        };
        pool_sizes[pool_size_count++] = (VkDescriptorPoolSize) {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
        };
        pool_sizes[0].descriptorCount++;
    }
#endif
    VkDescriptorPoolCreateInfo pool_info_desc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = texture_descriptor_count + pool_size_count - 1,
        .poolSizeCount = pool_size_count,
        .pPoolSizes = pool_sizes,
    };
    result = vk.CreateDescriptorPool(vk.device, &pool_info_desc,
                                     NULL, &vk.descriptor_pool);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateDescriptorPool", result);

    if (vk.ssr_set_layout) {
        VkDescriptorSetLayout layouts[2] = {
            vk.ssr_set_layout, vk.ssr_set_layout
        };
        VkDescriptorSet sets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkDescriptorSetAllocateInfo ssr_alloc = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = vk.descriptor_pool,
            .descriptorSetCount = 2,
            .pSetLayouts = layouts,
        };
        result = vk.AllocateDescriptorSets(vk.device, &ssr_alloc,
                                            sets);
        if (result != VK_SUCCESS) {
            Com_WPrintf("Couldn't allocate Vulkan SSR descriptors; reflections will be disabled\n");
            vk.ssr_descriptor_set = VK_NULL_HANDLE;
            vk.ssr_resolve_descriptor_set = VK_NULL_HANDLE;
        } else {
            vk.ssr_descriptor_set = sets[0];
            vk.ssr_resolve_descriptor_set = sets[1];
        }
    }

#if USE_VULKAN_RAYTRACING
    if (vk.raytracing_active) {
        VkDescriptorSetAllocateInfo rt_alloc = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = vk.descriptor_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &vk.rt_set_layout,
        };
        result = vk.AllocateDescriptorSets(vk.device, &rt_alloc,
                                           &vk.rt_descriptor_set);
        if (result != VK_SUCCESS) {
            Com_WPrintf("Couldn't allocate Vulkan ray-query descriptor set; disabling ray tracing\n");
            vk.raytracing_active = false;
            Q_strlcpy(vk.raytracing_reason, "descriptor allocation failed",
                      sizeof(vk.raytracing_reason));
        }
    }
#endif

    if (!vk_create_sampler(&vk.sampler))
        return false;
    if (!vk_create_sky_sampler(&vk.sky_sampler))
        return false;
    if (!vk_create_postprocess_sampler(&vk.postprocess_sampler))
        return false;
    if (!vk_create_nearest_sampler(&vk.nearest_sampler,
                                   VK_SAMPLER_ADDRESS_MODE_REPEAT))
        return false;
    if (!vk_create_nearest_sampler(&vk.sky_nearest_sampler,
                                   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
        return false;

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = max(max(max(max(max(sizeof(vk_draw_push_t), sizeof(vk_color3d_push_t)),
                                sizeof(vk_world_push_t)), sizeof(vk_world_lit_push_t)),
                            sizeof(vk_alias_push_t)),
                    sizeof(vk_alias_shadow_push_t)),
    };
    VkDescriptorSetLayout set_layouts[2] = { vk.texture_set_layout,
#if USE_VULKAN_RAYTRACING
                                             vk.rt_set_layout,
#endif
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount =
#if USE_VULKAN_RAYTRACING
            vk.raytracing_active ? 2 : 1,
#else
            1,
#endif
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    result = vk.CreatePipelineLayout(vk.device, &layout_info,
                                     NULL, &vk.rect_pipeline_layout);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreatePipelineLayout", result);

    if (vk.ssr_set_layout) {
        VkPushConstantRange ssr_push_range = {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(vk_ssr_push_t),
        };
        VkPipelineLayoutCreateInfo ssr_pipeline_layout_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &vk.ssr_set_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &ssr_push_range,
        };
        result = vk.CreatePipelineLayout(vk.device, &ssr_pipeline_layout_info,
                                          NULL, &vk.ssr_pipeline_layout);
        if (result != VK_SUCCESS) {
            Com_WPrintf("Couldn't create Vulkan SSR pipeline layout; reflections will be disabled\n");
            vk.ssr_pipeline_layout = VK_NULL_HANDLE;
        }
    }

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk.queues.graphics_family,
    };
    result = vk.CreateCommandPool(vk.device, &pool_info, NULL, &vk.command_pool);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateCommandPool", result);

    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++) {
        result = vk.CreateSemaphore(vk.device, &semaphore_info, NULL,
                                    &vk.image_available[i]);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateSemaphore", result);
        result = vk.CreateSemaphore(vk.device, &semaphore_info, NULL,
                                    &vk.render_finished[i]);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateSemaphore", result);
        result = vk.CreateFence(vk.device, &fence_info, NULL,
                                &vk.frame_fence[i]);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateFence", result);
    }

    return true;
}

static VkSurfaceFormatKHR vk_choose_surface_format(const VkSurfaceFormatKHR *formats,
                                                   uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[i];
        }
    }

    return formats[0];
}

static VkPresentModeKHR vk_choose_present_mode(const VkPresentModeKHR *modes,
                                               uint32_t count)
{
    VkPresentModeKHR requested = VK_PRESENT_MODE_FIFO_KHR;
    bool explicit_mode = vk_present_mode && vk_present_mode->integer > 0;

    if (explicit_mode) {
        switch (vk_present_mode->integer) {
        case 2:
            requested = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        case 3:
            requested = VK_PRESENT_MODE_IMMEDIATE_KHR;
            break;
        default:
            requested = VK_PRESENT_MODE_FIFO_KHR;
            break;
        }
    } else if (!vk_swapinterval || vk_swapinterval->integer) {
        return VK_PRESENT_MODE_FIFO_KHR;
    } else {
        requested = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (modes[i] == requested)
            return requested;
    }

    if (!explicit_mode) {
        for (uint32_t i = 0; i < count; i++) {
            if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                return modes[i];
        }
    } else if (!vk_present_mode_warned) {
        Com_WPrintf("Requested Vulkan present mode is unavailable; falling back to fifo.\n");
        vk_present_mode_warned = true;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

static const char *vk_present_mode_name(VkPresentModeKHR mode)
{
    switch (mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "mailbox";
    case VK_PRESENT_MODE_FIFO_KHR:
        return "fifo";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "fifo_relaxed";
    default:
        return "unknown";
    }
}

static VkExtent2D vk_choose_extent(const VkSurfaceCapabilitiesKHR *caps,
                                   int width, int height)
{
    if (caps->currentExtent.width != UINT32_MAX)
        return caps->currentExtent;

    VkExtent2D extent = {
        .width = Q_clip(width, caps->minImageExtent.width, caps->maxImageExtent.width),
        .height = Q_clip(height, caps->minImageExtent.height, caps->maxImageExtent.height),
    };

    return extent;
}

static void vk_destroy_swapchain(void)
{
    if (vk.device && vk.DeviceWaitIdle)
        vk.DeviceWaitIdle(vk.device);

    vk.render_pass_active = false;
    vk.frame_active = false;
    vk.image_acquired = false;

    if (vk.command_buffers) {
        vk.FreeCommandBuffers(vk.device, vk.command_pool,
                              vk.swapchain_image_count, vk.command_buffers);
        Z_Free(vk.command_buffers);
        vk.command_buffers = NULL;
    }

    if (vk.rect_pipeline) {
        vk.DestroyPipeline(vk.device, vk.rect_pipeline, NULL);
        vk.rect_pipeline = VK_NULL_HANDLE;
    }

    if (vk.vignette_pipeline) {
        vk.DestroyPipeline(vk.device, vk.vignette_pipeline, NULL);
        vk.vignette_pipeline = VK_NULL_HANDLE;
    }

    if (vk.texture_pipeline) {
        vk.DestroyPipeline(vk.device, vk.texture_pipeline, NULL);
        vk.texture_pipeline = VK_NULL_HANDLE;
    }
    if (vk.scene_pipeline) {
        vk.DestroyPipeline(vk.device, vk.scene_pipeline, NULL);
        vk.scene_pipeline = VK_NULL_HANDLE;
    }

    if (vk.waterwarp_pipeline) {
        vk.DestroyPipeline(vk.device, vk.waterwarp_pipeline, NULL);
        vk.waterwarp_pipeline = VK_NULL_HANDLE;
    }

    if (vk.bloom_downscale_pipeline) {
        vk.DestroyPipeline(vk.device, vk.bloom_downscale_pipeline, NULL);
        vk.bloom_downscale_pipeline = VK_NULL_HANDLE;
    }

    if (vk.bloom_blur_pipeline) {
        vk.DestroyPipeline(vk.device, vk.bloom_blur_pipeline, NULL);
        vk.bloom_blur_pipeline = VK_NULL_HANDLE;
    }

    if (vk.bloom_add_pipeline) {
        vk.DestroyPipeline(vk.device, vk.bloom_add_pipeline, NULL);
        vk.bloom_add_pipeline = VK_NULL_HANDLE;
    }
    if (vk.ssr_pipeline) {
        vk.DestroyPipeline(vk.device, vk.ssr_pipeline, NULL);
        vk.ssr_pipeline = VK_NULL_HANDLE;
    }
    if (vk.ssr_resolve_pipeline) {
        vk.DestroyPipeline(vk.device, vk.ssr_resolve_pipeline, NULL);
        vk.ssr_resolve_pipeline = VK_NULL_HANDLE;
    }

    if (vk.color3d_pipeline) {
        vk.DestroyPipeline(vk.device, vk.color3d_pipeline, NULL);
        vk.color3d_pipeline = VK_NULL_HANDLE;
    }

    if (vk.line3d_pipeline) {
        vk.DestroyPipeline(vk.device, vk.line3d_pipeline, NULL);
        vk.line3d_pipeline = VK_NULL_HANDLE;
    }

    if (vk.debug_line_pipeline) {
        vk.DestroyPipeline(vk.device, vk.debug_line_pipeline, NULL);
        vk.debug_line_pipeline = VK_NULL_HANDLE;
    }

    if (vk.debug_text_pipeline) {
        vk.DestroyPipeline(vk.device, vk.debug_text_pipeline, NULL);
        vk.debug_text_pipeline = VK_NULL_HANDLE;
    }

    if (vk.beam_pipeline) {
        vk.DestroyPipeline(vk.device, vk.beam_pipeline, NULL);
        vk.beam_pipeline = VK_NULL_HANDLE;
    }
    if (vk.beam_bloom_pipeline) {
        vk.DestroyPipeline(vk.device, vk.beam_bloom_pipeline, NULL);
        vk.beam_bloom_pipeline = VK_NULL_HANDLE;
    }

    if (vk.world_pipeline) {
        vk.DestroyPipeline(vk.device, vk.world_pipeline, NULL);
        vk.world_pipeline = VK_NULL_HANDLE;
    }

    if (vk.world_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.world_alpha_pipeline, NULL);
        vk.world_alpha_pipeline = VK_NULL_HANDLE;
    }

    if (vk.world_blend_pipeline) {
        vk.DestroyPipeline(vk.device, vk.world_blend_pipeline, NULL);
        vk.world_blend_pipeline = VK_NULL_HANDLE;
    }


    if (vk.world_glow_pipeline) {
        vk.DestroyPipeline(vk.device, vk.world_glow_pipeline, NULL);
        vk.world_glow_pipeline = VK_NULL_HANDLE;
    }

    if (vk.pixel_world_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_pipeline, NULL);
        vk.pixel_world_pipeline = VK_NULL_HANDLE;
    }

    if (vk.pixel_world_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_alpha_pipeline, NULL);
        vk.pixel_world_alpha_pipeline = VK_NULL_HANDLE;
    }
    if (vk.pixel_world_fast_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_fast_pipeline, NULL);
        vk.pixel_world_fast_pipeline = VK_NULL_HANDLE;
    }

    if (vk.pixel_world_glow_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_glow_pipeline, NULL);
        vk.pixel_world_glow_pipeline = VK_NULL_HANDLE;
    }

    if (vk.pixel_world_glow_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_glow_alpha_pipeline, NULL);
        vk.pixel_world_glow_alpha_pipeline = VK_NULL_HANDLE;
    }
    if (vk.pixel_world_glow_fast_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_glow_fast_pipeline, NULL);
        vk.pixel_world_glow_fast_pipeline = VK_NULL_HANDLE;
    }
#if USE_VULKAN_RAYTRACING
    if (vk.pixel_world_rt_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_rt_pipeline, NULL);
        vk.pixel_world_rt_pipeline = VK_NULL_HANDLE;
    }
    if (vk.pixel_world_rt_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_rt_alpha_pipeline, NULL);
        vk.pixel_world_rt_alpha_pipeline = VK_NULL_HANDLE;
    }
    if (vk.pixel_world_rt_glow_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_rt_glow_pipeline, NULL);
        vk.pixel_world_rt_glow_pipeline = VK_NULL_HANDLE;
    }
    if (vk.pixel_world_rt_glow_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_rt_glow_alpha_pipeline, NULL);
        vk.pixel_world_rt_glow_alpha_pipeline = VK_NULL_HANDLE;
    }
#endif

    if (vk.pixel_world_pipeline_layout) {
        vk.DestroyPipelineLayout(vk.device, vk.pixel_world_pipeline_layout, NULL);
        vk.pixel_world_pipeline_layout = VK_NULL_HANDLE;
    }

    if (vk.sky_pipeline) {
        vk.DestroyPipeline(vk.device, vk.sky_pipeline, NULL);
        vk.sky_pipeline = VK_NULL_HANDLE;
    }

    if (vk.sprite_pipeline) {
        vk.DestroyPipeline(vk.device, vk.sprite_pipeline, NULL);
        vk.sprite_pipeline = VK_NULL_HANDLE;
    }

    if (vk.sprite_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.sprite_alpha_pipeline, NULL);
        vk.sprite_alpha_pipeline = VK_NULL_HANDLE;
    }
    if (vk.sprite_bloom_pipeline) {
        vk.DestroyPipeline(vk.device, vk.sprite_bloom_pipeline, NULL);
        vk.sprite_bloom_pipeline = VK_NULL_HANDLE;
    }

    if (vk.particle_add_pipeline) {
        vk.DestroyPipeline(vk.device, vk.particle_add_pipeline, NULL);
        vk.particle_add_pipeline = VK_NULL_HANDLE;
    }

    if (vk.glare_pipeline) {
        vk.DestroyPipeline(vk.device, vk.glare_pipeline, NULL);
        vk.glare_pipeline = VK_NULL_HANDLE;
    }

    if (vk.glare_occlusion_pipeline) {
        vk.DestroyPipeline(vk.device, vk.glare_occlusion_pipeline, NULL);
        vk.glare_occlusion_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_pipeline, NULL);
        vk.alias_pipeline = VK_NULL_HANDLE;
    }
    if (vk.alias_bloom_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_bloom_pipeline, NULL);
        vk.alias_bloom_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_alpha_pipeline, NULL);
        vk.alias_alpha_pipeline = VK_NULL_HANDLE;
    }
    if (vk.alias_depth_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_depth_pipeline, NULL);
        vk.alias_depth_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_blend_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_blend_pipeline, NULL);
        vk.alias_blend_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_shadow_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_shadow_pipeline, NULL);
        vk.alias_shadow_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_cel_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_cel_pipeline, NULL);
        vk.alias_cel_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_line_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_line_pipeline, NULL);
        vk.alias_line_pipeline = VK_NULL_HANDLE;
    }

    if (vk.framebuffers) {
        for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
            if (vk.framebuffers[i])
                vk.DestroyFramebuffer(vk.device, vk.framebuffers[i], NULL);
        }
        Z_Free(vk.framebuffers);
        vk.framebuffers = NULL;
    }

    if (vk.scene_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.scene_framebuffer, NULL);
        vk.scene_framebuffer = VK_NULL_HANDLE;
    }

    if (vk.bloom_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.bloom_framebuffer, NULL);
        vk.bloom_framebuffer = VK_NULL_HANDLE;
    }

    if (vk.blur_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.blur_framebuffer, NULL);
        vk.blur_framebuffer = VK_NULL_HANDLE;
    }
    if (vk.ssr_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.ssr_framebuffer, NULL);
        vk.ssr_framebuffer = VK_NULL_HANDLE;
    }
    if (vk.ssr_resolve_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.ssr_resolve_framebuffer, NULL);
        vk.ssr_resolve_framebuffer = VK_NULL_HANDLE;
    }

    vk_destroy_texture_resource(&vk.scene_texture);
    vk_destroy_texture_resource(&vk.bloom_source_texture);
    vk_destroy_texture_resource(&vk.bloom_texture);
    vk_destroy_texture_resource(&vk.ssr_texture);
    vk_destroy_texture_resource(&vk.ssr_resolve_texture);
    vk_destroy_texture_resource(&vk.blur_texture);
    vk.scene_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.bloom_source_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.bloom_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.blur_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.ssr_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.ssr_resolve_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.ssr_ready = false;

    if (vk.multisample_view) {
        vk.DestroyImageView(vk.device, vk.multisample_view, NULL);
        vk.multisample_view = VK_NULL_HANDLE;
    }

    if (vk.multisample_image) {
        vk.DestroyImage(vk.device, vk.multisample_image, NULL);
        vk.multisample_image = VK_NULL_HANDLE;
    }

    if (vk.multisample_memory) {
        vk.FreeMemory(vk.device, vk.multisample_memory, NULL);
        vk.multisample_memory = VK_NULL_HANDLE;
    }

    if (vk.bloom_multisample_view) {
        vk.DestroyImageView(vk.device, vk.bloom_multisample_view, NULL);
        vk.bloom_multisample_view = VK_NULL_HANDLE;
    }

    if (vk.bloom_multisample_image) {
        vk.DestroyImage(vk.device, vk.bloom_multisample_image, NULL);
        vk.bloom_multisample_image = VK_NULL_HANDLE;
    }

    if (vk.bloom_multisample_memory) {
        vk.FreeMemory(vk.device, vk.bloom_multisample_memory, NULL);
        vk.bloom_multisample_memory = VK_NULL_HANDLE;
    }

    if (vk.depth_view) {
        vk.DestroyImageView(vk.device, vk.depth_view, NULL);
        vk.depth_view = VK_NULL_HANDLE;
    }
    if (vk.depth_sample_view) {
        vk.DestroyImageView(vk.device, vk.depth_sample_view, NULL);
        vk.depth_sample_view = VK_NULL_HANDLE;
    }

    if (vk.depth_image) {
        vk.DestroyImage(vk.device, vk.depth_image, NULL);
        vk.depth_image = VK_NULL_HANDLE;
    }

    if (vk.depth_memory) {
        vk.FreeMemory(vk.device, vk.depth_memory, NULL);
        vk.depth_memory = VK_NULL_HANDLE;
    }

    if (vk.render_pass) {
        vk.DestroyRenderPass(vk.device, vk.render_pass, NULL);
        vk.render_pass = VK_NULL_HANDLE;
    }

    if (vk.bloom_render_pass) {
        vk.DestroyRenderPass(vk.device, vk.bloom_render_pass, NULL);
        vk.bloom_render_pass = VK_NULL_HANDLE;
    }

    if (vk.swapchain_views) {
        for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
            if (vk.swapchain_views[i])
                vk.DestroyImageView(vk.device, vk.swapchain_views[i], NULL);
        }
        Z_Free(vk.swapchain_views);
        vk.swapchain_views = NULL;
    }

    if (vk.swapchain_images) {
        Z_Free(vk.swapchain_images);
        vk.swapchain_images = NULL;
    }

    if (vk.swapchain_layouts) {
        Z_Free(vk.swapchain_layouts);
        vk.swapchain_layouts = NULL;
    }

    if (vk.image_fences) {
        Z_Free(vk.image_fences);
        vk.image_fences = NULL;
    }

    if (vk.swapchain) {
        vk.DestroySwapchainKHR(vk.device, vk.swapchain, NULL);
        vk.swapchain = VK_NULL_HANDLE;
    }

    vk.swapchain_image_count = 0;
}

static bool vk_recreate_swapchain(const char *reason)
{
    if (!vk.device)
        return false;

    int width = r_config.width;
    int height = r_config.height;

    vk_destroy_swapchain();
    if (!vk_create_swapchain(width, height)) {
        if (vk.swapchain_deferred) {
            vk.swapchain_retry_time = Sys_Milliseconds() + 1000;
            vk_destroy_swapchain();
            return false;
        }
        if (!vk.swapchain_recreate_failed) {
            Com_EPrintf("Couldn't recreate Vulkan swapchain after %s: %s\n",
                        reason, Com_GetLastError());
        }
        vk.swapchain_recreate_failed = true;
        vk.swapchain_retry_time = Sys_Milliseconds() + 1000;
        vk_destroy_swapchain();
        return false;
    }

    vk.swapchain_recreate_failed = false;
    vk.swapchain_retry_time = 0;
    return true;
}

static bool vk_allocate_swapchain_commands(void)
{
    vk.command_buffers = Z_Malloc(sizeof(*vk.command_buffers) * vk.swapchain_image_count);

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = vk.swapchain_image_count,
    };

    VkResult result = vk.AllocateCommandBuffers(vk.device, &alloc_info, vk.command_buffers);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateCommandBuffers", result);

    return true;
}

static bool vk_create_render_pass(void)
{
    bool multisampled = vk.sample_count != VK_SAMPLE_COUNT_1_BIT;
    uint32_t color_count = vk.mrt_bloom ? 2 : 1;
    VkAttachmentDescription attachments[5] = {
        {
            .format = vk.swapchain_format,
            .samples = vk.sample_count,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE :
                                      VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = multisampled ? VK_IMAGE_LAYOUT_UNDEFINED :
                                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = vk.swapchain_format,
            .samples = vk.sample_count,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE :
                                      VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = multisampled ? VK_IMAGE_LAYOUT_UNDEFINED :
                                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = vk.depth_format,
            .samples = vk.sample_count,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = vk_shadow_stencil_enabled() ?
                VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
        {
            .format = vk.swapchain_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = vk.swapchain_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
    };

    VkAttachmentReference color_refs[2] = {
        { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { .attachment = 1, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
    };
    VkAttachmentReference depth_ref = {
        .attachment = vk.mrt_bloom ? 2 : 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference resolve_refs[2] = {
        { .attachment = vk.mrt_bloom ? 3 : 2,
          .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { .attachment = 4, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = color_count,
        .pColorAttachments = color_refs,
        .pResolveAttachments = multisampled ? resolve_refs : NULL,
        .pDepthStencilAttachment = &depth_ref,
    };

    uint32_t attachment_count = vk.mrt_bloom ?
        (multisampled ? 5 : 3) : (multisampled ? 3 : 2);
    if (!vk.mrt_bloom) {
        attachments[1] = attachments[2];
        if (multisampled)
            attachments[2] = attachments[3];
    }

    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = attachment_count,
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };

    VkResult result = vk.CreateRenderPass(vk.device, &create_info, NULL, &vk.render_pass);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateRenderPass", result);

    VkAttachmentDescription bloom_attachment = {
        .format = vk.swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference bloom_color = {
        .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription bloom_subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &bloom_color,
    };
    VkRenderPassCreateInfo bloom_info = create_info;
    bloom_info.attachmentCount = 1;
    bloom_info.pAttachments = &bloom_attachment;
    bloom_info.pSubpasses = &bloom_subpass;
    result = vk.CreateRenderPass(vk.device, &bloom_info, NULL, &vk.bloom_render_pass);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateRenderPass", result);

    return true;
}

static bool vk_create_depth_resources(void)
{
    if (vk.depth_format == VK_FORMAT_UNDEFINED) {
        Com_SetLastError("No supported Vulkan depth format");
        return false;
    }

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = vk.depth_format,
        .extent = {
            .width = vk.swapchain_extent.width,
            .height = vk.swapchain_extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk.sample_count,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkResult result = vk.CreateImage(vk.device, &image_info, NULL, &vk.depth_image);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImage", result);

    VkMemoryRequirements req;
    vk.GetImageMemoryRequirements(vk.device, vk.depth_image, &req);

    uint32_t memory_type = vk_find_memory_type(req.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("No suitable Vulkan depth memory type");
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_type,
    };
    result = vk.AllocateMemory(vk.device, &alloc_info, NULL, &vk.depth_memory);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateMemory", result);

    result = vk.BindImageMemory(vk.device, vk.depth_image, vk.depth_memory, 0);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkBindImageMemory", result);

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk.depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = vk.depth_format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                (vk_depth_format_has_stencil(vk.depth_format) ?
                 VK_IMAGE_ASPECT_STENCIL_BIT : 0),
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    result = vk.CreateImageView(vk.device, &view_info, NULL, &vk.depth_view);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImageView", result);

    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    result = vk.CreateImageView(vk.device, &view_info, NULL,
                                &vk.depth_sample_view);
    if (result != VK_SUCCESS) {
        Com_WPrintf("Couldn't create sampled Vulkan depth view; SSR reflections will be disabled\n");
        vk.depth_sample_view = VK_NULL_HANDLE;
    }
    vk.depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    return true;
}

static bool vk_create_multisample_resources(void)
{
    if (vk.sample_count == VK_SAMPLE_COUNT_1_BIT)
        return true;

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = vk.swapchain_format,
        .extent = {
            .width = vk.swapchain_extent.width,
            .height = vk.swapchain_extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk.sample_count,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkResult result = vk.CreateImage(vk.device, &image_info, NULL,
                                     &vk.multisample_image);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImage", result);

    VkMemoryRequirements req;
    vk.GetImageMemoryRequirements(vk.device, vk.multisample_image, &req);
    uint32_t memory_type = vk_find_memory_type(req.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("No suitable Vulkan multisample memory type");
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_type,
    };
    result = vk.AllocateMemory(vk.device, &alloc_info, NULL,
                               &vk.multisample_memory);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateMemory", result);

    result = vk.BindImageMemory(vk.device, vk.multisample_image,
                                vk.multisample_memory, 0);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkBindImageMemory", result);

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk.multisample_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = vk.swapchain_format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    result = vk.CreateImageView(vk.device, &view_info, NULL,
                                &vk.multisample_view);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImageView", result);

    if (!gl_bloom || gl_bloom->integer <= 0)
        return true;

    // Bloom MRT needs an independent multisampled color attachment so scene
    // color and bloom contribution can resolve in the same subpass.
    result = vk.CreateImage(vk.device, &image_info, NULL,
                            &vk.bloom_multisample_image);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImage(bloom multisample)", result);

    vk.GetImageMemoryRequirements(vk.device, vk.bloom_multisample_image, &req);
    memory_type = vk_find_memory_type(req.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("No suitable Vulkan bloom multisample memory type");
        return false;
    }
    alloc_info.allocationSize = req.size;
    alloc_info.memoryTypeIndex = memory_type;
    result = vk.AllocateMemory(vk.device, &alloc_info, NULL,
                               &vk.bloom_multisample_memory);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateMemory(bloom multisample)", result);

    result = vk.BindImageMemory(vk.device, vk.bloom_multisample_image,
                                vk.bloom_multisample_memory, 0);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkBindImageMemory(bloom multisample)", result);

    view_info.image = vk.bloom_multisample_image;
    result = vk.CreateImageView(vk.device, &view_info, NULL,
                                &vk.bloom_multisample_view);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImageView(bloom multisample)", result);

    return true;
}

static bool vk_create_framebuffers(void)
{
    vk.framebuffers = Z_Mallocz(sizeof(*vk.framebuffers) * vk.swapchain_image_count);

    for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
        bool multisampled = vk.sample_count != VK_SAMPLE_COUNT_1_BIT;
        VkImageView attachments[5];
        uint32_t attachment_count;
        if (vk.mrt_bloom) {
            attachments[0] = multisampled ? vk.multisample_view : vk.swapchain_views[i];
            attachments[1] = multisampled ? vk.bloom_multisample_view :
                                             vk.bloom_source_texture.view;
            attachments[2] = vk.depth_view;
            if (multisampled) {
                attachments[3] = vk.swapchain_views[i];
                attachments[4] = vk.bloom_source_texture.view;
            }
            attachment_count = multisampled ? 5 : 3;
        } else {
            attachments[0] = multisampled ? vk.multisample_view : vk.swapchain_views[i];
            attachments[1] = vk.depth_view;
            if (multisampled)
                attachments[2] = vk.swapchain_views[i];
            attachment_count = multisampled ? 3 : 2;
        }
        VkFramebufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vk.render_pass,
            .attachmentCount = attachment_count,
            .pAttachments = attachments,
            .width = vk.swapchain_extent.width,
            .height = vk.swapchain_extent.height,
            .layers = 1,
        };

        VkResult result = vk.CreateFramebuffer(vk.device, &create_info, NULL, &vk.framebuffers[i]);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateFramebuffer", result);
    }

    return true;
}

static bool vk_create_scene_target(void)
{
    uint32_t bloom_downsample = vk_bloom_downsample_value();
    struct {
        vk_texture_t *texture;
        VkImageLayout *layout;
        uint32_t width;
        uint32_t height;
    } targets[] = {
        { &vk.scene_texture, &vk.scene_layout,
          vk.swapchain_extent.width, vk.swapchain_extent.height },
        { &vk.bloom_source_texture, &vk.bloom_source_layout,
          vk.swapchain_extent.width, vk.swapchain_extent.height },
        { &vk.bloom_texture, &vk.bloom_layout,
          max(vk.swapchain_extent.width / bloom_downsample, 1),
          max(vk.swapchain_extent.height / bloom_downsample, 1) },
        { &vk.blur_texture, &vk.blur_layout,
          max(vk.swapchain_extent.width / bloom_downsample, 1),
          max(vk.swapchain_extent.height / bloom_downsample, 1) },
        { &vk.ssr_texture, &vk.ssr_layout,
          max(vk.swapchain_extent.width / 2, 1),
          max(vk.swapchain_extent.height / 2, 1) },
        { &vk.ssr_resolve_texture, &vk.ssr_resolve_layout,
          vk.swapchain_extent.width, vk.swapchain_extent.height },
    };

    for (size_t i = 0; i < q_countof(targets); i++) {
        if (!vk_create_color_target(targets[i].texture,
                                    targets[i].width,
                                    targets[i].height,
                                    vk.swapchain_format, 0))
            return false;

        vk_update_texture_descriptor_with_sampler(targets[i].texture,
                                                  vk.postprocess_sampler);

        *targets[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    bool multisampled = vk.sample_count != VK_SAMPLE_COUNT_1_BIT;
    VkImageView scene_attachments[5];
    uint32_t scene_attachment_count;
    if (vk.mrt_bloom) {
        scene_attachments[0] = multisampled ? vk.multisample_view : vk.scene_texture.view;
        scene_attachments[1] = multisampled ? vk.bloom_multisample_view :
                                              vk.bloom_source_texture.view;
        scene_attachments[2] = vk.depth_view;
        if (multisampled) {
            scene_attachments[3] = vk.scene_texture.view;
            scene_attachments[4] = vk.bloom_source_texture.view;
        }
        scene_attachment_count = multisampled ? 5 : 3;
    } else {
        scene_attachments[0] = multisampled ? vk.multisample_view : vk.scene_texture.view;
        scene_attachments[1] = vk.depth_view;
        if (multisampled)
            scene_attachments[2] = vk.scene_texture.view;
        scene_attachment_count = multisampled ? 3 : 2;
    }
    VkFramebufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = vk.render_pass,
        .attachmentCount = scene_attachment_count,
        .pAttachments = scene_attachments,
        .width = vk.swapchain_extent.width,
        .height = vk.swapchain_extent.height,
        .layers = 1,
    };
    VkResult result = vk.CreateFramebuffer(vk.device, &create_info, NULL,
                                           &vk.scene_framebuffer);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateFramebuffer(scene MRT)", result);

    struct {
        vk_texture_t *texture;
        VkFramebuffer *framebuffer;
    } post_targets[] = {
        { &vk.bloom_texture, &vk.bloom_framebuffer },
        { &vk.blur_texture, &vk.blur_framebuffer },
        { &vk.ssr_texture, &vk.ssr_framebuffer },
        { &vk.ssr_resolve_texture, &vk.ssr_resolve_framebuffer },
    };
    for (size_t i = 0; i < q_countof(post_targets); i++) {
        VkImageView attachment = post_targets[i].texture->view;
        create_info.renderPass = vk.bloom_render_pass;
        create_info.attachmentCount = 1;
        create_info.pAttachments = &attachment;
        create_info.width = post_targets[i].texture->width;
        create_info.height = post_targets[i].texture->height;
        result = vk.CreateFramebuffer(vk.device, &create_info, NULL,
                                      post_targets[i].framebuffer);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateFramebuffer(bloom postprocess)", result);
    }

    vk.ssr_ready = false;
    if (vk.sample_count == VK_SAMPLE_COUNT_1_BIT && vk.ssr_pipeline_layout &&
        vk.ssr_descriptor_set && vk.ssr_resolve_descriptor_set &&
        vk.depth_sample_view && vk.scene_texture.view && vk.ssr_framebuffer &&
        vk.ssr_resolve_framebuffer) {
        VkDescriptorImageInfo images[3] = {
            {
                .sampler = vk.postprocess_sampler,
                .imageView = vk.scene_texture.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            {
                .sampler = vk.postprocess_sampler,
                .imageView = vk.depth_sample_view,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            },
            {
                .sampler = vk.postprocess_sampler,
                .imageView = vk.bloom_source_texture.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
        };
        VkDescriptorImageInfo resolve_images[3] = {
            {
                .sampler = vk.postprocess_sampler,
                .imageView = vk.ssr_texture.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            {
                .sampler = vk.postprocess_sampler,
                .imageView = vk.depth_sample_view,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            },
            {
                .sampler = vk.postprocess_sampler,
                .imageView = vk.bloom_source_texture.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
        };
        VkWriteDescriptorSet writes[6];
        memset(writes, 0, sizeof(writes));
        for (uint32_t i = 0; i < 3; i++) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = vk.ssr_descriptor_set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[i];
            writes[i + 3] = writes[i];
            writes[i + 3].dstSet = vk.ssr_resolve_descriptor_set;
            writes[i + 3].pImageInfo = &resolve_images[i];
        }
        vk.UpdateDescriptorSets(vk.device, q_countof(writes), writes, 0, NULL);
    }

    return true;
}

static VkShaderModule vk_create_shader_module(const uint32_t *code, size_t code_size)
{
    VkShaderModule module;
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size,
        .pCode = code,
    };

    VkResult result = vk.CreateShaderModule(vk.device, &create_info, NULL, &module);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkCreateShaderModule", result);
        return VK_NULL_HANDLE;
    }

    return module;
}

static int vk_pixel_lightmap_mode(void)
{
    if (!vk_pixel_lightmaps)
        return 0;
    return Cvar_ClampInteger(vk_pixel_lightmaps, 0, 2);
}

static bool vk_validate_pixel_lightmap_shaders(void)
{
    if (vk_pixel_lightmap_mode() < 1)
        return true;

    VkShaderModule vert = vk_create_shader_module(vk_world_pixel_vert_spv,
                                                  sizeof(vk_world_pixel_vert_spv));
    VkShaderModule frag = vk_create_shader_module(vk_world_pixel_frag_spv,
                                                  sizeof(vk_world_pixel_frag_spv));
    VkShaderModule alpha = vk_create_shader_module(vk_world_pixel_alpha_frag_spv,
                                                   sizeof(vk_world_pixel_alpha_frag_spv));
    VkShaderModule glow = vk_create_shader_module(vk_world_pixel_glow_frag_spv,
                                                  sizeof(vk_world_pixel_glow_frag_spv));
    VkShaderModule glow_alpha = vk_create_shader_module(
        vk_world_pixel_glow_alpha_frag_spv,
        sizeof(vk_world_pixel_glow_alpha_frag_spv));
    bool ok = vert && frag && alpha && glow && glow_alpha;

    if (vert)
        vk.DestroyShaderModule(vk.device, vert, NULL);
    if (frag)
        vk.DestroyShaderModule(vk.device, frag, NULL);
    if (alpha)
        vk.DestroyShaderModule(vk.device, alpha, NULL);
    if (glow)
        vk.DestroyShaderModule(vk.device, glow, NULL);
    if (glow_alpha)
        vk.DestroyShaderModule(vk.device, glow_alpha, NULL);

    return ok;
}

static bool vk_create_pixel_world_pipeline_layout(void)
{
    if (vk_pixel_lightmap_mode() < 2)
        return true;
    if (sizeof(vk_world_pixel_push_t) >
        vk.physical_device_properties.limits.maxPushConstantsSize) {
        Com_WPrintf("Vulkan pixel lightmaps mode 2 exceeds push constant limit; using mode 1\n");
        Cvar_Set("vk_pixel_lightmaps", "1");
        return true;
    }

    VkDescriptorSetLayout set_layouts[] = {
        vk.texture_set_layout,
        vk.texture_set_layout,
        vk.texture_set_layout,
#if USE_VULKAN_RAYTRACING
        vk.raytracing_active ? vk.rt_set_layout : vk.texture_set_layout,
#endif
    };
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(vk_world_pixel_push_t),
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = q_countof(set_layouts),
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    VkResult result = vk.CreatePipelineLayout(vk.device, &layout_info, NULL,
                                              &vk.pixel_world_pipeline_layout);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreatePipelineLayout(pixel_world)", result);

    return true;
}

static const VkDynamicState vk_3d_dynamic_states[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
    VK_DYNAMIC_STATE_LINE_WIDTH,
};

static const VkPipelineDynamicStateCreateInfo vk_3d_dynamic_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = q_countof(vk_3d_dynamic_states),
    .pDynamicStates = vk_3d_dynamic_states,
};

static bool vk_create_rect_pipeline_ex(VkPipeline *pipeline,
                                       const uint32_t *vert_spv,
                                       size_t vert_size)
{
    VkShaderModule vert = vk_create_shader_module(vert_spv, vert_size);
    if (!vert)
        return false;

    VkShaderModule frag = vk_create_shader_module(vk_rect_frag_spv, sizeof(vk_rect_frag_spv));
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main",
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = vk.swapchain_extent.width,
        .height = vk.swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = vk.swapchain_extent,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = vk.sample_count,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment[2] = { [0] = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    } };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = vk.mrt_bloom ? 2 : 1,
        .pAttachments = color_blend_attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
    };

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .layout = vk.rect_pipeline_layout,
        .renderPass = vk.render_pass,
        .subpass = 0,
    };

    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                  &create_info, NULL,
                                                  pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

static bool vk_create_rect_pipeline(void)
{
    return vk_create_rect_pipeline_ex(&vk.rect_pipeline, vk_rect_vert_spv,
                                      sizeof(vk_rect_vert_spv));
}

static bool vk_create_texture_pipeline_ex(VkPipeline *pipeline,
                                          const uint32_t *frag_spv,
                                          size_t frag_size,
                                          bool additive,
                                          VkExtent2D extent,
                                          bool postprocess)
{
    VkShaderModule vert = vk_create_shader_module(vk_tex_vert_spv,
                                                  sizeof(vk_tex_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag = vk_create_shader_module(frag_spv, frag_size);
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main",
        },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = extent.width,
        .height = extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = extent,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = postprocess ? VK_SAMPLE_COUNT_1_BIT : vk.sample_count,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment[2] = { [0] = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    } };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = !postprocess && vk.mrt_bloom ? 2 : 1,
        .pAttachments = color_blend_attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
    };

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .layout = vk.rect_pipeline_layout,
        .renderPass = postprocess ? vk.bloom_render_pass : vk.render_pass,
        .subpass = 0,
    };

    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                  &create_info, NULL,
                                                  pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

static bool vk_create_texture_pipeline(void)
{
    return vk_create_texture_pipeline_ex(&vk.texture_pipeline,
                                          vk_tex_frag_spv,
                                          sizeof(vk_tex_frag_spv),
                                          false,
                                          vk.swapchain_extent,
                                          false);
}

static bool vk_create_ssr_pipeline(void)
{
    if (!vk.ssr_pipeline_layout)
        return true;

    VkShaderModule vert = vk_create_shader_module(vk_ssr_vert_spv,
                                                   sizeof(vk_ssr_vert_spv));
    VkShaderModule frag = vk_create_shader_module(vk_ssr_frag_spv,
                                                   sizeof(vk_ssr_frag_spv));
    if (!vert || !frag) {
        if (vert) vk.DestroyShaderModule(vk.device, vert, NULL);
        if (frag) vk.DestroyShaderModule(vk.device, frag, NULL);
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
    };
    VkPipelineVertexInputStateCreateInfo vertex = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport viewport = { .width = max(vk.swapchain_extent.width / 2, 1),
                            .height = max(vk.swapchain_extent.height / 2, 1),
                            .maxDepth = 1.0f };
    VkRect2D scissor = { .extent = { max(vk.swapchain_extent.width / 2, 1),
                                     max(vk.swapchain_extent.height / 2, 1) } };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport,
        .scissorCount = 1, .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    };
    VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages), .pStages = stages,
        .pVertexInputState = &vertex, .pInputAssemblyState = &assembly,
        .pViewportState = &viewport_state, .pRasterizationState = &raster,
        .pMultisampleState = &multisample, .pDepthStencilState = &depth,
        .pColorBlendState = &blend, .layout = vk.ssr_pipeline_layout,
        .renderPass = vk.bloom_render_pass,
    };
    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                  &info, NULL, &vk.ssr_pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    if (result != VK_SUCCESS) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
        return false;
    }

    frag = vk_create_shader_module(vk_ssr_resolve_frag_spv,
                                   sizeof(vk_ssr_resolve_frag_spv));
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
        return false;
    }
    stages[1].module = frag;
    viewport.width = vk.swapchain_extent.width;
    viewport.height = vk.swapchain_extent.height;
    scissor.extent = vk.swapchain_extent;
    result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                         &info, NULL,
                                         &vk.ssr_resolve_pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);
    return result == VK_SUCCESS;
}

static uint32_t vk_bloom_downsample_value(void)
{
    int value = vk_bloom_downsample ? Cvar_ClampInteger(vk_bloom_downsample, 1, 16) : 4;

    return max(value, 1);
}

static uint32_t vk_frames_in_flight_value(void)
{
    int value = vk_frames_in_flight ?
        Cvar_ClampInteger(vk_frames_in_flight, 1, VK_MAX_FRAMES_IN_FLIGHT) : 2;

    return max(value, 1);
}

static bool vk_create_color3d_pipeline(VkPipeline *pipeline, bool depth_test,
                                       bool depth_write, bool blend,
                                       VkPrimitiveTopology topology)
{
    VkShaderModule vert = vk_create_shader_module(vk_color3d_vert_spv,
                                                  sizeof(vk_color3d_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag = vk_create_shader_module(vk_color3d_frag_spv,
                                                  sizeof(vk_color3d_frag_spv));
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main",
        },
    };
    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(vk_vertex_t),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attributes[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, position),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_vertex_t, color),
        },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = q_countof(attributes),
        .pVertexAttributeDescriptions = attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology,
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = vk.swapchain_extent.width,
        .height = vk.swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = vk.swapchain_extent,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = vk.sample_count,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment[2] = { [0] = {
        .blendEnable = blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    } };
    if (pipeline == &vk.beam_bloom_pipeline) {
        color_blend_attachment[1] = color_blend_attachment[0];
        color_blend_attachment[0].colorWriteMask = 0;
    }
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = vk.mrt_bloom ? 2 : 1,
        .pAttachments = color_blend_attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_test,
        .depthWriteEnable = depth_write,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &vk_3d_dynamic_state,
        .layout = vk.rect_pipeline_layout,
        .renderPass = vk.render_pass,
        .subpass = 0,
    };

    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                 &create_info, NULL,
                                                 pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

static bool vk_create_world_pipeline(VkPipeline *pipeline, bool depth_test,
                                     bool depth_write, bool blend,
                                     bool alpha_test, bool additive,
                                     bool glowmap, bool smooth_dlights,
                                     bool color_write)
{
    VkShaderModule vert = (smooth_dlights || glowmap) ?
        vk_create_shader_module(vk_world_lit_vert_spv,
                                sizeof(vk_world_lit_vert_spv)) :
        vk_create_shader_module(vk_world_vert_spv,
                                sizeof(vk_world_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag;
    if (smooth_dlights && alpha_test) {
        frag = vk_create_shader_module(vk_world_lit_alpha_frag_spv,
                                       sizeof(vk_world_lit_alpha_frag_spv));
#if USE_VULKAN_RAYTRACING
    } else if (smooth_dlights && vk.raytracing_active) {
        frag = vk_create_shader_module(vk_world_lit_rt_frag_spv,
                                       sizeof(vk_world_lit_rt_frag_spv));
#endif
    } else if (smooth_dlights) {
        frag = vk_create_shader_module(vk_world_lit_frag_spv,
                                       sizeof(vk_world_lit_frag_spv));
    } else if (glowmap) {
        frag = vk_create_shader_module(vk_world_glow_frag_spv,
                                       sizeof(vk_world_glow_frag_spv));
    } else if (alpha_test) {
        frag = vk_create_shader_module(vk_world_alpha_frag_spv,
                                       sizeof(vk_world_alpha_frag_spv));
    } else {
        frag = vk_create_shader_module(vk_world_frag_spv,
                                       sizeof(vk_world_frag_spv));
    }
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main",
        },
    };
    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(vk_vertex_t),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attributes[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, position),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_vertex_t, color),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_vertex_t, uv),
        },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = q_countof(attributes),
        .pVertexAttributeDescriptions = attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = vk.swapchain_extent.width,
        .height = vk.swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = vk.swapchain_extent,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = vk.sample_count,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment[2] = { [0] = {
        .blendEnable = blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = additive ?
            VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = additive ?
            VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = color_write ?
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT : 0,
    } };
    if (pipeline == &vk.world_glow_pipeline ||
        pipeline == &vk.sprite_bloom_pipeline) {
        color_blend_attachment[1] = color_blend_attachment[0];
        color_blend_attachment[1].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT;
        color_blend_attachment[0].colorWriteMask = 0;
    } else if (pipeline == &vk.world_pipeline ||
               pipeline == &vk.world_alpha_pipeline) {
        color_blend_attachment[1].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = vk.mrt_bloom ? 2 : 1,
        .pAttachments = color_blend_attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_test,
        .depthWriteEnable = depth_write,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &vk_3d_dynamic_state,
        .layout = vk.rect_pipeline_layout,
        .renderPass = vk.render_pass,
        .subpass = 0,
    };

    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                 &create_info, NULL,
                                                 pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

static bool vk_create_pixel_world_pipeline(VkPipeline *pipeline, bool alpha_test,
                                           bool glowmap, bool fast_path,
                                           bool ray_query)
{
    if (vk_pixel_lightmap_mode() < 2)
        return true;
    if (!vk.pixel_world_pipeline_layout) {
        Com_SetLastError("No Vulkan pixel world pipeline layout");
        return false;
    }

    VkShaderModule vert;
#if USE_VULKAN_RAYTRACING
    if (ray_query)
        vert = vk_create_shader_module(vk_world_pixel_rt_vert_spv,
                                       sizeof(vk_world_pixel_rt_vert_spv));
    else
#endif
        vert = vk_create_shader_module(vk_world_pixel_vert_spv,
                                       sizeof(vk_world_pixel_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag;
#if USE_VULKAN_RAYTRACING
    if (ray_query && glowmap && alpha_test)
        frag = vk_create_shader_module(vk_world_pixel_rt_glow_alpha_frag_spv,
                                       sizeof(vk_world_pixel_rt_glow_alpha_frag_spv));
    else if (ray_query && glowmap && vk.raytracing_quad_sharing)
        frag = vk_create_shader_module(vk_world_pixel_rt_quad_glow_frag_spv,
                                       sizeof(vk_world_pixel_rt_quad_glow_frag_spv));
    else if (ray_query && glowmap)
        frag = vk_create_shader_module(vk_world_pixel_rt_glow_frag_spv,
                                       sizeof(vk_world_pixel_rt_glow_frag_spv));
    else if (ray_query && alpha_test)
        frag = vk_create_shader_module(vk_world_pixel_rt_alpha_frag_spv,
                                       sizeof(vk_world_pixel_rt_alpha_frag_spv));
    else if (ray_query && vk.raytracing_quad_sharing)
        frag = vk_create_shader_module(vk_world_pixel_rt_quad_frag_spv,
                                       sizeof(vk_world_pixel_rt_quad_frag_spv));
    else if (ray_query)
        frag = vk_create_shader_module(vk_world_pixel_rt_frag_spv,
                                       sizeof(vk_world_pixel_rt_frag_spv));
    else
#else
    (void)ray_query;
#endif
    if (glowmap && alpha_test)
        frag = vk_create_shader_module(vk_world_pixel_glow_alpha_frag_spv,
                                       sizeof(vk_world_pixel_glow_alpha_frag_spv));
    else if (glowmap)
        frag = vk_create_shader_module(vk_world_pixel_glow_frag_spv,
                                       sizeof(vk_world_pixel_glow_frag_spv));
    else if (alpha_test)
        frag = vk_create_shader_module(vk_world_pixel_alpha_frag_spv,
                                       sizeof(vk_world_pixel_alpha_frag_spv));
    else
        frag = vk_create_shader_module(vk_world_pixel_frag_spv,
                                       sizeof(vk_world_pixel_frag_spv));
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main",
        },
    };
    VkBool32 fast_value = fast_path ? VK_TRUE : VK_FALSE;
    VkSpecializationMapEntry fast_entry = {
        .constantID = 0,
        .offset = 0,
        .size = sizeof(fast_value),
    };
    VkSpecializationInfo fast_info = {
        .mapEntryCount = 1,
        .pMapEntries = &fast_entry,
        .dataSize = sizeof(fast_value),
        .pData = &fast_value,
    };
    stages[1].pSpecializationInfo = &fast_info;
    VkVertexInputBindingDescription bindings[] = {
        {
            .binding = 0,
            .stride = sizeof(vk_vertex_t),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
        {
            .binding = 1,
            .stride = sizeof(float) * 2,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
        {
            .binding = 2,
            .stride = 8,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
    };
    VkVertexInputAttributeDescription attributes[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, position),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_vertex_t, color),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_vertex_t, uv),
        },
        {
            .location = 3,
            .binding = 1,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = 0,
        },
        {
            .location = 4,
            .binding = 2,
            .format = VK_FORMAT_R32G32_UINT,
            .offset = 0,
        },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = ray_query ? 3 : 2,
        .pVertexBindingDescriptions = bindings,
        .vertexAttributeDescriptionCount = ray_query ? 5 : 4,
        .pVertexAttributeDescriptions = attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport viewport = {
        .width = vk.swapchain_extent.width,
        .height = vk.swapchain_extent.height,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .extent = vk.swapchain_extent,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = vk.sample_count,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment[2] = { [0] = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    } };
    if (vk.mrt_bloom) {
        color_blend_attachment[1].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = vk.mrt_bloom ? 2 : 1,
        .pAttachments = color_blend_attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &vk_3d_dynamic_state,
        .layout = vk.pixel_world_pipeline_layout,
        .renderPass = vk.render_pass,
        .subpass = 0,
    };
    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                 &create_info, NULL, pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines(pixel_world)", result);

    return true;
}

static bool vk_create_alias_pipeline(VkPipeline *pipeline, bool depth_write,
                                     bool blend, bool color_write,
                                     bool alpha_test,
                                     bool color_only,
                                     VkCullModeFlags cull_mode,
                                     VkPolygonMode polygon_mode,
                                     VkPrimitiveTopology topology,
                                     VkCompareOp depth_compare,
                                     bool depth_bias_enable,
                                     float depth_bias_constant,
                                     float depth_bias_slope,
                                     VkPipelineDepthStencilStateCreateInfo *stencil_state)
{
    VkShaderModule vert = color_only ?
        vk_create_shader_module(vk_alias_shadow_vert_spv,
                                sizeof(vk_alias_shadow_vert_spv)) :
        vk_create_shader_module(vk_alias_vert_spv,
                                sizeof(vk_alias_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag = color_only ?
        vk_create_shader_module(vk_alias_shadow_frag_spv,
                                sizeof(vk_alias_shadow_frag_spv)) :
        alpha_test ?
        vk_create_shader_module(vk_world_alpha_frag_spv,
                                sizeof(vk_world_alpha_frag_spv)) :
        vk_create_shader_module(vk_world_frag_spv,
                                sizeof(vk_world_frag_spv));
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main",
        },
    };
    VkVertexInputBindingDescription bindings[] = {
        {
            .binding = 0,
            .stride = sizeof(vk_vertex_t),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
        {
            .binding = 1,
            .stride = sizeof(vk_vertex_t),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
    };
    VkVertexInputAttributeDescription attributes[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, position),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_vertex_t, color),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_vertex_t, uv),
        },
        {
            .location = 3,
            .binding = 1,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, position),
        },
        {
            .location = 4,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, normal),
        },
        {
            .location = 5,
            .binding = 1,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, normal),
        },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = q_countof(bindings),
        .pVertexBindingDescriptions = bindings,
        .vertexAttributeDescriptionCount = q_countof(attributes),
        .pVertexAttributeDescriptions = attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology,
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = vk.swapchain_extent.width,
        .height = vk.swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = vk.swapchain_extent,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = polygon_mode,
        .cullMode = cull_mode,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
        .depthBiasEnable = depth_bias_enable,
        .depthBiasConstantFactor = depth_bias_constant,
        .depthBiasSlopeFactor = depth_bias_slope,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = vk.sample_count,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment[2] = { [0] = {
        .blendEnable = blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = color_write ?
            (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT) : 0,
    } };
    if (pipeline == &vk.alias_bloom_pipeline) {
        color_blend_attachment[1] = color_blend_attachment[0];
        color_blend_attachment[0].colorWriteMask = 0;
    } else if (pipeline == &vk.alias_pipeline ||
               pipeline == &vk.alias_alpha_pipeline) {
        // Opaque alias fragments replace any bloom left by geometry drawn
        // earlier. Zero/zero blending writes black without requiring a
        // separate fragment shader; alpha-tested holes still discard.
        color_blend_attachment[1].blendEnable = VK_TRUE;
        color_blend_attachment[1].srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment[1].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment[1].colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment[1].alphaBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment[1].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = vk.mrt_bloom ? 2 : 1,
        .pAttachments = color_blend_attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil;
    if (stencil_state) {
        depth_stencil = *stencil_state;
    } else {
        depth_stencil = (VkPipelineDepthStencilStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = depth_write,
            .depthCompareOp = depth_compare,
        };
    }
    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &vk_3d_dynamic_state,
        .layout = vk.rect_pipeline_layout,
        .renderPass = vk.render_pass,
        .subpass = 0,
    };

    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                 &create_info, NULL,
                                                 pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

static VkPipelineDepthStencilStateCreateInfo vk_shadow_depth_stencil_state(void)
{
    VkStencilOpState stencil = {
        .failOp = VK_STENCIL_OP_KEEP,
        .passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP,
        .depthFailOp = VK_STENCIL_OP_KEEP,
        .compareOp = VK_COMPARE_OP_EQUAL,
        .compareMask = 0xff,
        .writeMask = 0xff,
        .reference = 0,
    };

    return (VkPipelineDepthStencilStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .stencilTestEnable = vk_shadow_stencil_enabled(),
        .front = stencil,
        .back = stencil,
    };
}

static bool vk_create_swapchain(int width, int height)
{
    VkSurfaceCapabilitiesKHR caps;
    vk.swapchain_deferred = false;

    VkResult result = vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical_device,
                                                                 vk.surface, &caps);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);

    uint32_t format_count = 0;
    result = vk.GetPhysicalDeviceSurfaceFormatsKHR(vk.physical_device, vk.surface,
                                                   &format_count, NULL);
    if (result != VK_SUCCESS || !format_count)
        return vk_fail_result("vkGetPhysicalDeviceSurfaceFormatsKHR", result);

    VkSurfaceFormatKHR *formats = Z_Malloc(sizeof(*formats) * format_count);
    result = vk.GetPhysicalDeviceSurfaceFormatsKHR(vk.physical_device, vk.surface,
                                                   &format_count, formats);
    if (result != VK_SUCCESS) {
        Z_Free(formats);
        return vk_fail_result("vkGetPhysicalDeviceSurfaceFormatsKHR", result);
    }
    VkSurfaceFormatKHR surface_format = vk_choose_surface_format(formats, format_count);
    Z_Free(formats);

    uint32_t present_count = 0;
    result = vk.GetPhysicalDeviceSurfacePresentModesKHR(vk.physical_device, vk.surface,
                                                        &present_count, NULL);
    if (result != VK_SUCCESS || !present_count)
        return vk_fail_result("vkGetPhysicalDeviceSurfacePresentModesKHR", result);

    VkPresentModeKHR *present_modes = Z_Malloc(sizeof(*present_modes) * present_count);
    result = vk.GetPhysicalDeviceSurfacePresentModesKHR(vk.physical_device, vk.surface,
                                                        &present_count, present_modes);
    if (result != VK_SUCCESS) {
        Z_Free(present_modes);
        return vk_fail_result("vkGetPhysicalDeviceSurfacePresentModesKHR", result);
    }
    VkPresentModeKHR present_mode = vk_choose_present_mode(present_modes, present_count);
    Z_Free(present_modes);

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;

    VkExtent2D extent = vk_choose_extent(&caps, width, height);
    if (!extent.width || !extent.height) {
        vk.swapchain_deferred = true;
        return false;
    }

    uint32_t queue_indices[] = { vk.queues.graphics_family, vk.queues.present_family };

    vk.swapchain_transfer_src =
        (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    VkImageUsageFlags image_usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (vk.swapchain_transfer_src)
        image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk.surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = image_usage,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    if (vk.queues.graphics_family != vk.queues.present_family) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = q_countof(queue_indices);
        create_info.pQueueFamilyIndices = queue_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    result = vk.CreateSwapchainKHR(vk.device, &create_info, NULL, &vk.swapchain);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateSwapchainKHR", result);

    vk.swapchain_format = surface_format.format;
    vk.present_mode = present_mode;
    vk.sample_count = vk_choose_sample_count();
    vk.depth_format = vk_choose_depth_format(vk_shadow_stencil_requested());
    if (vk.depth_format == VK_FORMAT_UNDEFINED) {
        Com_SetLastError("No supported Vulkan depth format");
        return false;
    }
    Com_Printf("Vulkan depth format: %s, shadow stencil %s, multisampling %ux\n",
               vk_format_name(vk.depth_format),
               vk_shadow_stencil_enabled() ? "enabled" : "disabled",
               (unsigned)vk.sample_count);
    vk.swapchain_extent = extent;

    result = vk.GetSwapchainImagesKHR(vk.device, vk.swapchain, &vk.swapchain_image_count, NULL);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkGetSwapchainImagesKHR", result);
    if (!vk.swapchain_image_count) {
        Com_SetLastError("Vulkan swapchain has no images");
        return false;
    }

    vk.swapchain_images = Z_Malloc(sizeof(*vk.swapchain_images) * vk.swapchain_image_count);
    result = vk.GetSwapchainImagesKHR(vk.device, vk.swapchain,
                                      &vk.swapchain_image_count, vk.swapchain_images);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkGetSwapchainImagesKHR", result);

    vk.swapchain_views = Z_Mallocz(sizeof(*vk.swapchain_views) * vk.swapchain_image_count);
    vk.swapchain_layouts = Z_Malloc(sizeof(*vk.swapchain_layouts) * vk.swapchain_image_count);
    vk.image_fences = Z_Mallocz(sizeof(*vk.image_fences) * vk.swapchain_image_count);
    for (uint32_t i = 0; i < vk.swapchain_image_count; i++)
        vk.swapchain_layouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;

    for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vk.swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = vk.swapchain_format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        result = vk.CreateImageView(vk.device, &view_info, NULL, &vk.swapchain_views[i]);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateImageView", result);
    }

    VkExtent2D bloom_extent = {
        max(vk.swapchain_extent.width / vk_bloom_downsample_value(), 1),
        max(vk.swapchain_extent.height / vk_bloom_downsample_value(), 1),
    };
    vk.mrt_bloom = (gl_bloom && gl_bloom->integer > 0) ||
        (vk_raytracing && vk_raytracing->integer && vk_rt_reflections &&
         vk_rt_reflections->value > 0.001f);

    VkPipelineDepthStencilStateCreateInfo shadow_depth_stencil =
        vk_shadow_depth_stencil_state();

    if (!vk_validate_pixel_lightmap_shaders() ||
        !vk_create_pixel_world_pipeline_layout() ||
        !vk_create_render_pass() ||
        !vk_create_rect_pipeline() ||
        !vk_create_rect_pipeline_ex(&vk.vignette_pipeline,
                                    vk_vignette_vert_spv,
                                    sizeof(vk_vignette_vert_spv)) ||
        !vk_create_texture_pipeline() ||
        !vk_create_texture_pipeline_ex(&vk.scene_pipeline,
                                       vk_scene_frag_spv,
                                       sizeof(vk_scene_frag_spv),
                                       false,
                                       vk.swapchain_extent,
                                       false) ||
        !vk_create_texture_pipeline_ex(&vk.waterwarp_pipeline,
                                       vk_waterwarp_frag_spv,
                                       sizeof(vk_waterwarp_frag_spv),
                                       false,
                                       vk.swapchain_extent,
                                       false) ||
        !vk_create_texture_pipeline_ex(&vk.bloom_downscale_pipeline,
                                       vk_bloom_downscale_frag_spv,
                                       sizeof(vk_bloom_downscale_frag_spv),
                                       false,
                                       bloom_extent,
                                       true) ||
        !vk_create_texture_pipeline_ex(&vk.bloom_blur_pipeline,
                                       vk_bloom_blur_frag_spv,
                                       sizeof(vk_bloom_blur_frag_spv),
                                       false,
                                       bloom_extent,
                                       true) ||
        !vk_create_texture_pipeline_ex(&vk.bloom_add_pipeline,
                                       vk_tex_frag_spv,
                                       sizeof(vk_tex_frag_spv),
                                       true,
                                       vk.swapchain_extent,
                                       false) ||
        !vk_create_color3d_pipeline(&vk.color3d_pipeline, VK_TRUE, VK_TRUE, VK_FALSE,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) ||
        !vk_create_color3d_pipeline(&vk.line3d_pipeline, VK_TRUE, VK_TRUE, VK_FALSE,
                                    VK_PRIMITIVE_TOPOLOGY_LINE_LIST) ||
        !vk_create_color3d_pipeline(&vk.debug_line_pipeline, VK_FALSE, VK_FALSE, VK_TRUE,
                                    VK_PRIMITIVE_TOPOLOGY_LINE_LIST) ||
        !vk_create_color3d_pipeline(&vk.beam_pipeline, VK_TRUE, VK_FALSE, VK_TRUE,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) ||
        !vk_create_color3d_pipeline(&vk.beam_bloom_pipeline, VK_TRUE, VK_FALSE, VK_TRUE,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) ||
        !vk_create_world_pipeline(&vk.world_pipeline, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.world_alpha_pipeline, VK_TRUE, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.world_blend_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.world_glow_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_TRUE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_pipeline, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_alpha_pipeline, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_fast_pipeline, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_glow_pipeline, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_glow_alpha_pipeline, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_glow_fast_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE) ||
        !vk_create_world_pipeline(&vk.sky_pipeline, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.sprite_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.sprite_alpha_pipeline, VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.sprite_bloom_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.particle_add_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.glare_pipeline, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.glare_occlusion_pipeline, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE) ||
        !vk_create_world_pipeline(&vk.debug_text_pipeline, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_alias_pipeline(&vk.alias_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_alpha_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_depth_pipeline, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_blend_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_bloom_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_shadow_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_TRUE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_TRUE, -1.0f, -2.0f, &shadow_depth_stencil) ||
        !vk_create_alias_pipeline(&vk.alias_cel_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_BACK_BIT, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_line_pipeline, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_depth_resources() ||
        !vk_create_multisample_resources() ||
        !vk_create_scene_target() ||
        !vk_create_framebuffers())
        return false;

    if (vk.sample_count == VK_SAMPLE_COUNT_1_BIT && vk.ssr_pipeline_layout &&
        vk.ssr_descriptor_set && vk.ssr_resolve_descriptor_set &&
        vk.depth_sample_view && vk.ssr_framebuffer &&
        vk.ssr_resolve_framebuffer) {
        if (vk_create_ssr_pipeline()) {
            vk.ssr_ready = true;
            Com_Printf("Vulkan SSR: half resolution, 10-14 adaptive steps, full-resolution resolve\n");
        } else {
            Com_WPrintf("Couldn't create Vulkan SSR pipeline: %s; reflections disabled\n",
                        Com_GetLastError());
        }
    } else if (vk.sample_count != VK_SAMPLE_COUNT_1_BIT) {
        Com_WPrintf("Vulkan SSR requires multisampling 1x; reflections disabled\n");
    }

#if USE_VULKAN_RAYTRACING
    vk.raytracing_pixel_ready = false;
    if (vk.raytracing_active) {
        bool rt_ok =
            vk_create_pixel_world_pipeline(&vk.pixel_world_rt_pipeline,
                                           VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE) &&
            vk_create_pixel_world_pipeline(&vk.pixel_world_rt_alpha_pipeline,
                                           VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE) &&
            vk_create_pixel_world_pipeline(&vk.pixel_world_rt_glow_pipeline,
                                           VK_FALSE, VK_TRUE, VK_FALSE, VK_TRUE) &&
            vk_create_pixel_world_pipeline(&vk.pixel_world_rt_glow_alpha_pipeline,
                                           VK_TRUE, VK_TRUE, VK_FALSE, VK_TRUE);
        if (rt_ok) {
            vk.raytracing_pixel_ready = true;
            Com_Printf("Vulkan RT fragment sharing: %s\n",
                       vk.raytracing_quad_sharing ?
                       "adaptive 2x2 (static >384, dynamic >512)" :
                       "unsupported (per-pixel fallback)");
        } else {
            Com_WPrintf("Couldn't create Vulkan RT pixel-lightmap pipelines: %s; using raster pixel lighting\n",
                        Com_GetLastError());
            VkPipeline *rt_pipelines[] = {
                &vk.pixel_world_rt_pipeline,
                &vk.pixel_world_rt_alpha_pipeline,
                &vk.pixel_world_rt_glow_pipeline,
                &vk.pixel_world_rt_glow_alpha_pipeline,
            };
            for (uint32_t i = 0; i < q_countof(rt_pipelines); i++) {
                if (*rt_pipelines[i]) {
                    vk.DestroyPipeline(vk.device, *rt_pipelines[i], NULL);
                    *rt_pipelines[i] = VK_NULL_HANDLE;
                }
            }
        }
    }
#endif

    if (!vk_allocate_swapchain_commands())
        return false;

    Com_Printf("Vulkan swapchain: requested %dx%d, actual %ux%u, %u images, %s present\n",
               width, height, vk.swapchain_extent.width,
               vk.swapchain_extent.height, vk.swapchain_image_count,
               vk_present_mode_name(vk.present_mode));
    return true;
}

static void vk_transition_image(VkCommandBuffer cmd, uint32_t image_index,
                                VkImageLayout new_layout,
                                VkAccessFlags dst_access,
                                VkPipelineStageFlags dst_stage)
{
    VkImageSubresourceRange range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    VkPipelineStageFlags src_stage;
    VkAccessFlags src_access;

    if (vk.swapchain_layouts[image_index] == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    } else if (vk.swapchain_layouts[image_index] == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        src_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        src_access = 0;
    } else {
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        src_access = 0;
    }

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = vk.swapchain_layouts[image_index],
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk.swapchain_images[image_index],
        .subresourceRange = range,
    };

    vk.CmdPipelineBarrier(cmd, src_stage, dst_stage,
                          0, 0, NULL, 0, NULL, 1, &barrier);

    vk.swapchain_layouts[image_index] = new_layout;
}

static void vk_transition_color_target(VkCommandBuffer cmd, vk_texture_t *texture,
                                       VkImageLayout *layout,
                                       VkImageLayout new_layout,
                                       VkAccessFlags dst_access,
                                       VkPipelineStageFlags dst_stage)
{
    if (!texture->image || *layout == new_layout)
        return;

    VkPipelineStageFlags src_stage;
    VkAccessFlags src_access;

    if (*layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    } else if (*layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        src_access = VK_ACCESS_SHADER_READ_BIT;
    } else {
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        src_access = 0;
    }

    vk_texture_barrier(cmd, texture->image,
                       *layout, new_layout,
                       src_access, dst_access,
                       src_stage, dst_stage);
    *layout = new_layout;
}

static void vk_transition_scene(VkCommandBuffer cmd, VkImageLayout new_layout,
                                VkAccessFlags dst_access,
                                VkPipelineStageFlags dst_stage)
{
    vk_transition_color_target(cmd, &vk.scene_texture, &vk.scene_layout,
                               new_layout, dst_access, dst_stage);
}

static void vk_transition_depth(VkCommandBuffer cmd, VkImageLayout new_layout,
                                VkAccessFlags dst_access,
                                VkPipelineStageFlags dst_stage)
{
    if (!vk.depth_image || vk.depth_layout == new_layout)
        return;

    VkAccessFlags src_access = 0;
    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (vk.depth_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        src_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (vk.depth_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
        src_access = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = vk.depth_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk.depth_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                (vk_shadow_stencil_enabled() ? VK_IMAGE_ASPECT_STENCIL_BIT : 0),
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vk.CmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                          0, NULL, 0, NULL, 1, &barrier);
    vk.depth_layout = new_layout;
}

static VkClearColorValue vk_color_to_clear(uint32_t color)
{
    color_t c = { .u32 = color };
    VkClearColorValue clear = {
        .float32 = {
            c.u8[0] / 255.0f,
            c.u8[1] / 255.0f,
            c.u8[2] / 255.0f,
            c.u8[3] / 255.0f,
        },
    };

    return clear;
}

static VkClearColorValue vk_frame_clear_color(void)
{
    VkClearColorValue clear = {
        .float32 = { 0.015f, 0.025f, 0.035f, 1.0f },
    };
    color_t color;

    if (!vk_clear || !vk_clear->integer || !vk_clearcolor)
        return clear;

    if (!SCR_ParseColor(vk_clearcolor->string, &color)) {
        Com_WPrintf("Invalid value '%s' for '%s'\n",
                    vk_clearcolor->string, vk_clearcolor->name);
        Cvar_Reset(vk_clearcolor);
        return vk_color_to_clear(U32_BLACK);
    }

    return vk_color_to_clear(color.u32);
}

static void vk_bind_texture_descriptor(VkCommandBuffer cmd, VkDescriptorSet set)
{
    if (vk.bound_texture_descriptor == set)
        return;

    vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             vk.rect_pipeline_layout, 0, 1, &set, 0, NULL);
    vk.bound_texture_descriptor = set;
    c.texSwitches++;
}

static void vk_bind_pixel_world_descriptor(VkCommandBuffer cmd,
                                           uint32_t set_index,
                                           VkDescriptorSet set)
{
    if (set_index < q_countof(vk.bound_pixel_world_descriptors) &&
        vk.bound_pixel_world_descriptors[set_index] == set)
        return;

    vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             vk.pixel_world_pipeline_layout, set_index, 1,
                             &set, 0, NULL);
    if (set_index < q_countof(vk.bound_pixel_world_descriptors))
        vk.bound_pixel_world_descriptors[set_index] = set;
    c.texSwitches++;
}

static void vk_bind_pipeline(VkCommandBuffer cmd, VkPipelineBindPoint bind_point,
                             VkPipeline pipeline)
{
    if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS &&
        vk.bound_pipeline == pipeline)
        return;

    vk.CmdBindPipeline(cmd, bind_point, pipeline);
    if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        vk.bound_pipeline = pipeline;
        vk.bound_texture_descriptor = VK_NULL_HANDLE;
        memset(vk.bound_pixel_world_descriptors, 0,
               sizeof(vk.bound_pixel_world_descriptors));
    }
    c.pipelineBinds++;
}

static void vk_reset_bind_cache(void)
{
    vk.bound_pipeline = VK_NULL_HANDLE;
    vk.bound_texture_descriptor = VK_NULL_HANDLE;
    memset(vk.bound_pixel_world_descriptors, 0,
           sizeof(vk.bound_pixel_world_descriptors));
    memset(vk.bound_vertex_buffers, 0, sizeof(vk.bound_vertex_buffers));
    memset(vk.bound_vertex_offsets, 0, sizeof(vk.bound_vertex_offsets));
    vk.bound_index_buffer = VK_NULL_HANDLE;
    vk.bound_index_offset = 0;
    vk.bound_index_type = VK_INDEX_TYPE_UINT32;
}

static uint64_t vk_time_usec(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void vk_push_constants(VkCommandBuffer cmd, uint32_t size,
                              const void *data)
{
    vk.CmdPushConstants(cmd, vk.rect_pipeline_layout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, size, data);
    c.uniformUploads++;
}

static void vk_push_pixel_world_constants(VkCommandBuffer cmd, uint32_t size,
                                          const void *data)
{
    vk.CmdPushConstants(cmd, vk.pixel_world_pipeline_layout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, size, data);
    c.uniformUploads++;
}

static void vk_bind_vertex_buffers(VkCommandBuffer cmd, uint32_t first,
                                   uint32_t count, const VkBuffer *buffers,
                                   const VkDeviceSize *offsets)
{
    bool unchanged = first + count <= q_countof(vk.bound_vertex_buffers);

    for (uint32_t i = 0; unchanged && i < count; i++) {
        unchanged = vk.bound_vertex_buffers[first + i] == buffers[i] &&
            vk.bound_vertex_offsets[first + i] == offsets[i];
    }
    if (unchanged)
        return;

    vk.CmdBindVertexBuffers(cmd, first, count, buffers, offsets);
    if (first + count <= q_countof(vk.bound_vertex_buffers)) {
        for (uint32_t i = 0; i < count; i++) {
            vk.bound_vertex_buffers[first + i] = buffers[i];
            vk.bound_vertex_offsets[first + i] = offsets[i];
        }
    }
    c.vertexArrayBinds++;
}

static void vk_bind_index_buffer(VkCommandBuffer cmd, VkBuffer buffer,
                                 VkDeviceSize offset, VkIndexType type)
{
    if (vk.bound_index_buffer == buffer && vk.bound_index_offset == offset &&
        vk.bound_index_type == type)
        return;

    vk.CmdBindIndexBuffer(cmd, buffer, offset, type);
    vk.bound_index_buffer = buffer;
    vk.bound_index_offset = offset;
    vk.bound_index_type = type;
}

static void vk_count_batch3d(void)
{
    c.batchesDrawn++;
    if (vk.drawing_bloom) {
        c.bloomBatches++;
        return;
    }

    switch (vk.draw_scope) {
    case VK_DRAW_WORLD:
        c.worldBatches++;
        break;
    case VK_DRAW_ENTITY:
        c.entityBatches++;
        break;
    case VK_DRAW_PARTICLE:
        c.particleBatches++;
        break;
    default:
        c.otherBatches++;
        break;
    }
}

static int vk_2d_width(void)
{
    int width = r_config.width > 0 ? r_config.width : (int)vk.swapchain_extent.width;

    if (vk.scale > 0.0f && vk.scale != 1.0f)
        width = Q_rint(width * vk.scale);

    return max(width, 1);
}

static int vk_2d_height(void)
{
    int height = r_config.height > 0 ? r_config.height : (int)vk.swapchain_extent.height;

    if (vk.scale > 0.0f && vk.scale != 1.0f)
        height = Q_rint(height * vk.scale);

    return max(height, 1);
}

static void vk_set_3d_viewport(const refdef_t *fd)
{
    if (!fd || !vk.CmdSetViewport || !vk.CmdSetScissor)
        return;

    int width = max(fd->width, 1);
    int height = max(fd->height, 1);
    int x = Q_clip(fd->x, 0, (int)vk.swapchain_extent.width);
    int y = Q_clip(fd->y, 0, (int)vk.swapchain_extent.height);
    int x2 = Q_clip(fd->x + width, x, (int)vk.swapchain_extent.width);
    int y2 = Q_clip(fd->y + height, y, (int)vk.swapchain_extent.height);

    VkViewport viewport = {
        .x = x,
        .y = y,
        .width = max(x2 - x, 1),
        .height = max(y2 - y, 1),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { x, y },
        .extent = { max(x2 - x, 1), max(y2 - y, 1) },
    };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];

    vk.CmdSetViewport(cmd, 0, 1, &viewport);
    vk.CmdSetScissor(cmd, 0, 1, &scissor);
    if (vk.CmdSetLineWidth)
        vk.CmdSetLineWidth(cmd, 1.0f);
}

static void vk_clear_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!vk.render_pass_active || !vk.rect_pipeline || w <= 0 || h <= 0)
        return;

    if (vk.clip_set) {
        int x2 = min(x + w, vk.clip.right);
        int y2 = min(y + h, vk.clip.bottom);
        x = max(x, vk.clip.left);
        y = max(y, vk.clip.top);
        w = x2 - x;
        h = y2 - y;
        if (w <= 0 || h <= 0)
            return;
    }

    int screen_w = vk_2d_width();
    int screen_h = vk_2d_height();
    int x2 = min(x + w, screen_w);
    int y2 = min(y + h, screen_h);
    x = max(x, 0);
    y = max(y, 0);
    w = x2 - x;
    h = y2 - y;
    if (w <= 0 || h <= 0)
        return;

    VkClearColorValue clear = vk_color_to_clear(color);
    vk_rect_push_t push = {
        .rect = { x, y, w, h },
        .color = {
            clear.float32[0],
            clear.float32[1],
            clear.float32[2],
            clear.float32[3],
        },
        .screen = {
            screen_w,
            screen_h,
        },
    };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.rect_pipeline);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
    c.trisDrawn += 2;
    c.batchesDrawn2D++;
    c.rectsDrawn2D++;
}

static void vk_blend_rect(int x, int y, int w, int h, const vec4_t color)
{
    if (!vk.render_pass_active || !vk.rect_pipeline || w <= 0 || h <= 0 ||
        color[3] <= 0.0f)
        return;

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    int screen_w = vk_2d_width();
    int screen_h = vk_2d_height();
    float rgba[4] = {
        Q_clipf(color[0], 0.0f, 1.0f),
        Q_clipf(color[1], 0.0f, 1.0f),
        Q_clipf(color[2], 0.0f, 1.0f),
        Q_clipf(color[3], 0.0f, 1.0f),
    };

    vk_rect_push_t push = {
        .rect = { x, y, w, h },
        .color = { rgba[0], rgba[1], rgba[2], rgba[3] },
        .screen = {
            screen_w,
            screen_h,
        },
    };

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.rect_pipeline);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
    c.trisDrawn += 2;
    c.batchesDrawn2D++;
    c.rectsDrawn2D++;
}

static void vk_blend_vignette(int x, int y, int w, int h, const vec4_t color,
                              float frac)
{
    int distance = min(w, h) * Q_clipf(frac, 0.0f, 0.5f);

    if (distance <= 0) {
        vk_blend_rect(x, y, w, h, color);
        return;
    }

    int inner_w = w - distance * 2;
    int inner_h = h - distance * 2;
    if (!vk.render_pass_active || !vk.vignette_pipeline || inner_w <= 0 ||
        inner_h <= 0 || color[3] <= 0.0f)
        return;

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    vk_draw_push_t push = {
        .rect = { x, y, w, h },
        .color = {
            Q_clipf(color[0], 0.0f, 1.0f),
            Q_clipf(color[1], 0.0f, 1.0f),
            Q_clipf(color[2], 0.0f, 1.0f),
            Q_clipf(color[3], 0.0f, 1.0f),
        },
        .screen = { vk_2d_width(), vk_2d_height() },
        .uv = { x + distance, y + distance, inner_w, inner_h },
    };

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                       vk.vignette_pipeline);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDraw(cmd, 24, 1, 0, 0);
    c.trisDrawn += 8;
    c.batchesDrawn2D++;
    c.rectsDrawn2D++;
}

static void vk_draw_pic_showtris(int x, int y, int w, int h)
{
    if (!gl_showtris || !(gl_showtris->integer & SHOWTRIS_PIC) ||
        w <= 0 || h <= 0)
        return;

    vk_clear_rect(x, y, w, 1, U32_RED);
    vk_clear_rect(x, y + h - 1, w, 1, U32_RED);
    vk_clear_rect(x, y, 1, h, U32_RED);
    vk_clear_rect(x + w - 1, y, 1, h, U32_RED);
}

static void vk_draw_tearing(void)
{
    static int frame;
    bool clip_set;
    float scale;

    if (!vk_showtearing || !vk_showtearing->integer)
        return;

    clip_set = vk.clip_set;
    scale = vk.scale;
    vk.clip_set = false;
    vk.scale = 1.0f;
    vk_clear_rect(0, 0, vk_2d_width(), vk_2d_height(),
                  (++frame & 1) ? U32_WHITE : MakeColor(255, 0, 0, 255));
    vk.scale = scale;
    vk.clip_set = clip_set;
}

static void vk_draw_texture_resource(int x, int y, int w, int h,
                                     float s1, float t1, float s2, float t2,
                                     const vk_texture_t *texture)
{
    if (!vk.render_pass_active || !vk.texture_pipeline || w <= 0 || h <= 0)
        return;
    if (!texture->descriptor_set)
        return;

    float rw = w;
    float rh = h;
    if (vk.clip_set) {
        int x1 = x;
        int y1 = y;
        int x2 = x + w;
        int y2 = y + h;
        int nx1 = max(x1, vk.clip.left);
        int ny1 = max(y1, vk.clip.top);
        int nx2 = min(x2, vk.clip.right);
        int ny2 = min(y2, vk.clip.bottom);
        if (nx2 <= nx1 || ny2 <= ny1)
            return;

        float ds = s2 - s1;
        float dt = t2 - t1;
        s1 += ds * (nx1 - x1) / rw;
        s2 -= ds * (x2 - nx2) / rw;
        t1 += dt * (ny1 - y1) / rh;
        t2 -= dt * (y2 - ny2) / rh;
        x = nx1;
        y = ny1;
        w = nx2 - nx1;
        h = ny2 - ny1;
    }

    int screen_w = vk_2d_width();
    int screen_h = vk_2d_height();
    int x2i = min(x + w, screen_w);
    int y2i = min(y + h, screen_h);
    int nx = max(x, 0);
    int ny = max(y, 0);
    if (x2i <= nx || y2i <= ny)
        return;

    float ds = s2 - s1;
    float dt = t2 - t1;
    s1 += ds * (nx - x) / (float)w;
    s2 -= ds * (x + w - x2i) / (float)w;
    t1 += dt * (ny - y) / (float)h;
    t2 -= dt * (y + h - y2i) / (float)h;
    x = nx;
    y = ny;
    w = x2i - nx;
    h = y2i - ny;

    color_t color = { .u32 = vk.color_set ? vk.color.u32 : MakeColor(255, 255, 255, 255) };
    vk_draw_push_t push = {
        .rect = { x, y, w, h },
        .color = {
            color.u8[0] / 255.0f,
            color.u8[1] / 255.0f,
            color.u8[2] / 255.0f,
            color.u8[3] / 255.0f,
        },
        .screen = {
            screen_w,
            screen_h,
        },
        .uv = { s1, t1, s2, t2 },
    };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.texture_pipeline);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
    c.trisDrawn += 2;
    c.batchesDrawn2D++;
    c.picsDrawn2D++;

    vk_draw_pic_showtris(x, y, w, h);
}

static void vk_draw_texture_rect(int x, int y, int w, int h,
                                 float s1, float t1, float s2, float t2,
                                 qhandle_t pic)
{
    if (pic <= 0 || pic >= r_numImages)
        return;

    const image_t *image = IMG_ForHandle(pic);
    unsigned index = image->texnum;
    const vk_texture_t *texture = vk_texture_for_index(index, true);
    if (!texture)
        return;

    vk_draw_texture_resource(x, y, w, h, s1, t1, s2, t2, texture);
}

static bool vk_bloom_enabled(void)
{
    return gl_bloom && gl_bloom->integer > 0 && vk.mrt_bloom &&
        vk.scene_framebuffer &&
        vk.bloom_framebuffer && vk.blur_framebuffer &&
        vk.scene_texture.descriptor_set &&
        vk.bloom_source_texture.descriptor_set &&
        vk.bloom_texture.descriptor_set && vk.blur_texture.descriptor_set;
}

static bool vk_bloom_enabled_for_frame(void)
{
    return vk_bloom_enabled() && vk.fd_valid &&
        !(vk.fd.rdflags & RDF_NOWORLDMODEL);
}

static bool vk_waterwarp_enabled_for_frame(void)
{
    return vk_waterwarp && vk_waterwarp->integer > 0 && vk.fd_valid &&
        (vk.fd.rdflags & RDF_UNDERWATER) && vk.scene_framebuffer &&
        vk.scene_texture.descriptor_set;
}

static bool vk_ssr_enabled_for_frame(void)
{
#if USE_VULKAN_RAYTRACING
    return vk.ssr_ready && vk.raytracing_active && vk_raytracing &&
        vk_raytracing->integer && vk_rt_reflections &&
        vk_rt_reflections->value > 0.001f && vk.fd_valid &&
        (!vk_rt_debug || vk_rt_debug->integer < 8) &&
        !(vk.fd.rdflags & RDF_NOWORLDMODEL);
#else
    return false;
#endif
}

static bool vk_alias_model_has_glowmap(const vk_model_t *model)
{
    if (!model || model->type != VK_MODEL_ALIAS || !model->skins)
        return false;

    for (int i = 0; i < model->skin_count; i++) {
        const image_t *skin = model->skins[i];

        if (skin && skin->texnum2 && skin->texnum2 < MAX_RIMAGES)
            return true;
    }

    return false;
}

static void vk_draw_texture_rect_sized(VkPipeline pipeline,
                                       const vk_texture_t *texture,
                                       const vec4_t color,
                                       int x, int y, int w, int h,
                                       uint32_t screen_w,
                                       uint32_t screen_h,
                                       const vec4_t uv)
{
    if (!vk.render_pass_active || !pipeline || !texture->descriptor_set ||
        w <= 0 || h <= 0)
        return;

    vk_draw_push_t push = {
        .rect = { x, y, w, h },
        .color = { color[0], color[1], color[2], color[3] },
        .screen = { screen_w, screen_h },
        .uv = { uv[0], uv[1], uv[2], uv[3] },
    };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
    c.trisDrawn += 2;
    c.batchesDrawn2D++;
    c.picsDrawn2D++;
}

static void vk_draw_refdef_texture(VkPipeline pipeline,
                                   const vk_texture_t *texture,
                                   const vec4_t color)
{
    vec4_t uv = { 0.0f, 0.0f, 1.0f, 1.0f };

    if (vk.fd_valid) {
        float scale_x = texture->width / max((float)vk.swapchain_extent.width, 1.0f);
        float scale_y = texture->height / max((float)vk.swapchain_extent.height, 1.0f);
        float tex_w = max((float)texture->width, 1.0f);
        float tex_h = max((float)texture->height, 1.0f);

        uv[0] = (vk.fd.x * scale_x) / tex_w;
        uv[1] = (vk.fd.y * scale_y) / tex_h;
        uv[2] = ((vk.fd.x + vk.fd.width) * scale_x) / tex_w;
        uv[3] = ((vk.fd.y + vk.fd.height) * scale_y) / tex_h;
        vk_draw_texture_rect_sized(pipeline, texture, color,
                                   vk.fd.x, vk.fd.y,
                                   vk.fd.width, vk.fd.height,
                                   vk.swapchain_extent.width,
                                   vk.swapchain_extent.height, uv);
    } else {
        vk_draw_texture_rect_sized(pipeline, texture, color,
                                   0, 0,
                                   vk.swapchain_extent.width,
                                   vk.swapchain_extent.height,
                                   vk.swapchain_extent.width,
                                   vk.swapchain_extent.height, uv);
    }
}

static void vk_composite_scene_texture(void)
{
    vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
    VkPipeline pipeline = vk.scene_pipeline ? vk.scene_pipeline : vk.texture_pipeline;

    if (vk.frame_waterwarp) {
        color[0] = vk.fd.time;
        pipeline = vk.waterwarp_pipeline;
    }

    vk_draw_refdef_texture(pipeline, &vk.scene_texture, color);
}

static void vk_draw_fullscreen_texture_sized(VkPipeline pipeline,
                                             const vk_texture_t *texture,
                                             const vec4_t color,
                                             uint32_t width, uint32_t height)
{
    if (!vk.render_pass_active || !pipeline || !texture->descriptor_set)
        return;

    vk_draw_push_t push = {
        .rect = { 0, 0, width, height },
        .color = { color[0], color[1], color[2], color[3] },
        .screen = { width, height },
        .uv = { 0.0f, 0.0f, 1.0f, 1.0f },
    };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
    c.trisDrawn += 2;
    c.batchesDrawn2D++;
    c.picsDrawn2D++;
}

static float vk_projection_zfar(int rdflags)
{
    if ((rdflags & RDF_NOWORLDMODEL) || vk.world.size <= 0.0f)
        return 2048.0f;

    return vk.world.size * 2.0f;
}

static void vk_projection_matrix(mat4_t m, float fov_x, float fov_y, int rdflags)
{
    const float znear = vk_znear ? Cvar_ClampValue(vk_znear, 0.1f, 4095.0f) : 2.0f;
    const float zfar = max(vk_projection_zfar(rdflags), znear + 1.0f);
    float xmax = tanf(fov_x * (M_PIf / 360.0f));
    float ymax = tanf(fov_y * (M_PIf / 360.0f));

    memset(m, 0, sizeof(mat4_t));
    m[0] = xmax ? 1.0f / xmax : 1.0f;
    m[5] = ymax ? -1.0f / ymax : -1.0f;
    m[10] = zfar / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (znear * zfar) / (znear - zfar);
}

static void vk_view_matrix(mat4_t matrix, const refdef_t *fd)
{
    vec3_t axis[3];

    AnglesToAxis(fd->viewangles, axis);

    memset(matrix, 0, sizeof(mat4_t));
    matrix[0] = -axis[1][0];
    matrix[4] = -axis[1][1];
    matrix[8] = -axis[1][2];
    matrix[12] = DotProduct(axis[1], fd->vieworg);

    matrix[1] = axis[2][0];
    matrix[5] = axis[2][1];
    matrix[9] = axis[2][2];
    matrix[13] = -DotProduct(axis[2], fd->vieworg);

    matrix[2] = -axis[0][0];
    matrix[6] = -axis[0][1];
    matrix[10] = -axis[0][2];
    matrix[14] = DotProduct(axis[0], fd->vieworg);

    matrix[15] = 1.0f;
}

static void vk_entity_projection_matrix(mat4_t matrix, const refdef_t *fd,
                                        const entity_t *ent)
{
    extern cvar_t *cl_adjustfov;
    extern cvar_t *cl_gun;
    extern cvar_t *cl_gunfov;
    extern cvar_t *info_hand;
    extern float V_CalcFov(float fov_x, float width, float height);

    float fov_x = fd->fov_x;
    float fov_y = fd->fov_y;
    float reflect_x = 1.0f;

    if (ent->flags & RF_WEAPONMODEL) {
        if (cl_gunfov && cl_gunfov->value > 0.0f) {
            fov_x = Cvar_ClampValue(cl_gunfov, 30.0f, 160.0f);
            if (cl_adjustfov && cl_adjustfov->integer) {
                fov_y = V_CalcFov(fov_x, 4.0f, 3.0f);
                fov_x = V_CalcFov(fov_y, fd->height, fd->width);
            } else {
                fov_y = V_CalcFov(fov_x, fd->width, fd->height);
            }
        }

        if ((info_hand && cl_gun && info_hand->integer == 1 && cl_gun->integer == 1) ||
            (cl_gun && cl_gun->integer == 3))
            reflect_x = -1.0f;
    }

    vk_projection_matrix(matrix, fov_x, fov_y, fd->rdflags);
    matrix[0] *= reflect_x;
}

static void vk_matrix_multiply(mat4_t out, const mat4_t a, const mat4_t b)
{
    mat4_t tmp;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i + j * 4] =
                a[i + 0 * 4] * b[0 + j * 4] +
                a[i + 1 * 4] * b[1 + j * 4] +
                a[i + 2 * 4] * b[2 + j * 4] +
                a[i + 3 * 4] * b[3 + j * 4];
        }
    }

    memcpy(out, tmp, sizeof(tmp));
}

static void vk_entity_model_matrix(mat4_t model, const entity_t *ent,
                                   const vec3_t axis[3])
{
    memset(model, 0, sizeof(mat4_t));
    model[0] = axis[0][0];
    model[1] = axis[0][1];
    model[2] = axis[0][2];
    model[4] = axis[1][0];
    model[5] = axis[1][1];
    model[6] = axis[1][2];
    model[8] = axis[2][0];
    model[9] = axis[2][1];
    model[10] = axis[2][2];
    model[12] = ent->origin[0];
    model[13] = ent->origin[1];
    model[14] = ent->origin[2];
    model[15] = 1.0f;
}

static void vk_world_mvp(mat4_t out, const refdef_t *fd)
{
    mat4_t proj, view;

    vk_projection_matrix(proj, fd->fov_x, fd->fov_y, fd->rdflags);
    vk_view_matrix(view, fd);
    vk_matrix_multiply(out, proj, view);
}

static void vk_entity_axis(const entity_t *ent, vec3_t axis[3])
{
    if (VectorEmpty(ent->angles)) {
        VectorSet(axis[0], 1.0f, 0.0f, 0.0f);
        VectorSet(axis[1], 0.0f, 1.0f, 0.0f);
        VectorSet(axis[2], 0.0f, 0.0f, 1.0f);
    } else {
        AnglesToAxis(ent->angles, axis);
    }

    if (ent->scale && ent->scale != 1.0f) {
        VectorScale(axis[0], ent->scale, axis[0]);
        VectorScale(axis[1], ent->scale, axis[1]);
        VectorScale(axis[2], ent->scale, axis[2]);
    }
}

static void vk_entity_mvp(mat4_t out, const refdef_t *fd,
                          const entity_t *ent, const vec3_t axis[3])
{
    mat4_t proj, view, model, view_model;

    vk_entity_model_matrix(model, ent, axis);

    vk_entity_projection_matrix(proj, fd, ent);
    vk_view_matrix(view, fd);
    vk_matrix_multiply(view_model, view, model);
    vk_matrix_multiply(out, proj, view_model);
}

static void vk_model_mvp(mat4_t out, const refdef_t *fd, const mat4_t model)
{
    mat4_t proj, view, view_model;

    vk_projection_matrix(proj, fd->fov_x, fd->fov_y, fd->rdflags);
    vk_view_matrix(view, fd);
    vk_matrix_multiply(view_model, view, model);
    vk_matrix_multiply(out, proj, view_model);
}

static vk_alias_lerp_t vk_alias_lerp_for_entity(const vk_model_t *model,
                                                const entity_t *ent,
                                                const refdef_t *fd)
{
    vk_alias_lerp_t lerp;

    lerp.backlerp = Q_clipf(ent->backlerp, 0.0f, 1.0f);

    if (fd && fd->extended) {
        lerp.frame = ent->frame % model->frame_count;
        lerp.oldframe = ent->oldframe % model->frame_count;
    } else {
        lerp.frame = ent->frame;
        if (lerp.frame >= (uint32_t)model->frame_count) {
            Com_DPrintf("%s: no such frame: %u\n", __func__, lerp.frame);
            lerp.frame = 0;
        }

        lerp.oldframe = ent->oldframe;
        if (lerp.oldframe >= (uint32_t)model->frame_count) {
            Com_DPrintf("%s: no such oldframe: %u\n", __func__, lerp.oldframe);
            lerp.oldframe = 0;
        }
    }

    if (lerp.backlerp == 0.0f)
        lerp.oldframe = lerp.frame;

    return lerp;
}

static bool vk_alias_model_culled(const vk_model_t *model, const entity_t *ent,
                                  const vec3_t axis[3], uint32_t frame,
                                  uint32_t oldframe)
{
    vec3_t bounds[2];
    vec3_t points[8];

    if (!vk_cull_models || !vk_cull_models->integer)
        return false;
    if (!model || !model->alias_frames || !ent || !axis)
        return false;
    if (ent->flags & RF_WEAPONMODEL)
        return false;

    if (frame >= (uint32_t)model->frame_count ||
        oldframe >= (uint32_t)model->frame_count)
        return false;

    if (frame == oldframe) {
        VectorCopy(model->alias_frames[frame].bounds[0], bounds[0]);
        VectorCopy(model->alias_frames[frame].bounds[1], bounds[1]);
    } else {
        UnionBounds(model->alias_frames[frame].bounds,
                    model->alias_frames[oldframe].bounds, bounds);
    }

    if (!VectorEmpty(ent->angles) || (ent->scale && ent->scale != 1.0f)) {
        float radius = max(model->alias_frames[frame].radius,
                           model->alias_frames[oldframe].radius);
        bool clipped = false;
        radius *= ent->scale ? ent->scale : 1.0f;

        for (int i = 0; i < 4; i++) {
            float dist = PlaneDiffFast(ent->origin, &vk.world.frustum[i]);

            if (dist < -radius) {
                c.spheresCulled++;
                return true;
            }
            if (dist <= radius)
                clipped = true;
        }

        if (!clipped)
            return false;
    }

    for (int i = 0; i < 8; i++) {
        VectorCopy(ent->origin, points[i]);
        VectorMA(points[i], bounds[(i >> 0) & 1][0], axis[0], points[i]);
        VectorMA(points[i], bounds[(i >> 1) & 1][1], axis[1], points[i]);
        VectorMA(points[i], bounds[(i >> 2) & 1][2], axis[2], points[i]);
    }

    for (int i = 0; i < 4; i++) {
        bool infront = false;

        for (int j = 0; j < 8; j++) {
            if (PlaneDiffFast(points[j], &vk.world.frustum[i]) >= 0.0f) {
                infront = true;
                break;
            }
        }

        if (!infront) {
            if (!VectorEmpty(ent->angles) || (ent->scale && ent->scale != 1.0f))
                c.rotatedBoxesCulled++;
            else
                c.boxesCulled++;
            return true;
        }
    }

    return false;
}

static bool vk_create_skybox_mesh(void)
{
    const float size = 2048.0f;
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    vk_vertex_t vertices[24];
    uint32_t indices[36];
    static const int8_t st_to_vec[6][3] = {
        { 3, -1, 2 },
        { -3, 1, 2 },
        { 1, 3, 2 },
        { -1, -3, 2 },
        { -2, -1, 3 },
        { 2, -1, -3 },
    };
    static const float st[4][2] = {
        { 1.0f, -1.0f },
        { -1.0f, -1.0f },
        { 1.0f, 1.0f },
        { -1.0f, 1.0f },
    };

    for (uint32_t face = 0; face < 6; face++) {
        for (uint32_t vert = 0; vert < 4; vert++) {
            vk_vertex_t *dst = &vertices[face * 4 + vert];
            float b[3] = { st[vert][0] * size, st[vert][1] * size, size };

            for (uint32_t j = 0; j < 3; j++) {
                int k = st_to_vec[face][j];

                dst->position[j] = k < 0 ? -b[-k - 1] : b[k - 1];
            }

            memcpy(dst->color, white, sizeof(dst->color));
            dst->uv[0] = (st[vert][0] + 1.0f) * 0.5f;
            dst->uv[1] = 1.0f - (st[vert][1] + 1.0f) * 0.5f;
        }

        indices[face * 6 + 0] = face * 4 + 0;
        indices[face * 6 + 1] = face * 4 + 1;
        indices[face * 6 + 2] = face * 4 + 2;
        indices[face * 6 + 3] = face * 4 + 2;
        indices[face * 6 + 4] = face * 4 + 1;
        indices[face * 6 + 5] = face * 4 + 3;
    }

    return vk_upload_mesh(&vk.skybox, vertices, q_countof(vertices),
                          indices, q_countof(indices));
}

static bool vk_create_sprite_quad(void)
{
    static const vk_vertex_t vertices[] = {
        { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
        { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
        { { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
        { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
    };
    static const uint32_t indices[] = { 0, 1, 2, 2, 1, 3 };
    uint32_t line_index_count;
    uint32_t *line_indices;

    if (!vk_upload_mesh(&vk.sprite_quad, vertices, q_countof(vertices),
                        indices, q_countof(indices)))
        return false;

    line_indices = vk_build_line_indices(indices, q_countof(indices),
                                         &line_index_count);
    if (!line_indices)
        return true;

    vk_destroy_buffer(&vk.sprite_quad_line_indices);
    vk.sprite_quad_line_index_count = 0;
    if (!vk_upload_buffer(&vk.sprite_quad_line_indices, line_indices,
                          sizeof(*line_indices) * line_index_count,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        Com_WPrintf("Couldn't upload Vulkan sprite outline indices\n");
        Z_Free(line_indices);
        return true;
    }

    Z_Free(line_indices);
    vk.sprite_quad_line_index_count = line_index_count;
    return true;
}

static bool vk_create_particle_buffer(void)
{
    VkDeviceSize size = sizeof(vk.particle_batch) *
        max(vk.swapchain_image_count, 1);

    if (!vk_create_buffer(size,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &vk.particle_vertices.buffer,
                          &vk.particle_vertices.memory))
        return false;

    VkResult result = vk.MapMemory(vk.device, vk.particle_vertices.memory,
                                   0, size, 0,
                                   &vk.particle_vertices_mapped);
    if (result != VK_SUCCESS) {
        vk.particle_vertices_mapped = NULL;
        vk_destroy_buffer(&vk.particle_vertices);
        return false;
    }

    return true;
}

#define VK_BEAM_POINTS 12

static bool vk_create_beam_cylinder(void)
{
    vk_vertex_t vertices[VK_BEAM_POINTS * 2];
    uint32_t indices[VK_BEAM_POINTS * 6];
    uint32_t line_index_count;
    uint32_t *line_indices;
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    for (uint32_t i = 0; i < VK_BEAM_POINTS; i++) {
        float a = (2.0f * M_PIf * i) / VK_BEAM_POINTS;
        float y = cosf(a);
        float z = sinf(a);

        VectorSet(vertices[i].position, 0.0f, y, z);
        memcpy(vertices[i].color, white, sizeof(vertices[i].color));
        VectorSet(vertices[i + VK_BEAM_POINTS].position, 1.0f, y, z);
        memcpy(vertices[i + VK_BEAM_POINTS].color, white,
               sizeof(vertices[i + VK_BEAM_POINTS].color));
    }

    for (uint32_t i = 0; i < VK_BEAM_POINTS; i++) {
        uint32_t j = (i + 1) % VK_BEAM_POINTS;

        indices[i * 6 + 0] = i;
        indices[i * 6 + 1] = i + VK_BEAM_POINTS;
        indices[i * 6 + 2] = j + VK_BEAM_POINTS;
        indices[i * 6 + 3] = i;
        indices[i * 6 + 4] = j + VK_BEAM_POINTS;
        indices[i * 6 + 5] = j;
    }

    if (!vk_upload_mesh(&vk.beam_cylinder, vertices, q_countof(vertices),
                        indices, q_countof(indices)))
        return false;

    line_indices = vk_build_line_indices(indices, q_countof(indices),
                                         &line_index_count);
    if (!line_indices)
        return true;

    vk_destroy_buffer(&vk.beam_cylinder_line_indices);
    vk.beam_cylinder_line_index_count = 0;
    if (!vk_upload_buffer(&vk.beam_cylinder_line_indices, line_indices,
                          sizeof(*line_indices) * line_index_count,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        Com_WPrintf("Couldn't upload Vulkan beam outline indices\n");
        Z_Free(line_indices);
        return true;
    }

    Z_Free(line_indices);
    vk.beam_cylinder_line_index_count = line_index_count;
    return true;
}

static bool vk_create_null_model(void)
{
    static const vk_vertex_t vertices[] = {
        { {  0.0f,  0.0f,  0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 16.0f,  0.0f,  0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { {  0.0f,  0.0f,  0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { {  0.0f, 16.0f,  0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { {  0.0f,  0.0f,  0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { {  0.0f,  0.0f, 16.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
    };
    static const uint32_t indices[] = { 0, 1, 2, 3, 4, 5 };

    return vk_upload_mesh(&vk.null_model, vertices, q_countof(vertices),
                          indices, q_countof(indices));
}

#if USE_DEBUG
static bool vk_create_debug_line_buffer(void)
{
    vk_destroy_buffer(&vk.debug_lines);
    return vk_create_buffer(sizeof(vk_vertex_t) * VK_MAX_DEBUG_LINE_VERTICES,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &vk.debug_lines.buffer, &vk.debug_lines.memory);
}

static bool vk_create_debug_text_buffers(void)
{
    vk_destroy_buffer(&vk.debug_text_vertices);
    vk_destroy_buffer(&vk.debug_text_indices);

    if (!vk_create_buffer(sizeof(vk_vertex_t) * VK_MAX_DEBUG_TEXT_VERTICES,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &vk.debug_text_vertices.buffer,
                          &vk.debug_text_vertices.memory))
        return false;

    if (!vk_create_buffer(sizeof(uint32_t) * VK_MAX_DEBUG_TEXT_INDICES,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &vk.debug_text_indices.buffer,
                          &vk.debug_text_indices.memory)) {
        vk_destroy_buffer(&vk.debug_text_vertices);
        return false;
    }

    return true;
}
#endif

static bool vk_create_particle_texture(void)
{
    int shape = vk_partshape ? Cvar_ClampInteger(vk_partshape, 0, 2) : 0;
    // The same mask is used for particles and for glare billboards.  A 16x16
    // radial mask is adequate for particles, but Vulkan magnifies it for large
    // glares and the individual alpha texels become visible.  Keep the legacy
    // hard-edged shape at its native size and generate smooth shapes at a
    // resolution suitable for billboard magnification.
    const int size = shape == 1 ? 16 : 128;
    uint32_t *pixels = Z_Malloc(sizeof(*pixels) * size * size);

    if (shape == 1) {
        memset(pixels, 0, sizeof(*pixels) * size * size);
        for (int y = 3; y <= 12; y++) {
            for (int x = 3; x <= 12; x++) {
                pixels[y * size + x] = MakeColor(255, 255, 255, 255 * 0.6f);
            }
        }
    } else {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                float fx = x - size / 2 + 0.5f;
                float fy = y - size / 2 + 0.5f;
                float f = sqrtf(fx * fx + fy * fy);
                byte alpha;

                // Preserve the legacy 16x16 mask's normalized radius.  Shape
                // 2 has a slightly tighter support than shape 0.
                float radius = (size - shape * (size / 16.0f)) * 0.5f - 0.5f;
                f = 1.0f - f / radius;
                f *= 1 << shape;
                alpha = 255 * Q_clipf(f, 0.0f, 1.0f - shape * 0.2f);
                pixels[y * size + x] = MakeColor(255, 255, 255, alpha);
            }
        }
    }

    bool uploaded = vk_upload_texture_data(&vk.particle_texture, size, size,
                                           pixels, false);
    Z_Free(pixels);
    if (!uploaded)
        return false;

    vk_update_texture_descriptor_with_sampler(&vk.particle_texture,
        vk_sampler_for_pic_flags(shape == 1 ? IF_NEAREST : IF_NONE));
    return true;
}

static bool vk_create_beam_texture(void)
{
    uint32_t pixels[16 * 16];

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            /* The shared sprite quad maps U along the beam and V across it.
             * Keep the alpha profile across the width, as GL_DrawSimpleBeam
             * does, instead of fading the beam from start to end. */
            float f = abs(y - 16 / 2) - 0.5f;
            byte alpha;

            f = 1.0f - f / (16 / 2 - 2.5f);
            alpha = 255 * Q_clipf(f, 0.0f, 1.0f);
            pixels[y * 16 + x] = MakeColor(255, 255, 255, alpha);
        }
    }

    if (!vk_upload_texture_data(&vk.beam_texture, 16, 16, pixels, false))
        return false;

    vk_update_texture_descriptor_with_sampler(&vk.beam_texture,
                                              vk.postprocess_sampler);
    return true;
}

static void vk_partshape_changed(cvar_t *self)
{
    if (vk.device && !vk_create_particle_texture()) {
        Com_WPrintf("Couldn't recreate Vulkan particle texture: %s\n",
                    Com_GetLastError());
    }
}

static bool vk_create_default_texture(void)
{
    image_t *image = R_NOTEXTURE;
    uint32_t pixels[8 * 8];

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            bool bright = ((x < 4) ^ (y < 4));
            pixels[y * 8 + x] = bright ?
                MakeColor(255, 0, 255, 255) :
                MakeColor(0, 0, 0, 255);
        }
    }

    if (!vk_upload_texture_data(&vk.textures[0], 8, 8, pixels, false))
        return false;

    strcpy(image->name, "NOTEXTURE");
    image->width = image->upload_width = 8;
    image->height = image->upload_height = 8;
    image->type = IT_WALL;
    image->flags = 0;
    image->texnum = 0;
    image->sl = 0;
    image->sh = 1;
    image->tl = 0;
    image->th = 1;
    return true;
}

static bool vk_create_shell_texture(void)
{
    uint32_t pixel = U32_WHITE;

    if (!vk_upload_texture_data(&vk.textures[1], 1, 1, &pixel, false))
        return false;

    R_SHELLTEXTURE->texnum = 1;
    return true;
}

static void vk_draw_null_model(const entity_t *ent, const refdef_t *fd)
{
    if ((ent->flags & RF_WEAPONMODEL) || !vk.render_pass_active ||
        !vk.line3d_pipeline || !vk.null_model.vertices.buffer ||
        !vk.null_model.indices.buffer)
        return;

    vec3_t axis[3];
    mat4_t mvp;
    vk_color3d_push_t push;
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    vk_entity_axis(ent, axis);
    vk_entity_mvp(mvp, fd, ent, axis);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    push.color[0] = 1.0f;
    push.color[1] = 1.0f;
    push.color[2] = 1.0f;
    push.color[3] = 1.0f;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.line3d_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.null_model.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.null_model.indices.buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, vk.null_model.index_count, 1, 0, 0, 0);
    c.trisDrawn += vk.null_model.index_count / 3;
    vk_count_batch3d();
}

#if USE_DEBUG
typedef struct {
    vk_vertex_t *vertices;
    uint32_t depth_vertices;
    uint32_t nodepth_vertices;
} vk_debug_line_build_t;

static void vk_debug_line_emit(const vec3_t start, const vec3_t end,
                               uint32_t color, qboolean depth_test,
                               void *userdata)
{
    vk_debug_line_build_t *build = userdata;
    color_t c = { .u32 = color };
    float rgba[4] = {
        c.u8[0] / 255.0f,
        c.u8[1] / 255.0f,
        c.u8[2] / 255.0f,
        c.u8[3] / 255.0f,
    };
    vk_vertex_t *dst;

    if (depth_test) {
        if (build->depth_vertices + build->nodepth_vertices + 2 >
            VK_MAX_DEBUG_LINE_VERTICES)
            return;
        dst = &build->vertices[build->depth_vertices];
        build->depth_vertices += 2;
    } else {
        if (build->depth_vertices + build->nodepth_vertices + 2 >
            VK_MAX_DEBUG_LINE_VERTICES)
            return;
        build->nodepth_vertices += 2;
        dst = &build->vertices[VK_MAX_DEBUG_LINE_VERTICES -
                               build->nodepth_vertices];
    }

    VectorCopy(start, dst[0].position);
    VectorCopy(end, dst[1].position);
    memcpy(dst[0].color, rgba, sizeof(dst[0].color));
    memcpy(dst[1].color, rgba, sizeof(dst[1].color));
}

static void vk_draw_debug_lines(const refdef_t *fd)
{
    if (!vk.render_pass_active || !vk.line3d_pipeline ||
        !vk.debug_line_pipeline || !vk.debug_lines.buffer)
        return;

    vk_vertex_t *vertices;
    VkResult result = vk.MapMemory(vk.device, vk.debug_lines.memory, 0,
                                   vk.debug_lines.size, 0,
                                   (void **)&vertices);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkMapMemory", result);
        return;
    }

    vk_debug_line_build_t build = { .vertices = vertices };
    R_EmitDebugLines(vk_debug_line_emit, &build);
    vk.UnmapMemory(vk.device, vk.debug_lines.memory);

    if (!build.depth_vertices && !build.nodepth_vertices)
        return;

    mat4_t mvp;
    vk_color3d_push_t push;
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    vk_world_mvp(mvp, fd);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    Vector4Set(push.color, 1.0f, 1.0f, 1.0f, 1.0f);

    vk_bind_vertex_buffers(cmd, 0, 1, &vk.debug_lines.buffer, &offset);
    vk_push_constants(cmd, sizeof(push), &push);

    float line_width = 1.0f;
    if (vk.CmdSetLineWidth && vk.physical_device_features.wideLines &&
        vk_debug_linewidth) {
        line_width = Cvar_ClampValue(vk_debug_linewidth,
            vk.physical_device_properties.limits.lineWidthRange[0],
            vk.physical_device_properties.limits.lineWidthRange[1]);
        vk.CmdSetLineWidth(cmd, line_width);
    }

    if (build.depth_vertices) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           vk.line3d_pipeline);
        vk.CmdDraw(cmd, build.depth_vertices, 1, 0, 0);
        vk_count_batch3d();
    }

    if (build.nodepth_vertices) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           vk.debug_line_pipeline);
        vk.CmdDraw(cmd, build.nodepth_vertices, 1,
                   VK_MAX_DEBUG_LINE_VERTICES - build.nodepth_vertices, 0);
        vk_count_batch3d();
    }

    if (line_width != 1.0f)
        vk.CmdSetLineWidth(cmd, 1.0f);
}

typedef struct {
    const refdef_t *fd;
    vec3_t viewaxis[3];
    vk_vertex_t *vertices;
    uint32_t *indices;
    uint32_t depth_vertices;
    uint32_t depth_indices;
    uint32_t nodepth_vertices;
    uint32_t nodepth_indices;
} vk_debug_text_build_t;

static void vk_debug_text_emit_char(vk_debug_text_build_t *build,
                                    const vec3_t pos, const vec3_t right,
                                    const vec3_t down, uint32_t color,
                                    int ch, qboolean depth_test)
{
    if ((ch & 127) == 32)
        return;

    if (build->depth_vertices + build->nodepth_vertices + 4 >
        VK_MAX_DEBUG_TEXT_VERTICES)
        return;
    if (build->depth_indices + build->nodepth_indices + 6 >
        VK_MAX_DEBUG_TEXT_INDICES)
        return;

    uint32_t base, first_index;
    if (depth_test) {
        base = build->depth_vertices;
        first_index = build->depth_indices;
        build->depth_vertices += 4;
        build->depth_indices += 6;
    } else {
        build->nodepth_vertices += 4;
        build->nodepth_indices += 6;
        base = VK_MAX_DEBUG_TEXT_VERTICES - build->nodepth_vertices;
        first_index = VK_MAX_DEBUG_TEXT_INDICES - build->nodepth_indices;
    }

    color_t c = { .u32 = color };
    float rgba[4] = {
        c.u8[0] / 255.0f,
        c.u8[1] / 255.0f,
        c.u8[2] / 255.0f,
        c.u8[3] / 255.0f,
    };
    float s = (ch & 15) * 0.0625f;
    float t = (ch >> 4) * 0.0625f;
    vk_vertex_t *dst = &build->vertices[base];
    uint32_t *indices = &build->indices[first_index];

    VectorCopy(pos, dst[0].position);
    VectorAdd(pos, right, dst[1].position);
    VectorAdd(dst[1].position, down, dst[2].position);
    VectorAdd(pos, down, dst[3].position);

    dst[0].uv[0] = s;
    dst[0].uv[1] = t;
    dst[1].uv[0] = s + 0.0625f;
    dst[1].uv[1] = t;
    dst[2].uv[0] = s + 0.0625f;
    dst[2].uv[1] = t + 0.0625f;
    dst[3].uv[0] = s;
    dst[3].uv[1] = t + 0.0625f;

    for (int i = 0; i < 4; i++)
        memcpy(dst[i].color, rgba, sizeof(dst[i].color));

    indices[0] = base + 0;
    indices[1] = base + 2;
    indices[2] = base + 3;
    indices[3] = base + 0;
    indices[4] = base + 1;
    indices[5] = base + 2;
}

static void vk_debug_text_emit_line(vk_debug_text_build_t *build,
                                    const vec3_t origin, const vec3_t right,
                                    const vec3_t down, const char *s,
                                    size_t len, uint32_t color,
                                    qboolean depth_test)
{
    vec3_t pos;

    if (!len)
        return;

    VectorMA(origin, -0.5f * len, right, pos);
    while (len--) {
        byte ch = *s++;
        vk_debug_text_emit_char(build, pos, right, down, color, ch, depth_test);
        VectorAdd(pos, right, pos);
    }
}

static void vk_debug_text_emit(const vec3_t origin, const vec3_t angles,
                               const char *text, float size, uint32_t color,
                               qboolean depth_test, qboolean oriented,
                               void *userdata)
{
    vk_debug_text_build_t *build = userdata;
    vec3_t right, down, pos;
    const char *s, *p;

    VectorSubtract(origin, build->fd->vieworg, pos);
    if (vk_debug_distfrac &&
        size < DotProduct(pos, build->viewaxis[0]) * vk_debug_distfrac->value)
        return;

    if (oriented) {
        vec3_t up;
        AngleVectors(angles, NULL, right, up);
        VectorScale(right, size, right);
        VectorScale(up, -size, down);
    } else {
        VectorScale(build->viewaxis[1], -size, right);
        VectorScale(build->viewaxis[2], -size, down);
    }

    VectorCopy(origin, pos);
    s = text;
    while (*s) {
        p = strchr(s, '\n');
        if (!p) {
            vk_debug_text_emit_line(build, pos, right, down, s, strlen(s),
                                    color, depth_test);
            break;
        }
        vk_debug_text_emit_line(build, pos, right, down, s, p - s,
                                color, depth_test);
        VectorAdd(pos, down, pos);
        s = p + 1;
    }
}

static void vk_draw_debug_texts(const refdef_t *fd)
{
    if (!vk.render_pass_active || !vk.sprite_pipeline ||
        !vk.debug_text_pipeline || !vk.debug_text_vertices.buffer ||
        !vk.debug_text_indices.buffer)
        return;

    image_t *font = IMG_ForHandle(r_charset);
    if (!font)
        return;

    const vk_texture_t *texture = vk_texture_for_index(font->texnum, true);
    if (!texture)
        return;

    vk_vertex_t *vertices;
    VkResult result = vk.MapMemory(vk.device, vk.debug_text_vertices.memory, 0,
                                   vk.debug_text_vertices.size, 0,
                                   (void **)&vertices);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkMapMemory", result);
        return;
    }

    uint32_t *indices;
    result = vk.MapMemory(vk.device, vk.debug_text_indices.memory, 0,
                          vk.debug_text_indices.size, 0, (void **)&indices);
    if (result != VK_SUCCESS) {
        vk.UnmapMemory(vk.device, vk.debug_text_vertices.memory);
        vk_fail_result("vkMapMemory", result);
        return;
    }

    vk_debug_text_build_t build = {
        .fd = fd,
        .vertices = vertices,
        .indices = indices,
    };
    AnglesToAxis(fd->viewangles, build.viewaxis);
    R_EmitDebugTexts(vk_debug_text_emit, &build);

    vk.UnmapMemory(vk.device, vk.debug_text_indices.memory);
    vk.UnmapMemory(vk.device, vk.debug_text_vertices.memory);

    if (!build.depth_indices && !build.nodepth_indices)
        return;

    mat4_t mvp;
    vk_world_push_t push = { 0 };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    vk_world_mvp(mvp, fd);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    Vector4Set(push.color, 1.0f, 1.0f, 1.0f, 1.0f);
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    Vector4Clear(push.fog);
    push.intensity = 1.0f;

    vk_bind_vertex_buffers(cmd, 0, 1, &vk.debug_text_vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.debug_text_indices.buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);

    if (build.depth_indices) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           vk.sprite_pipeline);
        vk.CmdDrawIndexed(cmd, build.depth_indices, 1, 0, 0, 0);
        c.trisDrawn += build.depth_indices / 3;
        vk_count_batch3d();
    }

    if (build.nodepth_indices) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           vk.debug_text_pipeline);
        vk.CmdDrawIndexed(cmd, build.nodepth_indices, 1,
                          VK_MAX_DEBUG_TEXT_INDICES - build.nodepth_indices,
                          0, 0);
        c.trisDrawn += build.nodepth_indices / 3;
        vk_count_batch3d();
    }
}
#endif

static float vk_world_face_alpha(const mface_t *face)
{
    if (face->drawflags & SURF_TRANS33)
        return 0.33f;
    if (face->drawflags & SURF_TRANS66)
        return 0.66f;
    // Warp liquids are opaque textures by default, but their surface is
    // meant to be viewed as a translucent volume (water/lava/slime).
    if (face->drawflags & SURF_WARP)
        return 0.66f;
    return 1.0f;
}

#define VK_BACKFACE_EPSILON 0.01f

static bool vk_world_face_backfacing(const mface_t *face, const vec3_t vieworg)
{
    vec_t dot = PlaneDiffFast(vieworg, face->plane);

    return (face->drawflags & DSURF_PLANEBACK) ?
        dot > VK_BACKFACE_EPSILON : dot < -VK_BACKFACE_EPSILON;
}

static bool vk_world_face_in_pass(const mface_t *face, vk_world_pass_t pass)
{
    bool translucent = face->drawflags & (SURF_TRANS_MASK | SURF_WARP);

    if (pass == VK_WORLD_ENTITY_ALPHA)
        return true;
    return pass == VK_WORLD_ALPHA ? translucent : !translucent;
}

static VkPipeline vk_world_face_pipeline(const mface_t *face, VkPipeline pipeline,
                                         vk_world_pass_t pass)
{
    if (pass == VK_WORLD_OPAQUE && (face->drawflags & SURF_ALPHATEST) &&
        vk.world_alpha_pipeline) {
        return vk.world_alpha_pipeline;
    }

    return pipeline;
}

static void vk_world_face_scroll(const mface_t *face, float time, float scroll[4])
{
    float speed;

    Vector4Clear(scroll);

    if (!(face->drawflags & (SURF_FLOWING | SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y)))
        return;

    if (face->drawflags & (SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y))
        speed = 0.78125f;
    else if (face->drawflags & SURF_WARP)
        speed = 0.5f;
    else
        speed = 1.6f;

    if (face->drawflags & SURF_N64_SCROLL_FLIP)
        speed = -speed;

    speed *= time;

    if (face->drawflags & SURF_N64_SCROLL_Y)
        scroll[1] = speed;
    else
        scroll[0] = -speed;
}

static void vk_world_light_params(const mface_t *face, float color[4], float scroll[4])
{
    bool lightmap = vk_lightmap && vk_lightmap->integer;
    bool fullbright = !lightmap && vk_fullbright && vk_fullbright->integer;
    bool warp = face->drawflags & SURF_WARP;

    color[0] = 1.0f;
    color[1] = 1.0f;
    color[2] = 1.0f;
    scroll[2] = (lightmap ? 2.0f : fullbright ? 1.0f : 0.0f) +
        (warp ? 4.0f : 0.0f);
    scroll[3] = 0.0f;

    if (fullbright || (face->drawflags & vk.world.nolm_mask))
        return;

    color[0] = Cvar_ClampValue(vk_modulate, 0.0f, 1e6f) *
        Cvar_ClampValue(vk_modulate_world, 0.0f, 1e6f);
    color[1] = color[0];
    color[2] = color[0];
    scroll[3] = Cvar_ClampValue(vk_brightness, -1.0f, 1.0f);
}

static bool vk_dynamic_lights_enabled(void)
{
    return (!vk_dynamic || vk_dynamic->integer == 1) &&
           (!vk_vertexlight || !vk_vertexlight->integer);
}

static void vk_fog_params(const refdef_t *fd, float fog[4])
{
    Vector4Clear(fog);

    if (!fd || (vk_fog && !vk_fog->integer) || fd->fog.density <= 0.0f)
        return;

    VectorCopy(fd->fog.color, fog);
    fog[3] = fd->fog.density / 64.0f;
}

static void vk_height_fog_params(const refdef_t *fd, float start[4],
                                 float end[4], float view[4],
                                 float params[4])
{
    Vector4Clear(start);
    Vector4Clear(end);
    Vector4Clear(view);
    Vector4Clear(params);

    if (!fd || (vk_fog && !vk_fog->integer) ||
        fd->heightfog.density <= 0.0f || fd->heightfog.falloff <= 0.0f)
        return;

    VectorCopy(fd->heightfog.start.color, start);
    start[3] = fd->heightfog.start.dist;
    VectorCopy(fd->heightfog.end.color, end);
    end[3] = fd->heightfog.end.dist;
    VectorCopy(fd->vieworg, view);
    params[0] = fd->heightfog.density;
    params[1] = fd->heightfog.falloff;
    params[3] = 1.0f;
}

static void vk_sky_fog_params(const refdef_t *fd, float fog[4])
{
    Vector4Clear(fog);

    if (!fd || (vk_fog && !vk_fog->integer) || fd->fog.sky_factor <= 0.0f)
        return;

    VectorCopy(fd->fog.color, fog);
    fog[3] = -fd->fog.sky_factor;
}

static float vk_texture_intensity(void)
{
    return Cvar_ClampValue(vk_intensity, 1.0f, 5.0f);
}

static float vk_world_face_desaturation(const mface_t *face)
{
    if (!face || (face->drawflags & SURF_WARP))
        return 0.0f;

    return 1.0f - Cvar_ClampValue(vk_saturation, 0.0f, 1.0f);
}

static float vk_world_face_intensity(const mface_t *face)
{
    // Match the OpenGL surface state: translucent textures are not boosted by
    // the global intensity setting.
    return (face->drawflags & SURF_TRANS_MASK) ? 1.0f : vk_texture_intensity();
}

static float vk_glowmap_intensity(void)
{
    return vk_texture_intensity() *
        (gl_glowmap_intensity ? Cvar_ClampValue(gl_glowmap_intensity, 0.0f, 5.0f) : 0.75f);
}

static void vk_transform_to_entity_local(const vec3_t point, const entity_t *ent,
                                         const vec3_t axis[3], vec3_t local)
{
    vec3_t delta;
    float len0, len1, len2;

    VectorSubtract(point, ent->origin, delta);
    len0 = DotProduct(axis[0], axis[0]);
    len1 = DotProduct(axis[1], axis[1]);
    len2 = DotProduct(axis[2], axis[2]);

    local[0] = len0 ? DotProduct(delta, axis[0]) / len0 : 0.0f;
    local[1] = len1 ? DotProduct(delta, axis[1]) / len1 : 0.0f;
    local[2] = len2 ? DotProduct(delta, axis[2]) / len2 : 0.0f;
}

static float vk_world_face_light_plane_dist(const mface_t *face, const dlight_t *light,
                                            const entity_t *ent, const vec3_t axis[3])
{
    if (!ent || !axis)
        return PlaneDiffFast(light->origin, face->plane);

    vec3_t local;

    vk_transform_to_entity_local(light->origin, ent, axis, local);
    return PlaneDiffFast(local, face->plane);
}

static void vk_world_face_light_origin(const dlight_t *light, const entity_t *ent,
                                       const vec3_t axis[3], vec3_t origin)
{
    if (!ent || !axis) {
        VectorCopy(light->origin, origin);
        return;
    }

    vk_transform_to_entity_local(light->origin, ent, axis, origin);
}

static float vk_world_dynamic_light_fraction(const dlight_t *light,
                                             const mface_t *face,
                                             const vec3_t origin,
                                             float plane_dist)
{
    float rad = light->intensity - fabsf(plane_dist);
    float minlight, scale, dist;

    if (rad < DLIGHT_CUTOFF)
        return 0.0f;

    if (vk_dlight_falloff && vk_dlight_falloff->integer) {
        minlight = rad - DLIGHT_CUTOFF * 0.8f;
        scale = rad / minlight;
    } else {
        minlight = rad - DLIGHT_CUTOFF;
        scale = 1.0f;
    }

    vec3_t point;
    vec2_t local;
    float s, t, sd, td;

    VectorMA(origin, -plane_dist, face->plane->normal, point);
    local[0] = DotProduct(point, face->lm_axis[0]) + face->lm_offset[0];
    local[1] = DotProduct(point, face->lm_axis[1]) + face->lm_offset[1];

    s = Q_clipf(local[0], 0.0f, max(face->lm_width - 1, 0));
    t = Q_clipf(local[1], 0.0f, max(face->lm_height - 1, 0));
    sd = fabsf(local[0] - s) * face->lm_scale[0];
    td = fabsf(local[1] - t) * face->lm_scale[1];
    dist = sd > td ? sd + td * 0.5f : td + sd * 0.5f;

    if (dist >= minlight)
        return 0.0f;

    return rad - dist * scale;
}

static bool vk_afterglow_rt_active(void)
{
#if USE_VULKAN_RAYTRACING
    return vk.raytracing_active;
#else
    return false;
#endif
}

static void vk_update_afterglow(const refdef_t *fd)
{
    float duration = vk_rt_afterglow ?
        Cvar_ClampValue(vk_rt_afterglow, 0.0f, 1.0f) : 0.0f;
    if (!fd || !vk_afterglow_rt_active() || duration <= 0.0f ||
        !vk_dynamic_lights_enabled()) {
        memset(vk.afterglow_lights, 0, sizeof(vk.afterglow_lights));
        vk.afterglow_time = fd ? fd->time : 0.0f;
        return;
    }

    if (fd->time < vk.afterglow_time) {
        memset(vk.afterglow_lights, 0, sizeof(vk.afterglow_lights));
        vk.afterglow_frame = 0;
    }
    vk.afterglow_time = fd->time;
    uint32_t frame = ++vk.afterglow_frame;
    if (!frame) {
        memset(vk.afterglow_lights, 0, sizeof(vk.afterglow_lights));
        frame = ++vk.afterglow_frame;
    }

    for (int i = 0; i < fd->num_dlights && fd->dlights; i++) {
        const dlight_t *light = &fd->dlights[i];
        float peak = max(light->color[0],
                         max(light->color[1], light->color[2]));
        if (light->intensity < 160.0f || peak <= 0.10f)
            continue;

        int best = -1;
        float best_distance = 128.0f;
        float light_color_length = sqrtf(DotProduct(light->color,
                                                     light->color));
        for (int j = 0; j < VK_RT_AFTERGLOW_LIGHTS; j++) {
            vk_rt_afterglow_light_t *cached = &vk.afterglow_lights[j];
            if (!cached->valid || cached->seen_frame == frame)
                continue;
            float age = fd->time - cached->last_seen;
            if (age < 0.0f || age > duration)
                continue;
            float cached_color_length = sqrtf(DotProduct(cached->light.color,
                                                          cached->light.color));
            float similarity = DotProduct(light->color, cached->light.color) /
                max(light_color_length * cached_color_length, 0.0001f);
            float distance = Distance(light->origin, cached->light.origin);
            if (similarity >= 0.75f && distance <= best_distance) {
                best = j;
                best_distance = distance;
            }
        }

        if (best < 0) {
            float oldest = 1e30f;
            for (int j = 0; j < VK_RT_AFTERGLOW_LIGHTS; j++) {
                vk_rt_afterglow_light_t *cached = &vk.afterglow_lights[j];
                if (!cached->valid || fd->time - cached->last_seen > duration) {
                    best = j;
                    break;
                }
                if (cached->seen_frame != frame && cached->last_seen < oldest) {
                    oldest = cached->last_seen;
                    best = j;
                }
            }
        }
        if (best < 0)
            continue;

        vk_rt_afterglow_light_t *cached = &vk.afterglow_lights[best];
        cached->light = *light;
        cached->last_seen = fd->time;
        cached->seen_frame = frame;
        cached->valid = true;
    }

    for (int i = 0; i < VK_RT_AFTERGLOW_LIGHTS; i++) {
        vk_rt_afterglow_light_t *cached = &vk.afterglow_lights[i];
        if (cached->valid && fd->time - cached->last_seen > duration)
            cached->valid = false;
    }
}

static int vk_world_insert_dynamic_light(const vk_world_face_t *face,
                                         const dlight_t *light,
                                         bool afterglow,
                                         const entity_t *ent,
                                         const vec3_t axis[3], int count,
                                         float scores[VK_WORLD_MAX_DLIGHTS],
                                         float origins[VK_WORLD_MAX_DLIGHTS][4],
                                         float colors[VK_WORLD_MAX_DLIGHTS][4])
{
    vec3_t light_origin;
    float plane_dist = vk_world_face_light_plane_dist(face->face, light,
                                                       ent, axis);
    vk_world_face_light_origin(light, ent, axis, light_origin);
    float f = vk_world_dynamic_light_fraction(light, face->face, light_origin,
                                               plane_dist);
    if (f <= 0.0f)
        return count;

    int slot = min(count, VK_WORLD_MAX_DLIGHTS - 1);
    while (slot > 0 && f > scores[slot - 1]) {
        if (origins)
            memcpy(origins[slot], origins[slot - 1], sizeof(origins[slot]));
        if (colors)
            memcpy(colors[slot], colors[slot - 1], sizeof(colors[slot]));
        scores[slot] = scores[slot - 1];
        slot--;
    }
    if (slot >= VK_WORLD_MAX_DLIGHTS ||
        (count >= VK_WORLD_MAX_DLIGHTS && f <= scores[slot]))
        return count;

    scores[slot] = f;
    if (origins) {
        VectorCopy(light_origin, origins[slot]);
        origins[slot][3] = max(light->intensity - DLIGHT_CUTOFF *
            ((vk_dlight_falloff && vk_dlight_falloff->integer) ? 0.8f : 1.0f),
            0.0f);
    }
    if (colors) {
        VectorCopy(light->color, colors[slot]);
        colors[slot][3] = afterglow ? -light->intensity : light->intensity;
    }
    return count < VK_WORLD_MAX_DLIGHTS ? count + 1 : count;
}

static int vk_world_dynamic_lights(const vk_world_face_t *face,
                                   const refdef_t *fd, const entity_t *ent,
                                   const vec3_t axis[3],
                                   float origins[VK_WORLD_MAX_DLIGHTS][4],
                                   float colors[VK_WORLD_MAX_DLIGHTS][4])
{
    float scores[VK_WORLD_MAX_DLIGHTS] = { 0.0f };
    int count = 0;

    if (origins)
        memset(origins, 0, sizeof(float) * VK_WORLD_MAX_DLIGHTS * 4);
    if (colors)
        memset(colors, 0, sizeof(float) * VK_WORLD_MAX_DLIGHTS * 4);

    if (!face || !face->face || !face->face->plane ||
        !fd || !vk_dynamic_lights_enabled() ||
        (face->face->drawflags & vk.world.nolm_mask))
        return 0;

    for (int i = 0; i < fd->num_dlights && fd->dlights; i++)
        count = vk_world_insert_dynamic_light(face, &fd->dlights[i], false,
            ent, axis, count, scores, origins, colors);

    float duration = vk_rt_afterglow ?
        Cvar_ClampValue(vk_rt_afterglow, 0.0f, 1.0f) : 0.0f;
    if (vk_afterglow_rt_active() && duration > 0.0f) {
        for (int i = 0; i < VK_RT_AFTERGLOW_LIGHTS; i++) {
            const vk_rt_afterglow_light_t *cached = &vk.afterglow_lights[i];
            if (!cached->valid || cached->seen_frame == vk.afterglow_frame)
                continue;
            float age = fd->time - cached->last_seen;
            if (age < 0.0f || age >= duration)
                continue;
            float fade = 1.0f - age / duration;
            fade *= fade;
            dlight_t light = cached->light;
            light.intensity = DLIGHT_CUTOFF +
                max(light.intensity - DLIGHT_CUTOFF, 0.0f) * fade;
            count = vk_world_insert_dynamic_light(face, &light, true, ent,
                axis, count, scores, origins, colors);
        }
    }

    return count;
}

static int vk_world_lights(const vk_world_face_t *face, const refdef_t *fd,
                           const entity_t *ent, const vec3_t axis[3],
                           float origins[VK_WORLD_MAX_DLIGHTS][4],
                           float colors[VK_WORLD_MAX_DLIGHTS][4])
{
    return vk_world_dynamic_lights(face, fd, ent, axis, origins, colors);
}

static const image_t *vk_world_face_image(const mface_t *face,
                                           const refdef_t *fd,
                                           const entity_t *ent)
{
    const mtexinfo_t *tex = face->texinfo;

    if (tex && tex->next) {
        int frame = ent ? ent->frame : (int)((fd ? fd->time : 0.0f) * 2.0f);
        int c = frame % tex->numframes;

        while (c-- > 0 && tex->next)
            tex = tex->next;
    }

    return tex ? tex->image : NULL;
}

static void vk_draw_world_outlines(const mat4_t mvp, bool marked_only,
                                   vk_world_pass_t pass, const refdef_t *fd,
                                   const entity_t *ent)
{
    if (!gl_showtris || !(gl_showtris->integer & SHOWTRIS_WORLD) ||
        !vk.render_pass_active || !vk.line3d_pipeline ||
        !vk.world.mesh.vertices.buffer || !vk.world.line_indices.buffer ||
        !vk.world.line_index_count || !vk.world.batch_count ||
        !vk.world.batches || !vk.world.faces)
        return;

    bool use_marked = marked_only || vk.world.face_count;
    vk_color3d_push_t push;
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    Vector4Set(push.color, 1.0f, 0.0f, 0.0f, 1.0f);

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.line3d_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.world.mesh.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.world.line_indices.buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);

    for (uint32_t i = 0; i < vk.world.batch_count; i++) {
        const vk_world_batch_t *batch = &vk.world.batches[i];

        if (batch->first_face > vk.world.face_count ||
            batch->face_count > vk.world.face_count - batch->first_face)
            continue;

        for (uint32_t j = 0; j < batch->face_count; j++) {
            const vk_world_face_t *face = &vk.world.faces[batch->first_face + j];
            uint32_t first_index, index_count;

            if (!face->face)
                continue;
            if (use_marked && face->face->drawframe != vk.world.drawframe)
                continue;
            if (!vk_world_face_in_pass(face->face, pass))
                continue;
            if (!vk_world_face_image(face->face, fd, ent))
                continue;

            first_index = (face->first_index / 3) * 6;
            index_count = (face->index_count / 3) * 6;
            if (!index_count || first_index + index_count > vk.world.line_index_count)
                continue;

            vk.CmdDrawIndexed(cmd, index_count, 1, first_index, 0, 0);
            vk_count_batch3d();
        }
    }
}

static bool vk_world_face_glowmap_enabled(const mface_t *face,
                                          const image_t *image)
{
    if (!vk.world_glow_pipeline || !image || !image->texnum2 ||
        image->texnum2 >= MAX_RIMAGES)
        return false;

    if (vk_lightmap && vk_lightmap->integer)
        return false;

    if (r_lava_glowmaps && !r_lava_glowmaps->integer && face->texinfo &&
        strstr(face->texinfo->name, "lava"))
        return false;

    return true;
}

static void vk_world_material(const mface_t *face, float *reflect,
                              float *roughness)
{
    *reflect = 1.0f;
    *roughness = 0.5f;
    if (!vk.world.rt_material_file_loaded)
        return;
    const vk_rt_material_rule_t *rule = face && face->texinfo ?
        vk_rt_material_rule(face->texinfo->name) : NULL;
    if (rule) {
        *reflect = rule->reflect;
        *roughness = rule->roughness;
    } else {
        // Keep SSR useful on maps whose floor textures are not present in
        // the optional allowlist.  The shader's normal mask rejects walls;
        // liquids and other translucent faces are excluded separately.
        *reflect = 0.514f;
        *roughness = 0.70f;
    }
}

static float vk_world_pack_material(float reflect, float roughness,
                                    float skylight, float environment,
                                    float sunlight)
{
    // The secondary MRT alpha remains available alongside bloom RGB. Four
    // bits per property are sufficient for broad Quake II material classes
    // and keep reflection eligibility and roughness independent.
    unsigned reflect_q = (unsigned)(min(max(reflect, 0.0f), 1.0f) * 15.0f + 0.5f);
    unsigned roughness_q = (unsigned)(min(max(roughness, 0.0f), 1.0f) * 15.0f + 0.5f);
    unsigned skylight_q = (unsigned)(min(max(skylight, 0.0f), 1.0f) *
                                     15.0f + 0.5f);
    unsigned environment_q = (unsigned)(min(max(environment, 0.0f), 1.0f) *
                                        15.0f + 0.5f);
    unsigned sunlight_q = (unsigned)(min(max(sunlight, 0.0f), 1.0f) *
                                     15.0f + 0.5f);
    return (float)((sunlight_q << 16) | (environment_q << 12) |
                   (skylight_q << 8) |
                   (reflect_q << 4) | roughness_q);
}

static float vk_world_pack_rt_controls(float specular, float bounce,
                                       float caustics, float shadow_fringe)
{
    // A float represents every 24-bit integer exactly. Seven-bit general
    // controls and five-bit effect controls retain smooth menu adjustment
    // while fitting water caustics and dynamic-shadow fringes together.
    unsigned specular_q = (unsigned)(min(max(specular, 0.0f), 1.0f) *
                                     127.0f + 0.5f);
    unsigned bounce_q = (unsigned)(min(max(bounce, 0.0f), 1.0f) *
                                   127.0f + 0.5f);
    unsigned caustics_q = (unsigned)(min(max(caustics, 0.0f), 1.0f) *
                                     31.0f + 0.5f);
    unsigned fringe_q = (unsigned)(min(max(shadow_fringe, 0.0f), 1.0f) *
                                   31.0f + 0.5f);
    return (float)(specular_q | (bounce_q << 7) | (caustics_q << 14) |
                   (fringe_q << 19));
}

static int vk_world_liquid_kind(const mface_t *face)
{
    if (!face || !(face->drawflags & SURF_WARP) || !face->texinfo)
        return 0;

    const char *name = face->texinfo->name;
    if (Q_strcasestr(name, "lava"))
        return r_lava_glowmaps && !r_lava_glowmaps->integer ? 4 : 3;
    if (Q_strcasestr(name, "slime"))
        return 2;
    return 1;
}

#if USE_VULKAN_RAYTRACING
static int vk_view_liquid_kind(const refdef_t *fd)
{
    if (!fd || !(fd->rdflags & RDF_UNDERWATER) ||
        !vk.world.cache || !vk.world.cache->nodes)
        return 0;

    const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes, fd->vieworg);
    int contents = leaf ? leaf->contents[0] : 0;
    if (contents & (CONTENTS_LAVA | CONTENTS_SLIME))
        return 0;
    return contents & CONTENTS_WATER ? 1 : 0;
}
#endif

static void vk_world_rt_params(float *params, bool pixel_world,
                               bool world_entity, const mface_t *face)
{
    params[0] = 0.0f;
    params[1] = 0.0f;
    params[2] = 0.0f;
    float specular = vk_rt_specular ?
        Cvar_ClampValue(vk_rt_specular, 0.0f, 1.0f) : 0.0f;
    float bounce = vk_rt_bounce ?
        Cvar_ClampValue(vk_rt_bounce, 0.0f, 1.0f) : 0.0f;
    float caustics = world_entity && vk.view_liquid_kind && vk_rt_caustics ?
        Cvar_ClampValue(vk_rt_caustics, 0.0f, 1.0f) : 0.0f;
    float shadow_fringe = world_entity && vk_rt_shadow_fringe ?
        Cvar_ClampValue(vk_rt_shadow_fringe, 0.0f, 1.0f) : 0.0f;
    float skylight = world_entity && vk_rt_skylight ?
        Cvar_ClampValue(vk_rt_skylight, 0.0f, 1.0f) : 0.0f;
    float environment = world_entity && vk_rt_environment ?
        Cvar_ClampValue(vk_rt_environment, 0.0f, 1.0f) : 0.0f;
    float sunlight = world_entity && vk_rt_sunlight ?
        Cvar_ClampValue(vk_rt_sunlight, 0.0f, 1.0f) : 0.0f;
    params[3] = vk_world_pack_rt_controls(specular, bounce, caustics,
                                          shadow_fringe);
    int requested_debug = vk_rt_debug ? vk_rt_debug->integer : 0;
#if USE_VULKAN_RAYTRACING
    float material_reflect, material_roughness;
    vk_world_material(face, &material_reflect, &material_roughness);
    if (face && (face->drawflags & (SURF_TRANS_MASK | SURF_WARP))) {
        material_reflect = 0.0f;
        sunlight = 0.0f;
    }
    float packed_material = vk_world_pack_material(material_reflect,
                                                   material_roughness,
                                                   skylight, environment,
                                                   sunlight);
    if (pixel_world) {
        if (requested_debug >= 1 && requested_debug <= 3) {
            params[0] = -(float)requested_debug;
        } else {
            params[0] = vk_rt_emissive ?
                Cvar_ClampValue(vk_rt_emissive, 0.0f, 2.0f) : 0.0f;
            params[1] = vk_rt_ao ?
                Cvar_ClampValue(vk_rt_ao, 0.0f, 0.5f) : 0.0f;
            if (requested_debug >= 8 && requested_debug <= 15)
                params[1] = -(float)requested_debug;
            params[2] = packed_material;
        }
    } else if (vk.raytracing_active && world_entity) {
        // This position aliases rt_enabled in vk_world_lit_push_t.
        params[0] = requested_debug >= 1 && requested_debug <= 3 ?
            -(float)requested_debug : 1.0f;
        if (requested_debug >= 8 && requested_debug <= 15)
            params[1] = -(float)requested_debug;
        params[2] = packed_material;
    }

    int liquid_kind = vk.raytracing_active && world_entity ?
        vk_world_liquid_kind(face) : 0;
    if (liquid_kind) {
        // Warp faces never enter SSR. Reuse their material channel for the
        // liquid class and retain the ordinary specular control in its
        // fractional part so strength zero reproduces the previous shader.
        params[2] = -((float)liquid_kind + specular * 0.99f);
        params[3] = vk_rt_liquids ?
            Cvar_ClampValue(vk_rt_liquids, 0.0f, 1.0f) : 0.65f;
    }
#else
    (void)pixel_world;
    (void)world_entity;
    (void)requested_debug;
#endif
}


static void vk_draw_world_mesh(const mat4_t mvp, bool marked_only,
                               VkPipeline pipeline, vk_world_pass_t pass,
                               float entity_alpha, const refdef_t *fd,
                               const entity_t *ent, const vec3_t axis[3])
{
    const vk_mesh_t *mesh = &vk.world.mesh;
    bool use_marked = marked_only || vk.world.face_count;
    bool special_light_mode = (vk_lightmap && vk_lightmap->integer) ||
        (vk_fullbright && vk_fullbright->integer) ||
        (vk_vertexlight && vk_vertexlight->integer);
    bool pixel_requested = vk_pixel_lightmap_mode() >= 2 &&
        pass == VK_WORLD_OPAQUE && !vk.drawing_bloom && !ent &&
        !special_light_mode;
    bool pixel_ready = vk.pixel_world_pipeline && vk.pixel_world_alpha_pipeline &&
        vk.pixel_world_fast_pipeline &&
        vk.pixel_world_glow_pipeline && vk.pixel_world_glow_alpha_pipeline &&
        vk.pixel_world_glow_fast_pipeline &&
        vk.pixel_world_pipeline_layout && vk.world.pixel_lmuv_buffer.buffer &&
        vk.world.pixel_lightmap_texture.descriptor_set;
    bool pixel_world = pixel_requested && pixel_ready;
#if USE_VULKAN_RAYTRACING
    bool ray_pixel = pixel_world && vk.raytracing_active &&
        vk.raytracing_pixel_ready &&
        vk.world.surface_lights_ready &&
        vk.rt_descriptor_set && vk.pixel_world_rt_pipeline &&
        vk.pixel_world_rt_alpha_pipeline && vk.pixel_world_rt_glow_pipeline &&
        vk.pixel_world_rt_glow_alpha_pipeline;
#else
    bool ray_pixel = false;
#endif
    bool world_batching = pass == VK_WORLD_OPAQUE && !ent && !vk.drawing_bloom;

    if (!vk.render_pass_active || !pipeline ||
        !mesh->vertices.buffer || !mesh->indices.buffer || !mesh->index_count ||
        !vk.world.batch_count || !vk.world.batches || !vk.world.faces)
        return;

    vk_draw_scope_t old_scope = vk.draw_scope;
    vk.draw_scope = VK_DRAW_WORLD;

    if (pixel_requested && !pixel_ready) {
        if (!vk_pixel_lightmaps_warned) {
            Com_WPrintf("Vulkan pixel lightmaps mode 2 needs vk_pixel_lightmaps 2 before video init; using mode 1\n");
            vk_pixel_lightmaps_warned = true;
        }
        Cvar_Set("vk_pixel_lightmaps", "1");
    }
    vk_world_pixel_push_t push = { 0 };
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    push.color[0] = 1.0f;
    push.color[1] = 1.0f;
    push.color[2] = 1.0f;
    push.color[3] = entity_alpha;
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    if (fd) {
        VectorCopy(fd->vieworg, push.dlight);
        push.dlight[3] = fd->time;
    }
    vk_fog_params(fd, push.fog);
    push.intensity = vk_texture_intensity();
    push.desaturation = 0.0f;
    push.lm_scale[0] = -1.0f;
    push.lm_scale[1] = -1.0f;
    vk_world_rt_params(push.rt_params, pixel_world, !ent, NULL);

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;
    VkBuffer pixel_buffers[] = {
        mesh->vertices.buffer,
        vk.world.pixel_lmuv_buffer.buffer,
        vk.world.rt_light_indices.buffer,
    };
    VkDeviceSize pixel_offsets[] = { 0, 0, 0 };
    uint32_t *batch_index_mapped = vk.world.batch_index_mapped;
    uint32_t batch_index_base = vk.current_image * vk.world.batch_index_capacity;
    uint32_t batch_index_cursor = 0;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                       ray_pixel ? vk.pixel_world_rt_pipeline :
                       (pixel_world ? vk.pixel_world_pipeline : pipeline));
#if USE_VULKAN_RAYTRACING
    if (vk.raytracing_active && vk.rt_descriptor_set) {
        if (ray_pixel)
            vk_bind_pixel_world_descriptor(cmd, 3, vk.rt_descriptor_set);
        else if (!pixel_world)
            vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     vk.rect_pipeline_layout, 1, 1,
                                     &vk.rt_descriptor_set, 0, NULL);
    }
#endif
    if (pixel_world)
        vk_bind_vertex_buffers(cmd, 0, ray_pixel ? 3 : 2,
                               pixel_buffers, pixel_offsets);
    else
        vk_bind_vertex_buffers(cmd, 0, 1, &mesh->vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    if (pixel_world) {
        vk_bind_pixel_world_descriptor(cmd, 1,
                                       vk.world.pixel_lightmap_texture.descriptor_set);
        vk_push_pixel_world_constants(cmd, sizeof(push), &push);
    } else {
        vk_push_constants(cmd, sizeof(vk_world_lit_push_t), &push);
    }

    for (uint32_t i = 0; i < vk.world.batch_count; i++) {
        const vk_world_batch_t *batch = &vk.world.batches[i];
        if (batch->texture_index >= MAX_RIMAGES)
            continue;

        const vk_texture_t *texture = vk_texture_for_index(batch->texture_index, true);
        if (!texture)
            continue;

        if (pixel_world)
            vk_bind_pixel_world_descriptor(cmd, 0, texture->descriptor_set);
        else
            vk_bind_texture_descriptor(cmd, texture->descriptor_set);
        uint32_t bound_texture_index = batch->texture_index;

        if (batch->first_face > vk.world.face_count ||
            batch->face_count > vk.world.face_count - batch->first_face)
            continue;

        uint32_t group_count = 0;
        uint32_t group_faces = 0;
        uint32_t group_tris = 0;
        bool group_active = false;
        bool group_glow = false;
        VkPipeline group_pipeline = ray_pixel ? vk.pixel_world_rt_pipeline :
            (pixel_world ? vk.pixel_world_pipeline : pipeline);
        const vk_texture_t *group_glow_texture = NULL;

#define VK_FLUSH_WORLD_GROUP() do { \
            if (group_active) { \
                bool draw_group = batch_index_mapped && \
                    batch_index_cursor <= vk.world.batch_index_capacity && \
                    group_count <= vk.world.batch_index_capacity - batch_index_cursor; \
                if (draw_group) { \
                    VkDeviceSize bind_offset = sizeof(uint32_t) * \
                        (batch_index_base + batch_index_cursor); \
                    VkDeviceSize size = sizeof(uint32_t) * group_count; \
                    memcpy(&batch_index_mapped[batch_index_base + batch_index_cursor], vk.world.batch_index_data, size); \
                    batch_index_cursor += group_count; \
                    vk_bind_index_buffer(cmd, vk.world.batch_indices.buffer, bind_offset, VK_INDEX_TYPE_UINT32); \
                } \
                vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, group_pipeline); \
                if (pixel_world) { \
                    vk_bind_pixel_world_descriptor(cmd, 0, texture->descriptor_set); \
                    vk_bind_pixel_world_descriptor(cmd, 1, \
                        vk.world.pixel_lightmap_texture.descriptor_set); \
                    if (group_glow && group_glow_texture) \
                        vk_bind_pixel_world_descriptor(cmd, 2, \
                            group_glow_texture->descriptor_set); \
                    vk_push_pixel_world_constants(cmd, sizeof(push), &push); \
                } else { \
                    vk_bind_texture_descriptor(cmd, texture->descriptor_set); \
                    vk_push_constants(cmd, sizeof(vk_world_lit_push_t), &push); \
                } \
                if (draw_group) \
                    vk.CmdDrawIndexed(cmd, group_count, 1, 0, 0, 0); \
                vk_bind_index_buffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32); \
                c.facesDrawn += group_faces; \
                c.facesTris += group_tris; \
                c.trisDrawn += group_count / 3; \
                if (draw_group) \
                    vk_count_batch3d(); \
                group_active = false; \
            } \
        } while (0)

        for (uint32_t j = 0; j < batch->face_count; j++) {
            const vk_world_face_t *face = &vk.world.faces[batch->first_face + j];
            VkPipeline face_pipeline;

            // Skipped faces do not change any GPU state. Keep an active group
            // open across BSP visibility gaps so all compatible visible faces
            // in this texture batch remain in one indexed draw.
            if (!face->face || !face->face->texinfo || !face->face->plane)
                continue;
            if (use_marked && face->face->drawframe != vk.world.drawframe)
                continue;
            if (!ent && fd &&
                vk_world_face_backfacing(face->face, fd->vieworg)) {
                c.facesCulled++;
                continue;
            }
            if (!vk_world_face_in_pass(face->face, pass))
                continue;

            const image_t *image = vk_world_face_image(face->face, fd, ent);
            if (!image || image->texnum >= MAX_RIMAGES)
                continue;

            if (vk.drawing_bloom) {
                if (!vk_world_face_glowmap_enabled(face->face, image))
                    continue;

                const vk_texture_t *glow = vk_texture_for_index(image->texnum2, false);
                if (!glow)
                    continue;

                vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   vk.world_glow_pipeline);
                vk_bind_texture_descriptor(cmd, glow->descriptor_set);
                push.intensity = vk_glowmap_intensity();
                push.color[0] = 1.0f;
                push.color[1] = 1.0f;
                push.color[2] = 1.0f;
                push.color[3] = entity_alpha * vk_world_face_alpha(face->face);
                Vector4Clear(push.dlight);
                vk_world_face_scroll(face->face, fd ? fd->time : 0.0f, push.scroll);
                vk_push_constants(cmd, sizeof(vk_world_lit_push_t), &push);
                vk.CmdDrawIndexed(cmd, face->index_count, 1, face->first_index, 0, 0);
                c.trisDrawn += face->index_count / 3;
                vk_count_batch3d();
                continue;
            }

            bool can_group = world_batching &&
                batch_index_mapped && vk.world.batch_indices.buffer &&
                vk.world.batch_index_data &&
                image->texnum == batch->texture_index &&
                !(face->face->drawflags & (SURF_ALPHATEST | SURF_FLOWING |
                                           SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y |
                                           SURF_WARP | SURF_TRANS33 | SURF_TRANS66)) &&
                !(face->face->drawflags & vk.world.nolm_mask) &&
                (!pixel_world || (face->pixel_lm_w && face->pixel_lm_h)) &&
                (pixel_world ||
                 !vk_world_face_glowmap_enabled(face->face, image));

            if (can_group) {
                can_group = !vk_world_lights(face, fd, ent, axis, NULL, NULL);
            }
            if (can_group) {
                uint32_t used_indices = batch_index_cursor + group_count;
                can_group = used_indices <= vk.world.batch_index_capacity &&
                    face->index_count <= vk.world.batch_index_capacity - used_indices;
            }

            if (can_group) {
                if (!group_active) {
                    group_count = 0;
                    group_faces = 0;
                    group_tris = 0;
                    group_active = true;

                    push.color[3] = entity_alpha;
                    Vector4Clear(push.scroll);
                    vk_world_light_params(face->face, push.color, push.scroll);
                    Vector4Clear(push.dlight);
                    if (fd)
                        VectorCopy(fd->vieworg, push.dlight);
                    push.dlight[3] = fd ? fd->time : 0.0f;
                    push.intensity = vk_texture_intensity();
                    push.desaturation = vk_world_face_desaturation(face->face);
                    group_glow = pixel_world &&
                        vk_world_face_glowmap_enabled(face->face, image);
                    group_glow_texture = group_glow ?
                        vk_texture_for_index(image->texnum2, false) : NULL;
                    group_glow = group_glow_texture != NULL;
                    if (ray_pixel)
                        group_pipeline = group_glow ?
                            vk.pixel_world_rt_glow_pipeline :
                            vk.pixel_world_rt_pipeline;
                    else
                        group_pipeline = group_glow ?
                            vk.pixel_world_glow_fast_pipeline :
                            (pixel_world ? vk.pixel_world_fast_pipeline : pipeline);
                    push.lm_scale[0] = 1.0f;
                    push.lm_scale[1] = pixel_world ? 1.0f : 0.0f;
                    vk_world_rt_params(push.rt_params, pixel_world, !ent, face->face);
                }
                for (uint32_t k = 0; k < face->edge_count - 2; k++) {
                    vk.world.batch_index_data[group_count++] = face->first_vertex;
                    vk.world.batch_index_data[group_count++] = face->first_vertex + k + 1;
                    vk.world.batch_index_data[group_count++] = face->first_vertex + k + 2;
                }
                group_faces++;
                group_tris += face->index_count / 3;
                continue;
            }

            VK_FLUSH_WORLD_GROUP();

            if (image->texnum != bound_texture_index) {
                texture = vk_texture_for_index(image->texnum, true);
                if (!texture)
                    continue;
                if (pixel_world)
                    vk_bind_pixel_world_descriptor(cmd, 0, texture->descriptor_set);
                else
                    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
                bound_texture_index = image->texnum;
            }

            bool combined_glow = pixel_world &&
                vk_world_face_glowmap_enabled(face->face, image);
            const vk_texture_t *combined_glow_texture = NULL;
            if (combined_glow) {
                combined_glow_texture =
                    vk_texture_for_index(image->texnum2, false);
                combined_glow = combined_glow_texture != NULL;
            }

            if (pixel_world) {
                if (combined_glow) {
                    if (ray_pixel)
                        face_pipeline = (face->face->drawflags & SURF_ALPHATEST) ?
                            vk.pixel_world_rt_glow_alpha_pipeline :
                            vk.pixel_world_rt_glow_pipeline;
                    else
                        face_pipeline = (face->face->drawflags & SURF_ALPHATEST) ?
                            vk.pixel_world_glow_alpha_pipeline :
                            vk.pixel_world_glow_pipeline;
                    vk_bind_pixel_world_descriptor(cmd, 2,
                                                   combined_glow_texture->descriptor_set);
                } else {
                    if (ray_pixel)
                        face_pipeline = (face->face->drawflags & SURF_ALPHATEST) ?
                            vk.pixel_world_rt_alpha_pipeline :
                            vk.pixel_world_rt_pipeline;
                    else
                        face_pipeline = (face->face->drawflags & SURF_ALPHATEST) ?
                            vk.pixel_world_alpha_pipeline : vk.pixel_world_pipeline;
                }
                vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   face_pipeline);
            } else {
                face_pipeline = vk_world_face_pipeline(face->face, pipeline, pass);
            }
            if (!pixel_world && face_pipeline != pipeline)
                vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, face_pipeline);

            push.color[3] = entity_alpha * vk_world_face_alpha(face->face);
            vk_world_face_scroll(face->face, fd ? fd->time : 0.0f, push.scroll);
            vk_world_light_params(face->face, push.color, push.scroll);
            Vector4Clear(push.dlight);
            if (fd)
                VectorCopy(fd->vieworg, push.dlight);
            vk_world_lights(face, fd, ent, axis,
                            push.dlight_origins, push.dlight_colors);
            push.dlight[3] = fd ? fd->time : 0.0f;
            push.intensity = vk_world_face_intensity(face->face);
            push.desaturation = vk_world_face_desaturation(face->face);
            push.color[3] = entity_alpha * vk_world_face_alpha(face->face);
            if (pixel_world && face->pixel_lm_w && face->pixel_lm_h) {
                push.lm_scale[0] = 1.0f;
                push.lm_scale[1] = 1.0f;
            } else {
                push.lm_scale[0] = 1.0f;
                push.lm_scale[1] = 0.0f;
            }
            vk_world_rt_params(push.rt_params, pixel_world, !ent, face->face);
            if (pixel_world)
                vk_push_pixel_world_constants(cmd, sizeof(push), &push);
            else
                vk_push_constants(cmd, sizeof(vk_world_lit_push_t), &push);
            vk.CmdDrawIndexed(cmd, face->index_count, 1, face->first_index, 0, 0);
            c.facesDrawn++;
            c.facesTris += face->index_count / 3;
            c.trisDrawn += face->index_count / 3;
            vk_count_batch3d();

            if (!combined_glow &&
                (pass == VK_WORLD_OPAQUE ||
                 (pass == VK_WORLD_ENTITY_ALPHA &&
                  !(face->face->drawflags & SURF_TRANS_MASK))) &&
                vk_world_face_glowmap_enabled(face->face, image)) {
                const vk_texture_t *glow = vk_texture_for_index(image->texnum2, false);
                if (glow) {
                    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       vk.world_glow_pipeline);
                    vk_bind_texture_descriptor(cmd, glow->descriptor_set);
                    bound_texture_index = image->texnum2;
                    push.intensity = vk_glowmap_intensity();
                    push.color[0] = 1.0f;
                    push.color[1] = 1.0f;
                    push.color[2] = 1.0f;
                    push.color[3] = entity_alpha * vk_world_face_alpha(face->face);
                    vk_push_constants(cmd, sizeof(vk_world_lit_push_t), &push);
                    vk.CmdDrawIndexed(cmd, face->index_count, 1, face->first_index, 0, 0);
                    c.trisDrawn += face->index_count / 3;
                    vk_count_batch3d();
                    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       face_pipeline);
                    if (pixel_world) {
                        vk_bind_pixel_world_descriptor(cmd, 0, texture->descriptor_set);
                        vk_bind_pixel_world_descriptor(cmd, 1,
                                                       vk.world.pixel_lightmap_texture.descriptor_set);
                        bound_texture_index = image->texnum;
                    }
                }
            }

            if (!pixel_world && face_pipeline != pipeline)
                vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        }

        VK_FLUSH_WORLD_GROUP();
#undef VK_FLUSH_WORLD_GROUP
    }

    vk.draw_scope = old_scope;
}

static void vk_draw_fx_outlines(const vk_mesh_t *mesh, const vk_buffer_t *indices,
                                uint32_t index_count, const mat4_t mvp)
{
    if (!gl_showtris || !(gl_showtris->integer & SHOWTRIS_FX) ||
        !vk.render_pass_active || !vk.line3d_pipeline || !mesh || !indices ||
        !mesh->vertices.buffer || !indices->buffer || !index_count)
        return;

    vk_color3d_push_t push;
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    Vector4Set(push.color, 1.0f, 0.0f, 0.0f, 1.0f);

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.line3d_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &mesh->vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, indices->buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);
    vk_count_batch3d();
}

static void vk_sky_mvp(mat4_t out, const refdef_t *fd)
{
    refdef_t sky_fd = *fd;
    mat4_t proj, view;

    VectorClear(sky_fd.vieworg);
    vk_projection_matrix(proj, sky_fd.fov_x, sky_fd.fov_y, sky_fd.rdflags);
    vk_view_matrix(view, &sky_fd);

    if (vk.sky_rotate) {
        vec3_t axis[3];
        mat4_t model, view_model;
        float angle = vk.sky_autorotate ? sky_fd.time * vk.sky_rotate :
            vk.sky_rotate;

        SetupRotationMatrix(axis, vk.sky_axis, angle);
        memset(model, 0, sizeof(model));
        model[0] = axis[0][0];
        model[1] = axis[0][1];
        model[2] = axis[0][2];
        model[4] = axis[1][0];
        model[5] = axis[1][1];
        model[6] = axis[1][2];
        model[8] = axis[2][0];
        model[9] = axis[2][1];
        model[10] = axis[2][2];
        model[15] = 1.0f;

        vk_matrix_multiply(view_model, view, model);
        vk_matrix_multiply(out, proj, view_model);
        return;
    }

    vk_matrix_multiply(out, proj, view);
}

static void vk_draw_skybox(const refdef_t *fd)
{
    if (vk_drawsky && !vk_drawsky->integer)
        return;
    // The water surface is translucent, but the above-water PVS can be
    // intentionally absent from an underwater view. Do not let that surface
    // reveal the global sky background by itself.
    if (fd && (fd->rdflags & RDF_UNDERWATER))
        return;
    if (!vk.world.sky_visible)
        return;
    if (!vk.render_pass_active || !vk.sky_pipeline ||
        !vk.skybox.vertices.buffer || !vk.skybox.indices.buffer)
        return;

    mat4_t mvp;
    vk_world_push_t push = { 0 };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    vk_sky_mvp(mvp, fd);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    push.color[0] = 1.0f;
    push.color[1] = 1.0f;
    push.color[2] = 1.0f;
    push.color[3] = 1.0f;
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    vk_sky_fog_params(fd, push.fog);
    push.intensity = 1.0f;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.sky_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.skybox.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.skybox.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);

    for (uint32_t face = 0; face < 6; face++) {
        if (vk.sky_cubemap >= 0 && vk.sky_cubemap < VK_MAX_CUBEMAPS) {
            const vk_cubemap_t *cubemap = &vk.cubemaps[vk.sky_cubemap];
            const vk_texture_t *texture = &cubemap->faces[face];
            if (cubemap->image && texture->descriptor_set) {
                vk_bind_texture_descriptor(cmd, texture->descriptor_set);
                vk.CmdDrawIndexed(cmd, 6, 1, face * 6, 0, 0);
                c.trisDrawn += 2;
                vk_count_batch3d();
            }
            continue;
        }

        uint32_t texture_index = vk.sky_images[face];

        if (!texture_index || texture_index >= MAX_RIMAGES)
            continue;

        const vk_texture_t *texture = vk_texture_for_index(texture_index, true);
        if (!texture)
            continue;

        vk_bind_texture_descriptor(cmd, texture->descriptor_set);
        vk.CmdDrawIndexed(cmd, 6, 1, face * 6, 0, 0);
        c.trisDrawn += 2;
        vk_count_batch3d();
    }
}

static void vk_setup_world_frustum(const refdef_t *fd)
{
    vec3_t axis[3], forward, left, up;
    vec_t angle, sf, cf;

    AnglesToAxis(fd->viewangles, axis);

    angle = DEG2RAD(fd->fov_x / 2);
    sf = sinf(angle);
    cf = cosf(angle);

    VectorScale(axis[0], sf, forward);
    VectorScale(axis[1], cf, left);
    VectorAdd(forward, left, vk.world.frustum[0].normal);
    VectorSubtract(forward, left, vk.world.frustum[1].normal);

    angle = DEG2RAD(fd->fov_y / 2);
    sf = sinf(angle);
    cf = cosf(angle);

    VectorScale(axis[0], sf, forward);
    VectorScale(axis[2], cf, up);
    VectorAdd(forward, up, vk.world.frustum[2].normal);
    VectorSubtract(forward, up, vk.world.frustum[3].normal);

    for (int i = 0; i < 4; i++) {
        cplane_t *plane = &vk.world.frustum[i];
        plane->dist = DotProduct(fd->vieworg, plane->normal);
        plane->type = PLANE_NON_AXIAL;
        SetPlaneSignbits(plane);
    }
}

#define VK_NODE_CLIPPED     0
#define VK_NODE_UNCLIPPED   MASK(4)

static bool vk_clip_world_node(const mnode_t *node, int *clipflags)
{
    int flags = *clipflags;

    if (flags == VK_NODE_UNCLIPPED)
        return true;

    for (int i = 0, mask = 1; i < 4; i++, mask <<= 1) {
        box_plane_t bits;

        if (flags & mask)
            continue;

        bits = BoxOnPlaneSide(node->mins, node->maxs, &vk.world.frustum[i]);
        if (bits == BOX_BEHIND) {
            c.nodesCulled++;
            return false;
        }
        if (bits == BOX_INFRONT)
            flags |= mask;
    }

    *clipflags = flags;
    return true;
}

static void vk_mark_world_leaf(const mleaf_t *leaf, const refdef_t *fd)
{
    if (!leaf || (leaf->numleaffaces > 0 && !leaf->firstleafface))
        return;
    if (leaf->contents[0] == CONTENTS_SOLID)
        return;

    if (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area))
        return;

    for (int i = 0; i < leaf->numleaffaces; i++) {
        mface_t *face = leaf->firstleafface[i];

        if (!face)
            continue;
        if (face->drawflags & SURF_SKY)
            vk.world.sky_visible = true;
        face->drawframe = vk.world.drawframe;
    }

    c.leavesDrawn++;
}

static void vk_mark_world_visible_nodes(const refdef_t *fd)
{
    bsp_t *bsp = vk.world.cache;
    const mleaf_t *leaf;
    visrow_t vis1, vis2;
    int cluster1, cluster2;
    vec3_t tmp;

    if (!bsp || !bsp->nodes || !bsp->leafs)
        return;

    glr.nodes_visible = 0;

    if (vk_lockpvs && vk_lockpvs->integer)
        return;

    leaf = BSP_PointLeaf(bsp->nodes, fd->vieworg);
    if (!leaf)
        return;
    cluster1 = cluster2 = leaf->cluster;
    // Preserve cross-boundary visibility from air into liquid, but never
    // import the above-water PVS while the camera is still underwater.
    if (!(fd->rdflags & RDF_UNDERWATER)) {
        VectorCopy(fd->vieworg, tmp);
        if (!leaf->contents[0])
            tmp[2] -= 16;
        else
            tmp[2] += 16;
        leaf = BSP_PointLeaf(bsp->nodes, tmp);
        if (!leaf)
            return;
        if (!(leaf->contents[0] & CONTENTS_SOLID))
            cluster2 = leaf->cluster;
    }

    if (!bsp->vis || (vk_novis && vk_novis->integer) || cluster1 == -1) {
        for (int i = 0; i < bsp->numleafs; i++)
            bsp->leafs[i].visframe = vk.world.visframe;
        for (int i = 0; i < bsp->numnodes; i++)
            bsp->nodes[i].visframe = vk.world.visframe;
        glr.nodes_visible = bsp->numnodes;
        return;
    }

    BSP_ClusterVis(bsp, &vis1, cluster1, DVIS_PVS);
    if (cluster2 != -1 && cluster1 != cluster2) {
        BSP_ClusterVis(bsp, &vis2, cluster2, DVIS_PVS);
        int longs = VIS_FAST_LONGS(bsp->visrowsize);
        for (int i = 0; i < longs; i++)
            vis1.l[i] |= vis2.l[i];
    }

    for (int i = 0; i < bsp->numleafs; i++) {
        leaf = &bsp->leafs[i];
        int cluster = leaf->cluster;

        if (cluster == -1)
            continue;
        if (!Q_IsBitSet(vis1.b, cluster))
            continue;

        for (mnode_t *node = (mnode_t *)leaf;
             node && node->visframe != vk.world.visframe;
             node = node->parent) {
            node->visframe = vk.world.visframe;
            glr.nodes_visible++;
        }
    }
}

static void vk_update_world_view(const refdef_t *fd)
{
    const bsp_t *bsp = vk.world.cache;
    const mleaf_t *leaf;

    VectorCopy(fd->vieworg, vk.world.vieworg);
    vk.world.viewcluster = -1;

    if (!bsp || !bsp->nodes)
        return;

    leaf = BSP_PointLeaf(bsp->nodes, fd->vieworg);
    if (leaf)
        vk.world.viewcluster = leaf->cluster;
}

static void vk_mark_world_node_faces(const mnode_t *node, const refdef_t *fd, int clipflags)
{
    if (!node)
        return;

    while (node && node->visframe == vk.world.visframe) {
        int side;
        vec_t dot;

        if (!vk_clip_world_node(node, &clipflags))
            break;

        if (!node->plane) {
            vk_mark_world_leaf((const mleaf_t *)node, fd);
            break;
        }

        c.nodesDrawn++;

        dot = PlaneDiffFast(fd->vieworg, node->plane);
        side = dot < 0;

        if (node->children[side])
            vk_mark_world_node_faces(node->children[side], fd, clipflags);

        node = node->children[side ^ 1];
    }
}

static void vk_mark_world_faces(const refdef_t *fd)
{
    const bsp_t *bsp = vk.world.cache;
    int clipflags;

    if (!bsp || !bsp->nodes)
        return;

    vk.world.drawframe++;
    // The skybox is also the background for translucent portals. A sky face
    // behind one can be outside the visible-leaf walk, so keep the map sky
    // available and let opaque geometry mask it.
    vk.world.sky_visible = vk.world.has_sky;
    vk_update_world_view(fd);
    if (!vk_lockpvs || !vk_lockpvs->integer) {
        vk.world.visframe++;
        vk_mark_world_visible_nodes(fd);
    }
    vk_setup_world_frustum(fd);

    clipflags = (!vk_cull_nodes || vk_cull_nodes->integer) ?
        VK_NODE_CLIPPED : VK_NODE_UNCLIPPED;
    vk_mark_world_node_faces(bsp->nodes, fd, clipflags);
}

static void vk_mark_bmodel_faces(mmodel_t *model, const entity_t *ent,
                                 const refdef_t *fd, const vec3_t axis[3],
                                 bool translucent)
{
    vec3_t transformed;

    if (!model || (model->numfaces > 0 && !model->firstface))
        return;

    if (!VectorEmpty(ent->angles) || (ent->scale && ent->scale != 1.0f))
        vk_transform_to_entity_local(fd->vieworg, ent, axis, transformed);
    else
        VectorSubtract(fd->vieworg, ent->origin, transformed);

    for (int i = 0; i < model->numfaces; i++) {
        mface_t *face = model->firstface + i;

        if (!face->texinfo || !face->plane)
            continue;
        if (face->drawflags & (SURF_SKY | SURF_NODRAW))
            continue;
        if (!translucent && (face->drawflags & SURF_TRANS_MASK))
            continue;
        if (!face->texinfo->image ||
            face->texinfo->image->texnum >= MAX_RIMAGES)
            continue;

        if (vk_world_face_backfacing(face, transformed)) {
            c.facesCulled++;
            continue;
        }

        face->drawframe = vk.world.drawframe;
    }
}

static bool vk_bmodel_culled(const mmodel_t *model, const entity_t *ent,
                             const vec3_t axis[3])
{
    vec3_t points[8];

    if (!vk_cull_models || !vk_cull_models->integer)
        return false;
    if (!model || !ent || !axis)
        return false;

    if (!VectorEmpty(ent->angles) || (ent->scale && ent->scale != 1.0f)) {
        float radius = model->radius * (ent->scale ? ent->scale : 1.0f);

        for (int i = 0; i < 4; i++) {
            if (PlaneDiffFast(ent->origin, &vk.world.frustum[i]) < -radius) {
                c.spheresCulled++;
                return true;
            }
        }
    }

    for (int i = 0; i < 8; i++) {
        VectorCopy(ent->origin, points[i]);
        VectorMA(points[i], (i & 1) ? model->maxs[0] : model->mins[0],
                 axis[0], points[i]);
        VectorMA(points[i], (i & 2) ? model->maxs[1] : model->mins[1],
                 axis[1], points[i]);
        VectorMA(points[i], (i & 4) ? model->maxs[2] : model->mins[2],
                 axis[2], points[i]);
    }

    for (int i = 0; i < 4; i++) {
        bool infront = false;

        for (int j = 0; j < 8; j++) {
            if (PlaneDiffFast(points[j], &vk.world.frustum[i]) >= 0.0f) {
                infront = true;
                break;
            }
        }

        if (!infront) {
            if (!VectorEmpty(ent->angles) || (ent->scale && ent->scale != 1.0f))
                c.rotatedBoxesCulled++;
            else
                c.boxesCulled++;
            return true;
        }
    }

    return false;
}

static void vk_draw_bmodel(const entity_t *ent, const refdef_t *fd,
                           bool translucent_faces)
{
    bsp_t *bsp = vk.world.cache;
    int index = ~ent->model;
    mmodel_t *model;
    vec3_t axis[3];
    mat4_t mvp;
    unsigned world_drawframe;

    if (!bsp || index < 1 || index >= bsp->nummodels)
        return;
    model = &bsp->models[index];
    if (!model->numfaces)
        return;

    vk_entity_axis(ent, axis);
    if (vk_bmodel_culled(model, ent, axis))
        return;

    vk_entity_mvp(mvp, fd, ent, axis);

    // Brush models share the BSP face array with the static world, but their
    // visibility marks are consumed immediately.  Preserve the static-world
    // generation so its PVS/frustum result remains valid for the alpha pass.
    world_drawframe = vk.world.drawframe;
    vk.world.drawframe++;
    bool translucent = (ent->flags & RF_TRANSLUCENT) || translucent_faces;
    vk_world_pass_t world_pass = translucent_faces ? VK_WORLD_ALPHA :
        (translucent ? VK_WORLD_ENTITY_ALPHA : VK_WORLD_OPAQUE);
    vk_mark_bmodel_faces(model, ent, fd, axis, translucent);
    vk_draw_world_mesh(mvp, true,
                        translucent ? vk.world_blend_pipeline : vk.world_pipeline,
                       world_pass,
                       (ent->flags & RF_TRANSLUCENT) ? ent->alpha : 1.0f,
                       fd, ent, axis);
    vk_draw_world_outlines(mvp, true, world_pass, fd, ent);
    for (int i = 0; i < model->numfaces; i++) {
        mface_t *face = model->firstface + i;
        if (face->drawframe == vk.world.drawframe)
            face->drawframe = 0;
    }
    vk.world.drawframe = world_drawframe;
}

static vk_model_t *vk_model_for_handle(qhandle_t handle)
{
    if (handle <= 0 || handle > vk.model_count)
        return NULL;

    vk_model_t *model = &vk.models[handle - 1];
    if (model->type == VK_MODEL_FREE)
        return NULL;

    return model;
}

static const image_t *vk_skin_for_model(const vk_model_t *model, const entity_t *ent)
{
    if (ent->flags & RF_SHELL_MASK)
        return R_SHELLTEXTURE;
    if (ent->skin)
        return IMG_ForHandle(ent->skin);
    if (!model->skin_count)
        return R_NOTEXTURE;
    if (ent->skinnum < 0 || ent->skinnum >= model->skin_count)
        return model->skins[0];
    if (model->skins[ent->skinnum] == R_NOTEXTURE)
        return model->skins[0];
    return model->skins[ent->skinnum];
}

static const image_t *vk_skin_for_alias_batch(const vk_model_t *model,
                                              const vk_alias_batch_t *batch,
                                              const entity_t *ent)
{
    if (ent->flags & RF_SHELL_MASK)
        return R_SHELLTEXTURE;
    if (ent->skin)
        return IMG_ForHandle(ent->skin);
    if (!batch || !batch->skin_count)
        return vk_skin_for_model(model, ent);

    int skin = ent->skinnum;
    if (skin < 0 || skin >= batch->skin_count)
        skin = 0;

    const image_t *image = model->skins[batch->skin_start + skin];
    if (image == R_NOTEXTURE && batch->skin_count > 0)
        image = model->skins[batch->skin_start];
    if (!image)
        image = R_NOTEXTURE;

    return image;
}

static void vk_add_dynamic_lights(const refdef_t *fd, const vec3_t origin, vec3_t color)
{
    if (!vk_dynamic_lights_enabled())
        return;
    if (!fd || fd->num_dlights <= 0 || !fd->dlights)
        return;

    const dlight_t *light = fd->dlights;

    for (int i = 0; i < fd->num_dlights; i++, light++) {
        float f = light->intensity - DLIGHT_CUTOFF - Distance(light->origin, origin);
        if (f > 0.0f) {
            f *= 1.0f / 255.0f;
            VectorMA(color, f, light->color, color);
        }
    }
}

static float vk_entity_light_modulate(void)
{
    return Cvar_ClampValue(vk_modulate, 0.0f, 1e6f) *
           Cvar_ClampValue(vk_modulate_entities, 0.0f, 1e6f);
}

static void vk_adjust_entity_light(vec3_t color)
{
    float add = 255.0f * Cvar_ClampValue(vk_brightness, -1.0f, 1.0f);
    float modulate = vk_entity_light_modulate();
    float scale = Cvar_ClampValue(vk_coloredlightmaps, 0.0f, 1.0f);
    float maxc;

    color[0] = max((color[0] + add) * modulate, 0.0f);
    color[1] = max((color[1] + add) * modulate, 0.0f);
    color[2] = max((color[2] + add) * modulate, 0.0f);

    maxc = max(max(color[0], color[1]), color[2]);
    if (maxc > 255.0f)
        VectorScale(color, 255.0f / maxc, color);

    if (scale != 1.0f) {
        float y = LUMINANCE(color[0], color[1], color[2]);
        color[0] = y + (color[0] - y) * scale;
        color[1] = y + (color[1] - y) * scale;
        color[2] = y + (color[2] - y) * scale;
    }

    VectorScale(color, 1.0f / 255.0f, color);
}

static void vk_entity_light_color(const entity_t *ent, const refdef_t *fd, vec4_t color)
{
    uint64_t flags = ent->flags;
    float f, m;

    if (flags & RF_SHELL_MASK) {
        VectorClear(color);
        if (flags & RF_SHELL_LITE_GREEN)
            VectorSet(color, 0.56f, 0.93f, 0.56f);
        if (flags & RF_SHELL_HALF_DAM)
            VectorSet(color, 0.56f, 0.59f, 0.45f);
        if (flags & RF_SHELL_DOUBLE) {
            color[0] = 0.9f;
            color[1] = 0.7f;
        }
        if (flags & RF_SHELL_RED)
            color[0] = 1.0f;
        if (flags & RF_SHELL_GREEN)
            color[1] = 1.0f;
        if (flags & RF_SHELL_BLUE)
            color[2] = 1.0f;
    } else if (flags & RF_FULLBRIGHT) {
        VectorSet(color, 1.0f, 1.0f, 1.0f);
    } else if ((flags & RF_IR_VISIBLE) && (fd->rdflags & RDF_IRGOGGLES)) {
        VectorSet(color, 1.0f, 0.0f, 0.0f);
    } else if (flags & RF_TRACKER) {
        VectorClear(color);
    } else {
        if ((vk_fullbright && vk_fullbright->integer) ||
            !vk_static_light_point(ent->origin, fd, color)) {
            VectorSet(color, 1.0f, 1.0f, 1.0f);
        }

        vk_add_dynamic_lights(fd, ent->origin, color);

        if (vk_doublelight_entities && vk_doublelight_entities->integer)
            VectorScale(color, vk_entity_light_modulate(), color);

        if (flags & RF_MINLIGHT) {
            f = VectorLength(color);
            if (!f)
                VectorSet(color, 0.1f, 0.1f, 0.1f);
            else if (f < 0.1f)
                VectorScale(color, 0.1f / f, color);
        }

        if (flags & RF_GLOW) {
            f = 0.1f * sinf(fd->time * 7.0f);
            for (int i = 0; i < 3; i++) {
                m = color[i] * 0.8f;
                color[i] += f;
                if (color[i] < m)
                    color[i] = m;
            }
        }

        color[0] = Q_clipf(color[0], 0.0f, 1.0f);
        color[1] = Q_clipf(color[1], 0.0f, 1.0f);
        color[2] = Q_clipf(color[2], 0.0f, 1.0f);
    }

    color[3] = (flags & RF_TRANSLUCENT) ? ent->alpha : 1.0f;
}

static void vk_alias_shadedir(const entity_t *ent, vec4_t shadedir)
{
    float yaw, cy, sy, cp, sp;

    Vector4Clear(shadedir);

    if (!vk_dotshading || !vk_dotshading->integer)
        return;
    if (ent->flags & (RF_SHELL_MASK | RF_TRACKER))
        return;

    yaw = -DEG2RAD(ent->angles[YAW]);
    cy = cosf(yaw);
    sy = sinf(yaw);
    cp = cosf(-M_PIf / 4.0f);
    sp = sinf(-M_PIf / 4.0f);

    shadedir[0] = cp * cy;
    shadedir[1] = cp * sy;
    shadedir[2] = -sp;
    shadedir[3] = 1.0f;
}

static void vk_draw_alias_pass(VkCommandBuffer cmd, VkPipeline pipeline,
                               const VkBuffer buffers[2],
                               const VkDeviceSize offsets[2],
                               const vk_model_t *model,
                               const vk_alias_batch_t *batch,
                               const vk_texture_t *texture,
                               const vk_alias_push_t *push)
{
    uint32_t first_index = batch ? batch->first_index : 0;
    uint32_t index_count = batch ? batch->index_count : model->mesh.index_count;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_vertex_buffers(cmd, 0, 2, buffers, offsets);
    vk_bind_index_buffer(cmd, model->mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(*push), push);
    vk.CmdDrawIndexed(cmd, index_count, 1, first_index, 0, 0);
    c.trisDrawn += index_count / 3;
    vk_count_batch3d();
}

static void vk_draw_alias_color_pass(VkCommandBuffer cmd, VkPipeline pipeline,
                                     const VkBuffer buffers[2],
                                     const VkDeviceSize offsets[2],
                                     const vk_model_t *model,
                                     const vk_alias_shadow_push_t *push,
                                     bool count_stats)
{
    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_vertex_buffers(cmd, 0, 2, buffers, offsets);
    vk_bind_index_buffer(cmd, model->mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(*push), push);
    vk.CmdDrawIndexed(cmd, model->mesh.index_count, 1, 0, 0, 0);
    if (count_stats) {
        c.trisDrawn += model->mesh.index_count / 3;
        vk_count_batch3d();
    }
}

static void vk_draw_alias_cel_edges(VkCommandBuffer cmd,
                                    const VkBuffer buffers[2],
                                    const VkDeviceSize offsets[2],
                                    const vk_model_t *model,
                                    const vk_alias_batch_t *batch,
                                    const vk_alias_push_t *base_push,
                                    const entity_t *ent,
                                    const refdef_t *fd,
                                    float alpha)
{
    if (!vk.alias_cel_pipeline || !vk.textures[1].descriptor_set)
        return;

    uint32_t first_index = batch ? batch->first_index : 0;
    uint32_t index_count = batch ? batch->index_count : model->mesh.index_count;
    if (!index_count)
        return;

    vk_alias_push_t push = *base_push;
    float width = Cvar_ClampValue(vk_celshading, 0.0f, 10.0f) * alpha;
    float distance = max(Distance(ent->origin, fd->vieworg), 1.0f);
    float view_height = max(fd->height, 1);
    float entity_scale = ent->scale ? fabsf(ent->scale) : 1.0f;
    float world_per_pixel = 2.0f * distance *
        tanf(DEG2RAD(fd->fov_y) * 0.5f) / view_height;

    Vector4Set(push.color, 0.0f, 0.0f, 0.0f, alpha);
    Vector4Clear(push.shadedir);
    push.shellscale = width * world_per_pixel / entity_scale;
    push.depthscale = (ent->flags & RF_DEPTHHACK) ? 0.25f : 1.0f;
    push.intensity = 1.0f;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                     vk.alias_cel_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 2, buffers, offsets);
    vk_bind_index_buffer(cmd, model->mesh.indices.buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, vk.textures[1].descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, index_count, 1, first_index, 0, 0);
    vk_count_batch3d();
}

static void vk_draw_alias_outlines(VkCommandBuffer cmd,
                                    const VkBuffer buffers[2],
                                    const VkDeviceSize offsets[2],
                                    vk_model_t *model,
                                    const vk_alias_batch_t *batch,
                                    const vk_texture_t *texture,
                                    const vk_alias_push_t *push,
                                    const entity_t *ent,
                                    const refdef_t *fd)
{
    bool showtris = gl_showtris && (gl_showtris->integer & SHOWTRIS_MESH);
    float cel_alpha = 0.0f;

    if (vk_celshading && vk_celshading->value > 0.0f && fd &&
        !(ent->flags & (RF_TRANSLUCENT | RF_SHELL_MASK | RF_TRACKER))) {
        cel_alpha = 1.0f - Distance(ent->origin, fd->vieworg) / 700.0f;
        if (cel_alpha >= 0.01f) {
            vk_draw_alias_cel_edges(cmd, buffers, offsets, model, batch,
                                    push, ent, fd, cel_alpha);
        }
    }

    if (!showtris)
        return;

    if (!vk.alias_line_pipeline || !model->alias_line_indices.buffer ||
        !model->alias_line_index_count)
        return;

    uint32_t first_index = batch ? (batch->first_index / 3) * 6 : 0;
    uint32_t index_count = batch ?
        (batch->index_count / 3) * 6 : model->alias_line_index_count;

    if (!index_count ||
        first_index + index_count > model->alias_line_index_count)
        return;

    vk_alias_push_t outline = *push;
    Vector4Set(outline.color, 0.0f, 0.0f, 0.0f, 1.0f);
    Vector4Clear(outline.shadedir);
    outline.depthscale = 0.0f;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                       vk.alias_line_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 2, buffers, offsets);
    vk_bind_index_buffer(cmd, model->alias_line_indices.buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(outline), &outline);
    vk.CmdDrawIndexed(cmd, index_count, 1, first_index, 0, 0);
    vk_count_batch3d();
}

static void vk_trace_bmodel_light_points(const refdef_t *fd, const bsp_t *bsp,
                                         const vec3_t origin, const vec3_t end,
                                         lightpoint_t *point)
{
    if (!fd)
        return;

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *ent = &fd->entities[i];
        if (!(ent->model & BIT(31)))
            continue;

        int index = ~ent->model;
        if (index < 1 || index >= bsp->nummodels)
            continue;

        const mmodel_t *model = &bsp->models[index];
        if (!model->numfaces)
            continue;

        const vec_t *angles = NULL;
        if (!VectorEmpty(ent->angles)) {
            if (fabsf(origin[0] - ent->origin[0]) > model->radius ||
                fabsf(origin[1] - ent->origin[1]) > model->radius)
                continue;
            angles = ent->angles;
        } else {
            vec3_t mins, maxs;
            VectorAdd(model->mins, ent->origin, mins);
            VectorAdd(model->maxs, ent->origin, maxs);
            if (origin[0] < mins[0] || origin[0] > maxs[0] ||
                origin[1] < mins[1] || origin[1] > maxs[1])
                continue;
        }

        lightpoint_t model_point;
        BSP_TransformedLightPoint(&model_point, origin, end,
                                  model->headnode,
                                  vk.world.nolm_mask | SURF_TRANS_MASK,
                                  ent->origin, angles);
        if (model_point.fraction < point->fraction)
            *point = model_point;
    }
}

static bool vk_alias_shadow_point(const entity_t *ent, const refdef_t *fd,
                                  lightpoint_t *point)
{
    const bsp_t *bsp = vk.world.cache;
    vec3_t end;

    if (!bsp || !bsp->nodes || !bsp->lightmap)
        return false;

    VectorCopy(ent->origin, end);
    end[2] -= 8192.0f;
    BSP_LightPoint(point, ent->origin, end, bsp->nodes,
                   vk.world.nolm_mask | SURF_TRANS_MASK);
    vk_trace_bmodel_light_points(fd, bsp, ent->origin, end, point);
    return point->surf != NULL;
}

static void vk_shadow_projection_matrix(mat4_t matrix, const cplane_t *plane,
                                        const vec3_t dir)
{
    matrix[ 0] =  plane->normal[1] * dir[1] + plane->normal[2] * dir[2];
    matrix[ 4] = -plane->normal[1] * dir[0];
    matrix[ 8] = -plane->normal[2] * dir[0];
    matrix[12] =  plane->dist * dir[0];

    matrix[ 1] = -plane->normal[0] * dir[1];
    matrix[ 5] =  plane->normal[0] * dir[0] + plane->normal[2] * dir[2];
    matrix[ 9] = -plane->normal[2] * dir[1];
    matrix[13] =  plane->dist * dir[1];

    matrix[ 2] = -plane->normal[0] * dir[2];
    matrix[ 6] = -plane->normal[1] * dir[2];
    matrix[10] =  plane->normal[0] * dir[0] + plane->normal[1] * dir[1];
    matrix[14] =  plane->dist * dir[2];

    matrix[ 3] = 0.0f;
    matrix[ 7] = 0.0f;
    matrix[11] = 0.0f;
    matrix[15] = DotProduct(plane->normal, dir);
}

static void vk_draw_alias_shadow(const entity_t *ent, const refdef_t *fd,
                                 const vec3_t axis[3],
                                 const vk_model_t *model,
                                 const VkBuffer buffers[2],
                                 const VkDeviceSize offsets[2],
                                 const vk_alias_lerp_t *lerp,
                                 bool count_stats)
{
    lightpoint_t point;
    vec3_t dir;
    float w, radius, alpha = 0.5f;
    vec4_t color;
    mat4_t proj, view, model_matrix, shadow_proj, shadow_model, view_model;
    vk_alias_shadow_push_t push;

    if (!vk_shadows || !vk_shadows->integer || !vk.alias_shadow_pipeline)
        return;
    if (ent->flags & (RF_WEAPONMODEL | RF_NOSHADOW))
        return;
    // OpenGL obtains the shadow receiver from its normal entity-lighting
    // query. Entities that bypass that query do not cast a projected shadow.
    if (ent->flags & (RF_SHELL_MASK | RF_FULLBRIGHT | RF_TRACKER))
        return;
    if ((ent->flags & RF_IR_VISIBLE) && (fd->rdflags & RDF_IRGOGGLES))
        return;
    if (!model->alias_frames || !vk_alias_shadow_point(ent, fd, &point))
        return;

    w = point.plane.normal[2];
    if (point.surf->drawflags & DSURF_PLANEBACK)
        w = -w;
    if (w < 0.5f)
        return;

    radius = model->alias_frames[lerp->frame].radius * (1.0f - lerp->backlerp) +
        model->alias_frames[lerp->oldframe].radius * lerp->backlerp;
    radius *= ent->scale ? ent->scale : 1.0f;

    if (vk_shadows->integer >= 2) {
        float dist = ent->origin[2] - point.pos[2] - radius;
        if (dist > radius * 4.0f)
            return;
        if (dist > 0.0f)
            alpha = 0.5f - dist / (radius * 8.0f);
    }

    if (vk_cull_models && vk_cull_models->integer) {
        float min_d = -radius / w;
        for (int i = 0; i < 4; i++) {
            if (PlaneDiff(point.pos, &vk.world.frustum[i]) < min_d) {
                c.shadowsCulled++;
                return;
            }
        }
    }

    if (point.surf->drawflags & DSURF_PLANEBACK)
        VectorSet(dir, 0.0f, 0.0f, -1.0f);
    else
        VectorSet(dir, 0.0f, 0.0f, 1.0f);

    vk_shadow_projection_matrix(shadow_proj, &point.plane, dir);
    vk_entity_model_matrix(model_matrix, ent, axis);
    vk_matrix_multiply(shadow_model, shadow_proj, model_matrix);
    vk_projection_matrix(proj, fd->fov_x, fd->fov_y, fd->rdflags);
    vk_view_matrix(view, fd);
    vk_matrix_multiply(view_model, view, shadow_model);
    vk_matrix_multiply(push.mvp, proj, view_model);

    vk_entity_light_color(ent, fd, color);
    Vector4Set(push.color, 0.0f, 0.0f, 0.0f, color[3] * alpha);
    Vector4Clear(push.shadedir);
    push.backlerp = lerp->backlerp;
    push.shellscale = (ent->flags & RF_SHELL_MASK) && !(ent->flags & RF_NOSHELLSCALE) ?
        POWERSUIT_SCALE : 0.0f;
    push.depthscale = (ent->flags & RF_DEPTHHACK) ? 0.25f : 1.0f;
    push._pad = 0.0f;
    vk_fog_params(fd, push.fog);
    push.intensity = 1.0f;
    VectorClear(push._pad2);
    Vector4Set(push.height_x, shadow_model[0], shadow_model[4],
               shadow_model[8], shadow_model[12]);
    Vector4Set(push.height_y, shadow_model[1], shadow_model[5],
               shadow_model[9], shadow_model[13]);
    Vector4Set(push.height_z, shadow_model[2], shadow_model[6],
               shadow_model[10], shadow_model[14]);
    vk_height_fog_params(fd, push.heightfog_start, push.heightfog_end,
                         push.heightfog_view, push.heightfog_params);

    vk_draw_alias_color_pass(vk.command_buffers[vk.current_image],
                              vk.alias_shadow_pipeline, buffers, offsets,
                              model, &push, count_stats);
}

static void vk_draw_alias_model(const entity_t *ent, const refdef_t *fd)
{
    vk_model_t *model = vk_model_for_handle(ent->model);
#if USE_MD5
    vk_model_t md5_view;
#endif

    bool translucent = ent->flags & RF_TRANSLUCENT;
    bool bloom_only = ent->flags & RF_BLOOM_ONLY;
    bool bloom_shell = (ent->flags & RF_SHELL_MASK) && !(ent->flags & RF_NOBLOOM);
    bool draw_model = !bloom_only || vk.drawing_bloom;

    if (!model || model->type != VK_MODEL_ALIAS ||
        !model->mesh.vertices.buffer || !model->mesh.indices.buffer ||
        !model->vertex_count || !model->alias_batch_count)
        return;

#if USE_MD5
    if (model->md5_mesh.vertices.buffer && model->md5_mesh.indices.buffer &&
        model->md5_frame_count && model->md5_vertex_count &&
        vk_md5_use && vk_md5_use->integer &&
        ((ent->flags & RF_NO_LOD) || !vk_md5_distance ||
         vk_md5_distance->value <= 0.0f ||
         Distance(ent->origin, fd->vieworg) <= vk_md5_distance->value)) {
        md5_view = *model;
        md5_view.mesh = model->md5_mesh;
        md5_view.alias_line_indices = model->md5_line_indices;
        md5_view.alias_line_index_count = model->md5_line_index_count;
        // Match OpenGL: replacement geometry uses the original model's frame
        // bounds for visibility and projected-shadow sizing.
        md5_view.alias_frames = model->alias_frames;
        md5_view.alias_batches = model->md5_batches;
        md5_view.skins = model->md5_skins;
        md5_view.frame_count = model->md5_frame_count;
        md5_view.alias_batch_count = model->md5_batch_count;
        md5_view.skin_count = model->md5_skin_count;
        md5_view.vertex_count = model->md5_vertex_count;
        model = &md5_view;
    }
#endif

    if (vk.drawing_bloom && !bloom_only && !bloom_shell &&
        !vk_alias_model_has_glowmap(model))
        return;

    vec3_t axis[3];
    mat4_t mvp;
    vk_alias_push_t push;
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    vk_alias_lerp_t lerp = vk_alias_lerp_for_entity(model, ent, fd);
    VkDeviceSize offsets[] = {
        (VkDeviceSize)lerp.frame * model->vertex_count * sizeof(vk_vertex_t),
        (VkDeviceSize)lerp.oldframe * model->vertex_count * sizeof(vk_vertex_t),
    };
    VkBuffer buffers[] = {
        model->mesh.vertices.buffer,
        model->mesh.vertices.buffer,
    };

    vk_entity_axis(ent, axis);
    if (vk_alias_model_culled(model, ent, axis, lerp.frame, lerp.oldframe)) {
        if (!vk.drawing_bloom)
            vk_draw_alias_shadow(ent, fd, axis, model, buffers, offsets, &lerp, true);
        return;
    }

    if (!draw_model) {
        vk_draw_alias_shadow(ent, fd, axis, model, buffers, offsets, &lerp, true);
        return;
    }

    vk_entity_mvp(mvp, fd, ent, axis);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    vk_entity_light_color(ent, fd, push.color);
    vk_alias_shadedir(ent, push.shadedir);
    push.backlerp = lerp.backlerp;
    push.shellscale = (ent->flags & RF_SHELL_MASK) && !(ent->flags & RF_NOSHELLSCALE) ?
        ((ent->flags & RF_WEAPONMODEL) ? WEAPONSHELL_SCALE : POWERSUIT_SCALE) : 0.0f;
    push.depthscale = (ent->flags & RF_DEPTHHACK) ? 0.25f : 1.0f;
    push._pad = 0.0f;
    vk_fog_params(fd, push.fog);
    push.intensity = vk_texture_intensity();
    push.desaturation = 0.0f;

    for (int i = 0; i < model->alias_batch_count; i++) {
        const vk_alias_batch_t *batch = &model->alias_batches[i];
        const image_t *skin = vk_skin_for_alias_batch(model, batch, ent);
        VkPipeline pipeline;

        if (!skin || skin->texnum >= MAX_RIMAGES)
            continue;

        const vk_texture_t *texture = vk_texture_for_index(skin->texnum, true);
        if (!texture)
            continue;

        if (vk.drawing_bloom)
            pipeline = (bloom_only || bloom_shell) ? vk.alias_bloom_pipeline : VK_NULL_HANDLE;
        else if (translucent)
            pipeline = vk.alias_blend_pipeline;
        else if ((skin->flags & IF_TRANSPARENT) && vk.alias_alpha_pipeline)
            pipeline = vk.alias_alpha_pipeline;
        else
            pipeline = vk.alias_pipeline;

        if (!vk.drawing_bloom && translucent &&
            (ent->flags & (RF_FULLBRIGHT | RF_BLOOM_ONLY)) == 0 &&
            vk.alias_depth_pipeline) {
            vk_draw_alias_pass(cmd, vk.alias_depth_pipeline, buffers, offsets,
                               model, batch, texture, &push);
        }

        if (pipeline)
            vk_draw_alias_pass(cmd, pipeline, buffers, offsets, model, batch,
                               texture, &push);
        if (skin->texnum2 && skin->texnum2 < MAX_RIMAGES &&
            vk.alias_blend_pipeline) {
            const vk_texture_t *glow = vk_texture_for_index(skin->texnum2, false);

            if (glow) {
                vk_alias_push_t glow_push = push;

                glow_push.intensity = vk_glowmap_intensity();
                vk_draw_alias_pass(cmd, vk.drawing_bloom ? vk.alias_bloom_pipeline :
                                   vk.alias_blend_pipeline, buffers, offsets,
                                   model, batch, glow, &glow_push);
            }
        }
        if (!vk.drawing_bloom)
            vk_draw_alias_outlines(cmd, buffers, offsets, model, batch,
                                   texture, &push, ent, fd);
    }


    if (!vk.drawing_bloom)
        vk_draw_alias_shadow(ent, fd, axis, model, buffers, offsets, &lerp, false);
}

static void vk_draw_sprite(const entity_t *ent, const refdef_t *fd)
{
    vk_model_t *model = vk_model_for_handle(ent->model);

    if (!model || model->type != VK_MODEL_SPRITE || !vk.sprite_pipeline ||
        !vk.sprite_quad.vertices.buffer || !vk.sprite_quad.indices.buffer)
        return;

    const vk_sprite_frame_t *frame = &model->frames[ent->frame % model->frame_count];
    if (!frame->image || frame->image->texnum >= MAX_RIMAGES)
        return;

    const vk_texture_t *texture = vk_texture_for_index(frame->image->texnum, true);
    if (!texture)
        return;

    bool translucent = ent->flags & RF_TRANSLUCENT;
    VkPipeline pipeline = vk.drawing_bloom ? vk.sprite_bloom_pipeline :
                                             vk.sprite_pipeline;

    if (!vk.drawing_bloom && !translucent &&
        (frame->image->flags & (IF_TRANSPARENT | IF_PALETTED)) ==
            (IF_TRANSPARENT | IF_PALETTED) &&
        vk.sprite_alpha_pipeline) {
        pipeline = vk.sprite_alpha_pipeline;
    }

    vec3_t viewaxis[3], left, right, down, up, xaxis, yaxis, origin;
    float scale = ent->scale ? ent->scale : 1.0f;
    mat4_t model_matrix, mvp;
    vk_world_push_t push = { 0 };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    AnglesToAxis(fd->viewangles, viewaxis);
    VectorScale(viewaxis[1], frame->origin_x * scale, left);
    VectorScale(viewaxis[1], (frame->origin_x - frame->width) * scale, right);
    VectorScale(viewaxis[2], -frame->origin_y * scale, down);
    VectorScale(viewaxis[2], (frame->height - frame->origin_y) * scale, up);
    VectorSubtract(right, left, xaxis);
    VectorSubtract(up, down, yaxis);
    VectorAdd3(ent->origin, left, down, origin);

    memset(model_matrix, 0, sizeof(model_matrix));
    model_matrix[0] = xaxis[0];
    model_matrix[1] = xaxis[1];
    model_matrix[2] = xaxis[2];
    model_matrix[4] = yaxis[0];
    model_matrix[5] = yaxis[1];
    model_matrix[6] = yaxis[2];
    model_matrix[10] = 1.0f;
    model_matrix[12] = origin[0];
    model_matrix[13] = origin[1];
    model_matrix[14] = origin[2];
    model_matrix[15] = 1.0f;

    vk_model_mvp(mvp, fd, model_matrix);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    push.color[0] = 1.0f;
    push.color[1] = 1.0f;
    push.color[2] = 1.0f;
    push.color[3] = translucent ? ent->alpha : 1.0f;
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    vk_fog_params(fd, push.fog);
    push.intensity = 1.0f;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.sprite_quad.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.sprite_quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, vk.sprite_quad.index_count, 1, 0, 0, 0);
    c.trisDrawn += vk.sprite_quad.index_count / 3;
    vk_count_batch3d();
    vk_draw_fx_outlines(&vk.sprite_quad, &vk.sprite_quad_line_indices,
                        vk.sprite_quad_line_index_count, mvp);
}

static bool vk_flare_occluded(const entity_t *ent, const refdef_t *fd)
{
    bsp_t *bsp = vk.world.cache;
    lightpoint_t point;
    vec3_t end;

    if (!bsp || !bsp->nodes)
        return false;

    VectorCopy(ent->origin, end);
    const mleaf_t *leaf = BSP_PointLeaf(bsp->nodes, end);
    if (leaf && (leaf->contents[0] & CONTENTS_SOLID)) {
        vec3_t dir;

        VectorSubtract(end, fd->vieworg, dir);
        if (VectorNormalize(dir) > 0.0f)
            VectorMA(end, -5.0f, dir, end);
    }

    BSP_LightPoint(&point, fd->vieworg, end, bsp->nodes,
                   vk.world.nolm_mask | SURF_TRANS_MASK);
    vk_trace_bmodel_light_points(fd, bsp, fd->vieworg, end, &point);

    return point.surf && point.fraction < 0.995f;
}

static bool vk_flare_visible(const entity_t *ent, const refdef_t *fd)
{
    for (int i = 0; i < 4; i++) {
        if (PlaneDiff(ent->origin, &vk.world.frustum[i]) < -2.5f)
            return false;
    }

    return !vk_flare_occluded(ent, fd);
}

static float vk_flare_frac(const entity_t *ent, const refdef_t *fd)
{
    float target = vk_flare_visible(ent, fd) ? 1.0f : 0.0f;
    int key = ent->skinnum;

    if (key < 0 || key >= MAX_EDICTS)
        return target;

    float *frac = &vk.flare_fracs[key];
    float speed = vk_flarespeed ? vk_flarespeed->value : 8.0f;

    if (com_eventTime - vk.flare_times[key] >= 2500)
        *frac = 0.0f;
    vk.flare_times[key] = com_eventTime;

    if (speed <= 0.0f) {
        *frac = target;
    } else if (*frac < target) {
        *frac += speed * fd->frametime;
        if (*frac > target)
            *frac = target;
    } else if (*frac > target) {
        *frac -= speed * fd->frametime;
        if (*frac < target)
            *frac = target;
    }

    return *frac;
}

static void vk_draw_flare(const entity_t *ent, const refdef_t *fd)
{
    if (ent->skin <= 0 || ent->skin >= r_numImages || !vk.sprite_pipeline ||
        !vk.sprite_quad.vertices.buffer || !vk.sprite_quad.indices.buffer)
        return;

    float frac = vk_flare_frac(ent, fd);
    if (frac <= 0.0f)
        return;

    const image_t *image = IMG_ForHandle(ent->skin);
    if (!image || image->texnum >= MAX_RIMAGES)
        return;

    const vk_texture_t *texture = vk_texture_for_index(image->texnum, true);
    if (!texture)
        return;

    bool def = image->flags & IF_DEFAULT_FLARE;
    float scale = (float)(25 << def) * (ent->scale * frac);
    if (scale <= 0.0f)
        return;

    vec3_t viewaxis[3], left, right, down, up, xaxis, yaxis, origin;
    mat4_t model_matrix, mvp;
    vk_world_push_t push = { 0 };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;
    color_t color;

    AnglesToAxis(fd->viewangles, viewaxis);
    if (ent->flags & RF_FLARE_LOCK_ANGLE) {
        VectorScale(viewaxis[1], scale, left);
        VectorScale(viewaxis[1], -scale, right);
        VectorScale(viewaxis[2], -scale, down);
        VectorScale(viewaxis[2], scale, up);
    } else {
        vec3_t dir, r, u;

        VectorSubtract(ent->origin, fd->vieworg, dir);
        if (VectorNormalize(dir) <= 0.0f)
            return;

        MakeNormalVectors(dir, r, u);
        VectorScale(r, -scale, left);
        VectorScale(r, scale, right);
        VectorScale(u, -scale, down);
        VectorScale(u, scale, up);
    }

    VectorSubtract(right, left, xaxis);
    VectorSubtract(up, down, yaxis);
    VectorAdd3(ent->origin, left, down, origin);

    memset(model_matrix, 0, sizeof(model_matrix));
    model_matrix[0] = xaxis[0];
    model_matrix[1] = xaxis[1];
    model_matrix[2] = xaxis[2];
    model_matrix[4] = yaxis[0];
    model_matrix[5] = yaxis[1];
    model_matrix[6] = yaxis[2];
    model_matrix[10] = 1.0f;
    model_matrix[12] = origin[0];
    model_matrix[13] = origin[1];
    model_matrix[14] = origin[2];
    model_matrix[15] = 1.0f;

    vk_model_mvp(mvp, fd, model_matrix);
    memcpy(push.mvp, mvp, sizeof(push.mvp));

    color.u32 = ent->rgba.u32;
    if (ent->flags & (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE)) {
        VectorClear(color.u8);
        if (ent->flags & RF_SHELL_RED)
            color.u8[0] = 255;
        if (ent->flags & RF_SHELL_GREEN)
            color.u8[1] = 255;
        if (ent->flags & RF_SHELL_BLUE)
            color.u8[2] = 255;
    }
    color.u8[3] = (128 + def * 32) * (ent->alpha * frac);

    push.color[0] = color.u8[0] / 255.0f;
    push.color[1] = color.u8[1] / 255.0f;
    push.color[2] = color.u8[2] / 255.0f;
    push.color[3] = color.u8[3] / 255.0f;
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    vk_fog_params(fd, push.fog);
    push.intensity = def ? -1.0f : 1.0f;

    VkPipeline pipeline = (def && vk.particle_add_pipeline) ?
        vk.particle_add_pipeline : vk.sprite_pipeline;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.sprite_quad.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.sprite_quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, vk.sprite_quad.index_count, 1, 0, 0, 0);
    c.trisDrawn += vk.sprite_quad.index_count / 3;
    vk_count_batch3d();
    vk_draw_fx_outlines(&vk.sprite_quad, &vk.sprite_quad_line_indices,
                        vk.sprite_quad_line_index_count, mvp);
}

#define VK_PARTICLE_SIZE    (1.0f + M_SQRT1_2f)
#define VK_PARTICLE_SCALE   (1.0f / (2.0f * VK_PARTICLE_SIZE))

static void vk_draw_particles(const refdef_t *fd)
{
    if (!fd->num_particles || !fd->particles ||
        !vk.sprite_pipeline || !vk.particle_texture.descriptor_set ||
        !vk.particle_vertices.buffer || !vk.particle_vertices.memory ||
        !vk.particle_vertices_mapped)
        return;

    vk_draw_scope_t old_scope = vk.draw_scope;
    vk.draw_scope = VK_DRAW_PARTICLE;

    vec3_t viewaxis[3];
    VkPipeline pipeline = (vk_partstyle && vk_partstyle->integer &&
        vk.particle_add_pipeline) ? vk.particle_add_pipeline : vk.sprite_pipeline;
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;
    mat4_t proj, view, mvp;
    vk_world_push_t push = { 0 };
    uint32_t vertex_count = 0;

    AnglesToAxis(fd->viewangles, viewaxis);

    vk_projection_matrix(proj, fd->fov_x, fd->fov_y, fd->rdflags);
    vk_view_matrix(view, fd);
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    vk_fog_params(fd, push.fog);
    push.intensity = 1.0f;

    for (int i = 0; i < fd->num_particles && vertex_count + 6 <= VK_MAX_PARTICLE_VERTICES; i++) {
        const particle_t *particle = &fd->particles[i];
        vec3_t transformed, left, right, down, up, corners[4];
        vec_t dist, scale, scale2;
        color_t color;

        VectorSubtract(particle->origin, fd->vieworg, transformed);
        dist = DotProduct(transformed, viewaxis[0]);

        scale = 1.0f;
        if (dist > 20.0f)
            scale += dist * 0.004f;
        scale *= (vk_partscale ? vk_partscale->value : 2.0f) * particle->scale;
        scale2 = scale * VK_PARTICLE_SCALE;

        VectorScale(viewaxis[1], scale2, left);
        VectorScale(viewaxis[1], -scale, right);
        VectorScale(viewaxis[2], -scale2, down);
        VectorScale(viewaxis[2], scale, up);

        if (particle->color == -1)
            color.u32 = particle->rgba.u32;
        else
            color.u32 = d_8to24table[particle->color & 0xff];
        color.u8[3] *= particle->alpha;

        VectorAdd3(particle->origin, left, down, corners[0]);
        VectorAdd3(particle->origin, right, down, corners[1]);
        VectorAdd3(particle->origin, left, up, corners[2]);
        VectorAdd3(particle->origin, right, up, corners[3]);

        static const int order[6] = { 0, 2, 1, 1, 2, 3 };
        static const float uv[4][2] = {
            { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f },
        };
        for (int j = 0; j < 6; j++) {
            int idx = order[j];
            vk_vertex_t *v = &vk.particle_batch[vertex_count++];
            VectorCopy(corners[idx], v->position);
            v->uv[0] = uv[idx][0];
            v->uv[1] = uv[idx][1];
            v->color[0] = color.u8[0] / 255.0f;
            v->color[1] = color.u8[1] / 255.0f;
            v->color[2] = color.u8[2] / 255.0f;
            v->color[3] = color.u8[3] / 255.0f;
        }
    }

    if (!vertex_count) {
        vk.draw_scope = old_scope;
        return;
    }

    VkDeviceSize size = vertex_count * sizeof(vk.particle_batch[0]);
    memcpy((byte *)vk.particle_vertices_mapped + offset, vk.particle_batch, size);

    vk_matrix_multiply(mvp, proj, view);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    Vector4Set(push.color, 1.0f, 1.0f, 1.0f, 1.0f);

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.particle_vertices.buffer, &offset);
    vk_bind_texture_descriptor(cmd, vk.particle_texture.descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDraw(cmd, vertex_count, 1, 0, 0);
    c.trisDrawn += vertex_count / 3;
    vk_count_batch3d();
    vk.draw_scope = old_scope;
}

static void vk_read_glare_queries(uint32_t slot)
{
    uint32_t count = min(vk.glare_query_counts[slot],
                         (uint32_t)glr.num_glare_sources);
    uint64_t results[MAX_GLARE_SOURCES];

    vk.glare_query_counts[slot] = 0;
    if (!count || !vk.glare_query_pool)
        return;

    uint32_t first = slot * MAX_GLARE_SOURCES;
    VkResult result = vk.GetQueryPoolResults(
        vk.device, vk.glare_query_pool, first, count,
        sizeof(*results) * count, results, sizeof(*results),
        VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS)
        return;

    for (uint32_t i = 0; i < count; i++)
        glr.glare_sources[i].visible = results[i] != 0;
}

static void vk_glare_quad_mvp(mat4_t mvp, const vec3_t origin, float scale,
                              const vec3_t viewaxis[3], const refdef_t *fd)
{
    vec3_t xaxis, yaxis, corner;
    mat4_t model_matrix;

    VectorScale(viewaxis[1], -2.0f * scale, xaxis);
    VectorScale(viewaxis[2],  2.0f * scale, yaxis);
    VectorMA(origin, scale, viewaxis[1], corner);
    VectorMA(corner, -scale, viewaxis[2], corner);

    memset(model_matrix, 0, sizeof(model_matrix));
    model_matrix[0] = xaxis[0];
    model_matrix[1] = xaxis[1];
    model_matrix[2] = xaxis[2];
    model_matrix[4] = yaxis[0];
    model_matrix[5] = yaxis[1];
    model_matrix[6] = yaxis[2];
    model_matrix[10] = 1.0f;
    model_matrix[12] = corner[0];
    model_matrix[13] = corner[1];
    model_matrix[14] = corner[2];
    model_matrix[15] = 1.0f;
    vk_model_mvp(mvp, fd, model_matrix);
}

static void vk_draw_glare(const refdef_t *fd)
{
    if (!vk_glare || !vk_glare->integer || !glr.num_glare_sources || !fd ||
        (fd->rdflags & RDF_NOWORLDMODEL) ||
        (vk_fullbright && vk_fullbright->integer) ||
        (vk_vertexlight && vk_vertexlight->integer) ||
        !vk.world.cache || !vk.world.cache->nodes ||
        !vk.glare_pipeline || !vk.glare_occlusion_pipeline ||
        !vk.glare_query_pool || !vk.particle_texture.descriptor_set ||
        !vk.sprite_quad.vertices.buffer || !vk.sprite_quad.indices.buffer)
        return;

    vec3_t viewaxis[3];
    float distances[MAX_GLARE_SOURCES];
    float view_angles[MAX_GLARE_SOURCES];
    bool eligible[MAX_GLARE_SOURCES];
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;
    uint32_t query_base = vk.frame_index * MAX_GLARE_SOURCES;
    bool issue_queries = com_eventTime - vk.glare_query_time > 33;

    AnglesToAxis(fd->viewangles, viewaxis);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.sprite_quad.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.sprite_quad.indices.buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, vk.particle_texture.descriptor_set);

    // Match OpenGL's small depth-tested occlusion quad and 30 Hz query rate.
    // Results are consumed when this frame-in-flight slot is next reused.
    if (issue_queries) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         vk.glare_occlusion_pipeline);
        vk.glare_query_time = com_eventTime;
    }
    for (int i = 0; i < glr.num_glare_sources; i++) {
        glare_source_t *gs = &glr.glare_sources[i];
        vec3_t to_src, view_dir, to_viewer;
        bool test = true;

        for (int j = 0; j < 4; j++) {
            if (PlaneDiff(gs->origin, &vk.world.frustum[j]) < -2.5f) {
                test = false;
                break;
            }
        }

        VectorSubtract(gs->origin, fd->vieworg, to_src);
        float dist = VectorNormalize2(to_src, view_dir);
        if (dist < 1.0f)
            test = false;
        VectorNegate(view_dir, to_viewer);
        float view_angle = DotProduct(to_viewer, gs->normal);
        if (view_angle < 0.01f)
            test = false;

        distances[i] = dist;
        view_angles[i] = view_angle;
        eligible[i] = test;

        if (!issue_queries)
            continue;

        VkQueryControlFlags query_flags =
            vk.physical_device_features.occlusionQueryPrecise ?
            VK_QUERY_CONTROL_PRECISE_BIT : 0;
        vk.CmdBeginQuery(cmd, vk.glare_query_pool, query_base + i,
                         query_flags);
        if (test) {
            mat4_t mvp;
            vk_world_push_t push = { 0 };
            float query_scale = 2.5f;

            if (dist > 20.0f)
                query_scale += dist * 0.004f;
            vk_glare_quad_mvp(mvp, gs->origin, query_scale, viewaxis, fd);
            memcpy(push.mvp, mvp, sizeof(push.mvp));
            Vector4Set(push.color, 1.0f, 1.0f, 1.0f, 1.0f);
            push.intensity = 1.0f;
            vk_push_constants(cmd, sizeof(push), &push);
            vk.CmdDrawIndexed(cmd, vk.sprite_quad.index_count, 1, 0, 0, 0);
            c.occlusionQueries++;
        }
        vk.CmdEndQuery(cmd, vk.glare_query_pool, query_base + i);
    }
    vk.glare_query_counts[vk.frame_index] = issue_queries ?
        glr.num_glare_sources : 0;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.glare_pipeline);
    for (int i = 0; i < glr.num_glare_sources; i++) {
        glare_source_t *gs = &glr.glare_sources[i];
        bool visible = eligible[i] && gs->visible;

        float speed = vk_flarespeed ? vk_flarespeed->value : 8.0f;
        if (speed <= 0.0f) {
            gs->visibility = visible ? 1.0f : 0.0f;
        } else if (gs->visibility < (visible ? 1.0f : 0.0f)) {
            gs->visibility = min(gs->visibility + speed * fd->frametime, 1.0f);
        } else if (gs->visibility > (visible ? 1.0f : 0.0f)) {
            gs->visibility = max(gs->visibility - speed * fd->frametime, 0.0f);
        }
        if (gs->visibility <= 0.0f)
            continue;

        float scale = (vk_glare_size ?
            Cvar_ClampValue(vk_glare_size, 0.0f, 256.0f) : 24.0f) *
            gs->brightness;
        if (distances[i] > 20.0f)
            scale *= 1.0f + distances[i] * 0.004f;
        scale = min(scale, 200.0f);

        float alpha = view_angles[i] * gs->brightness *
            (vk_glare_intensity ?
             Cvar_ClampValue(vk_glare_intensity, 0.0f, 4.0f) : 0.5f) *
            gs->visibility;
        alpha = min(alpha, 1.0f);
        if (alpha < 0.01f || scale <= 0.0f)
            continue;

        mat4_t mvp;
        vk_world_push_t push = { 0 };

        vk_glare_quad_mvp(mvp, gs->origin, scale, viewaxis, fd);
        memcpy(push.mvp, mvp, sizeof(push.mvp));
        push.color[0] = min(gs->lightcolor[0], 1.0f);
        push.color[1] = min(gs->lightcolor[1], 1.0f);
        push.color[2] = min(gs->lightcolor[2], 1.0f);
        push.color[3] = alpha;
        Vector4Clear(push.scroll);
        Vector4Clear(push.dlight);
        Vector4Clear(push.fog);
        push.intensity = 1.0f;

        vk_push_constants(cmd, sizeof(push), &push);
        vk.CmdDrawIndexed(cmd, vk.sprite_quad.index_count, 1, 0, 0, 0);
        c.trisDrawn += vk.sprite_quad.index_count / 3;
        vk_count_batch3d();
    }
}

static void vk_draw_beam_segment(const vec3_t start, const vec3_t end,
                                 const refdef_t *fd, const float color[4],
                                 float width)
{
    vec3_t dir, to_view, normal, xaxis, yaxis, origin;
    mat4_t model_matrix, mvp;
    vk_world_push_t push = { 0 };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    VectorSubtract(end, start, dir);
    VectorSubtract(fd->vieworg, start, to_view);
    CrossProduct(dir, to_view, normal);
    if (VectorNormalize(normal) < 0.1f)
        return;

    VectorScale(normal, width, normal);
    VectorCopy(dir, xaxis);
    VectorScale(normal, -2.0f, yaxis);
    VectorAdd(start, normal, origin);

    memset(model_matrix, 0, sizeof(model_matrix));
    model_matrix[0] = xaxis[0];
    model_matrix[1] = xaxis[1];
    model_matrix[2] = xaxis[2];
    model_matrix[4] = yaxis[0];
    model_matrix[5] = yaxis[1];
    model_matrix[6] = yaxis[2];
    model_matrix[10] = 1.0f;
    VectorAdd(start, normal, origin);
    model_matrix[12] = origin[0];
    model_matrix[13] = origin[1];
    model_matrix[14] = origin[2];
    model_matrix[15] = 1.0f;

    vk_model_mvp(mvp, fd, model_matrix);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    memcpy(push.color, color, sizeof(push.color));
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    vk_fog_params(fd, push.fog);
    push.intensity = 1.0f;

    vk_bind_vertex_buffers(cmd, 0, 1, &vk.sprite_quad.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.sprite_quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, vk.sprite_quad.index_count, 1, 0, 0, 0);
    c.trisDrawn += vk.sprite_quad.index_count / 3;
    vk_count_batch3d();
    vk_draw_fx_outlines(&vk.sprite_quad, &vk.sprite_quad_line_indices,
                        vk.sprite_quad_line_index_count, mvp);
    if (gl_showtris && (gl_showtris->integer & SHOWTRIS_FX)) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.sprite_pipeline);
        vk_bind_vertex_buffers(cmd, 0, 1, &vk.sprite_quad.vertices.buffer, &offset);
        vk_bind_index_buffer(cmd, vk.sprite_quad.indices.buffer, 0,
                             VK_INDEX_TYPE_UINT32);
        vk_bind_texture_descriptor(cmd, vk.beam_texture.descriptor_set);
    }
}

static void vk_draw_poly_beam_segment(const vec3_t start, const vec3_t end,
                                      const refdef_t *fd, const float color[4],
                                      float width)
{
    if (!vk.beam_cylinder.vertices.buffer || !vk.beam_cylinder.indices.buffer)
        return;

    vec3_t dir, right, up;
    mat4_t model_matrix, mvp;
    vk_color3d_push_t push;
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    VectorSubtract(end, start, dir);
    if (VectorNormalize(dir) < 0.1f)
        return;

    MakeNormalVectors(dir, right, up);
    VectorScale(right, width, right);
    VectorScale(up, width, up);

    memset(model_matrix, 0, sizeof(model_matrix));
    VectorSubtract(end, start, dir);
    model_matrix[0] = dir[0];
    model_matrix[1] = dir[1];
    model_matrix[2] = dir[2];
    model_matrix[4] = right[0];
    model_matrix[5] = right[1];
    model_matrix[6] = right[2];
    model_matrix[8] = up[0];
    model_matrix[9] = up[1];
    model_matrix[10] = up[2];
    model_matrix[12] = start[0];
    model_matrix[13] = start[1];
    model_matrix[14] = start[2];
    model_matrix[15] = 1.0f;

    vk_model_mvp(mvp, fd, model_matrix);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    memcpy(push.color, color, sizeof(push.color));

    vk_bind_vertex_buffers(cmd, 0, 1, &vk.beam_cylinder.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.beam_cylinder.indices.buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, vk.beam_cylinder.index_count, 1, 0, 0, 0);
    c.trisDrawn += vk.beam_cylinder.index_count / 3;
    vk_count_batch3d();
    vk_draw_fx_outlines(&vk.beam_cylinder, &vk.beam_cylinder_line_indices,
                        vk.beam_cylinder_line_index_count, mvp);
    if (gl_showtris && (gl_showtris->integer & SHOWTRIS_FX))
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         vk.drawing_bloom ? vk.beam_bloom_pipeline : vk.beam_pipeline);
}

#define VK_MIN_LIGHTNING_SEGMENTS   3
#define VK_MAX_LIGHTNING_SEGMENTS   7
#define VK_MIN_SEGMENT_LENGTH       16

static void vk_draw_lightning_beam(const vec3_t start, const vec3_t end,
                                   const refdef_t *fd, const float color[4],
                                   float width, bool poly)
{
    vec3_t dir, segments[VK_MAX_LIGHTNING_SEGMENTS + 1];
    vec3_t right, up;
    vec_t length, segment_length;
    int num_segments, max_segments;

    VectorSubtract(end, start, dir);
    length = VectorNormalize(dir);
    if (length < 0.1f)
        return;

    max_segments = Q_clip(length / VK_MIN_SEGMENT_LENGTH, 1, VK_MAX_LIGHTNING_SEGMENTS);
    if (max_segments <= VK_MIN_LIGHTNING_SEGMENTS)
        num_segments = max_segments;
    else
        num_segments = VK_MIN_LIGHTNING_SEGMENTS +
            Com_SlowRand() % (max_segments - VK_MIN_LIGHTNING_SEGMENTS + 1);

    if (num_segments > 1)
        MakeNormalVectors(dir, right, up);

    segment_length = length / num_segments;
    for (int i = 1; i < num_segments; i++) {
        vec3_t point;
        float offs;

        VectorMA(start, i * segment_length, dir, point);

        offs = Com_SlowCrand() * (segment_length * 0.35f);
        VectorMA(point, offs, right, point);

        offs = Com_SlowCrand() * (segment_length * 0.35f);
        VectorMA(point, offs, up, segments[i]);
    }

    VectorCopy(start, segments[0]);
    VectorCopy(end, segments[num_segments]);

    if (poly) {
        for (int i = 0; i < num_segments; i++)
            vk_draw_poly_beam_segment(segments[i], segments[i + 1], fd, color, width);
    } else {
        for (int i = 0; i < num_segments; i++)
            vk_draw_beam_segment(segments[i], segments[i + 1], fd, color, width);
    }
}

static void vk_draw_beam(const entity_t *ent, const refdef_t *fd)
{
    if ((!vk.beam_pipeline && !vk.sprite_pipeline) ||
        !vk.sprite_quad.vertices.buffer || !vk.sprite_quad.indices.buffer ||
        !ent->frame)
        return;

    color_t color;
    float push_color[4];
    bool poly = vk_beamstyle && vk_beamstyle->integer;
    float scale = poly ? 0.5f : 1.2f;
    float width = abs((int16_t)ent->frame) * scale;
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];

    if (ent->skinnum == -1)
        color.u32 = ent->rgba.u32;
    else
        color.u32 = d_8to24table[ent->skinnum & 0xff];
    color.u8[3] *= ent->alpha;

    push_color[0] = color.u8[0] / 255.0f;
    push_color[1] = color.u8[1] / 255.0f;
    push_color[2] = color.u8[2] / 255.0f;
    push_color[3] = color.u8[3] / 255.0f;

    if (poly) {
        if (!vk.beam_pipeline)
            return;
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         vk.drawing_bloom ? vk.beam_bloom_pipeline : vk.beam_pipeline);
    } else {
        if (!vk.sprite_pipeline || !vk.beam_texture.descriptor_set)
            return;
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         vk.drawing_bloom ? vk.sprite_bloom_pipeline : vk.sprite_pipeline);
        vk_bind_texture_descriptor(cmd, vk.beam_texture.descriptor_set);
    }

    if (ent->flags & RF_GLOW) {
        vk_draw_lightning_beam(ent->origin, ent->oldorigin, fd, push_color,
                               width, poly);
    } else if (poly) {
        vk_draw_poly_beam_segment(ent->origin, ent->oldorigin, fd, push_color, width);
    } else {
        vk_draw_beam_segment(ent->origin, ent->oldorigin, fd, push_color, width);
    }
}

static float vk_lightstyle_value(const refdef_t *fd, byte style)
{
    if (!fd || !fd->lightstyles || style >= MAX_LIGHTSTYLES)
        return 1.0f;
    return fd->lightstyles[style].white;
}

static void vk_sample_lightpoint(const lightpoint_t *point, const refdef_t *fd,
                                 vec3_t color)
{
    const mface_t *surf = point->surf;
    const byte *lightmap = surf->lightmap;
    int s = point->s;
    int t = point->t;
    int smax = surf->lm_width;
    int tmax = surf->lm_height;
    int s1, t1;
    int size = smax * tmax * 3;
    float fracu = point->s - s;
    float fracv = point->t - t;
    float w1 = (1.0f - fracu) * (1.0f - fracv);
    float w2 = fracu * (1.0f - fracv);
    float w3 = fracu * fracv;
    float w4 = (1.0f - fracu) * fracv;

    VectorClear(color);

    if (!lightmap || smax < 1 || tmax < 1 || s < 0 || t < 0 ||
        s >= smax || t >= tmax)
        return;

    s1 = min(s + 1, smax - 1);
    t1 = min(t + 1, tmax - 1);

    for (int i = 0; i < surf->numstyles; i++) {
        const byte *b1 = &lightmap[3 * ((t + 0) * smax + (s + 0))];
        const byte *b2 = &lightmap[3 * ((t + 0) * smax + s1)];
        const byte *b3 = &lightmap[3 * (t1 * smax + s1)];
        const byte *b4 = &lightmap[3 * (t1 * smax + (s + 0))];

        float style = vk_lightstyle_value(fd, surf->styles[i]);

        color[0] += style * (w1 * b1[0] + w2 * b2[0] + w3 * b3[0] + w4 * b4[0]);
        color[1] += style * (w1 * b1[1] + w2 * b2[1] + w3 * b3[1] + w4 * b4[1]);
        color[2] += style * (w1 * b1[2] + w2 * b2[2] + w3 * b3[2] + w4 * b4[2]);

        lightmap += size;
    }

    vk_adjust_entity_light(color);
}

static bool vk_lightgrid_point(const lightgrid_t *grid, const vec3_t start,
                               const refdef_t *fd, vec3_t color)
{
    vec3_t point, avg;
    uint32_t point_i[3];
    vec3_t samples[8];
    int mask = 0;
    int numsamples = 0;

    if (!grid->numleafs || (vk_lightgrid && !vk_lightgrid->integer))
        return false;

    point[0] = (start[0] - grid->mins[0]) * grid->scale[0];
    point[1] = (start[1] - grid->mins[1]) * grid->scale[1];
    point[2] = (start[2] - grid->mins[2]) * grid->scale[2];

    point_i[0] = point[0];
    point_i[1] = point[1];
    point_i[2] = point[2];
    VectorClear(avg);

    for (int i = 0; i < 8; i++) {
        uint32_t tmp[3];
        const lightgrid_sample_t *sample;
        int j;

        tmp[0] = point_i[0] + ((i >> 0) & 1);
        tmp[1] = point_i[1] + ((i >> 1) & 1);
        tmp[2] = point_i[2] + ((i >> 2) & 1);

        sample = BSP_LookupLightgrid(grid, tmp);
        if (!sample)
            continue;

        VectorClear(samples[i]);
        for (j = 0; j < grid->numstyles && sample->style != 255; j++, sample++) {
            float style = vk_lightstyle_value(fd, sample->style);

            samples[i][0] += style * sample->rgb[0];
            samples[i][1] += style * sample->rgb[1];
            samples[i][2] += style * sample->rgb[2];
        }

        if (j) {
            mask |= BIT(i);
            VectorAdd(avg, samples[i], avg);
            numsamples++;
        }
    }

    if (!mask)
        return false;

    if (mask != 255) {
        VectorScale(avg, 1.0f / numsamples, avg);
        for (int i = 0; i < 8; i++) {
            if (!(mask & BIT(i)))
                VectorCopy(avg, samples[i]);
        }
    }

    float fx = point[0] - point_i[0];
    float fy = point[1] - point_i[1];
    float fz = point[2] - point_i[2];
    vec3_t lerp_x[4];
    vec3_t lerp_y[2];

    LerpVector2(samples[0], samples[1], 1.0f - fx, fx, lerp_x[0]);
    LerpVector2(samples[2], samples[3], 1.0f - fx, fx, lerp_x[1]);
    LerpVector2(samples[4], samples[5], 1.0f - fx, fx, lerp_x[2]);
    LerpVector2(samples[6], samples[7], 1.0f - fx, fx, lerp_x[3]);
    LerpVector2(lerp_x[0], lerp_x[1], 1.0f - fy, fy, lerp_y[0]);
    LerpVector2(lerp_x[2], lerp_x[3], 1.0f - fy, fy, lerp_y[1]);
    LerpVector2(lerp_y[0], lerp_y[1], 1.0f - fz, fz, color);
    vk_adjust_entity_light(color);
    return true;
}

static bool vk_static_light_point(const vec3_t origin, const refdef_t *fd,
                                  vec3_t light)
{
    const bsp_t *bsp = vk.world.cache;
    lightpoint_t point;
    vec3_t end;

    if (!bsp || !bsp->nodes)
        return false;

    if (vk_lightgrid_point(&bsp->lightgrid, origin, fd, light))
        return true;

    if (!bsp->lightmap)
        return false;

    end[0] = origin[0];
    end[1] = origin[1];
    end[2] = origin[2] - 8192.0f;

    BSP_LightPoint(&point, origin, end, bsp->nodes,
                   vk.world.nolm_mask | SURF_TRANS_MASK);
    vk_trace_bmodel_light_points(fd, bsp, origin, end, &point);

    if (!point.surf)
        return false;

    vk_sample_lightpoint(&point, fd, light);
    return true;
}

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

static bool vk_entity_in_view_medium(const entity_t *ent,
                                     const refdef_t *fd)
{
    if (!(fd->rdflags & RDF_UNDERWATER) ||
        (ent->flags & (RF_WEAPONMODEL | RF_DEPTHHACK)) ||
        (ent->model & BIT(31)) || !vk.world.cache || !vk.world.cache->nodes)
        return true;

    const mleaf_t *view_leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                             fd->vieworg);
    const mleaf_t *entity_leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                               ent->origin);
    int medium = view_leaf ? view_leaf->contents[0] & MASK_WATER : 0;
    bool same_medium = medium && entity_leaf &&
        (entity_leaf->contents[0] & medium);

    if (!same_medium && (ent->flags & RF_BEAM)) {
        entity_leaf = BSP_PointLeaf(vk.world.cache->nodes, ent->oldorigin);
        same_medium = medium && entity_leaf &&
            (entity_leaf->contents[0] & medium);
    }

    return !medium || same_medium;
}

static bool vk_entity_in_pass(const entity_t *ent, vk_entity_pass_t pass)
{
    if (ent->flags & RF_BEAM)
        return pass == VK_ENTITY_BEAM;

    if (ent->flags & RF_BLOOM_ONLY) {
        if (pass == VK_ENTITY_BLOOM_ONLY)
            return true;
        if (pass != VK_ENTITY_OPAQUE || (ent->model & BIT(31)))
            return false;

        vk_model_t *model = vk_model_for_handle(ent->model);
        return model && model->type == VK_MODEL_ALIAS;
    }

    if (pass == VK_ENTITY_BLOOM_ONLY)
        return false;

    if (pass == VK_ENTITY_BLOOM_SOURCE) {
        if (ent->flags & RF_FLARE)
            return false;
        if (ent->model & BIT(31))
            return false;

        vk_model_t *model = vk_model_for_handle(ent->model);
        if (!model || model->type != VK_MODEL_ALIAS)
            return false;

        return ((ent->flags & RF_SHELL_MASK) && !(ent->flags & RF_NOBLOOM)) ||
            vk_alias_model_has_glowmap(model);
    }

    if (ent->flags & RF_FLARE)
        return pass == VK_ENTITY_ALPHA_FRONT;

    if ((ent->model & BIT(31)) && pass == VK_ENTITY_BMODEL_ALPHA)
        return !(ent->flags & RF_TRANSLUCENT);

    if (!(ent->flags & RF_TRANSLUCENT))
        return (ent->model & BIT(31)) ?
            pass == VK_ENTITY_BMODEL : pass == VK_ENTITY_OPAQUE;

    if ((ent->flags & RF_WEAPONMODEL) ||
        ent->alpha <= (vk_draworder ? vk_draworder->value : 1.0f)) {
        return pass == VK_ENTITY_ALPHA_FRONT;
    }

    return pass == VK_ENTITY_ALPHA_BACK;
}

static void vk_draw_entity(const entity_t *ent, const refdef_t *fd,
                           vk_entity_pass_t pass)
{
    if (pass == VK_ENTITY_BEAM) {
        vk_draw_beam(ent, fd);
        return;
    }

    if (ent->flags & RF_FLARE) {
        vk_draw_flare(ent, fd);
        return;
    }

    if (ent->model & BIT(31)) {
        vk_draw_bmodel(ent, fd, pass == VK_ENTITY_BMODEL_ALPHA);
        return;
    }

    vk_model_t *model = vk_model_for_handle(ent->model);

    if (!model) {
        vk_draw_null_model(ent, fd);
        return;
    }
    if (model->type == VK_MODEL_SPRITE)
        vk_draw_sprite(ent, fd);
    else if (model->type == VK_MODEL_ALIAS)
        vk_draw_alias_model(ent, fd);

    if (!vk.drawing_bloom && vk_showorigins && vk_showorigins->integer)
        vk_draw_null_model(ent, fd);
}

static void vk_draw_entities(const refdef_t *fd, vk_entity_pass_t pass)
{
    if (vk_drawentities && !vk_drawentities->integer)
        return;
    if (fd->num_entities <= 0 || !fd->entities)
        return;

    vk_draw_scope_t old_scope = vk.draw_scope;
    vk.draw_scope = VK_DRAW_ENTITY;

    for (int i = fd->num_entities - 1; i >= 0; i--) {
        const entity_t *ent = &fd->entities[i];

        if (vk_entity_in_view_medium(ent, fd) &&
            vk_entity_in_pass(ent, pass))
            vk_draw_entity(ent, fd, pass);
    }

    vk.draw_scope = old_scope;
}

static void vk_draw_bloom_only_entities(const refdef_t *fd)
{
    bool old = vk.drawing_bloom;

    vk.drawing_bloom = true;
    vk_set_3d_viewport(fd);
    vk_draw_entities(fd, VK_ENTITY_BLOOM_ONLY);
    vk.drawing_bloom = old;
}

static void vk_draw_bloom_beams(const refdef_t *fd)
{
    bool old = vk.drawing_bloom;

    vk.drawing_bloom = true;
    vk_set_3d_viewport(fd);
    vk_draw_entities(fd, VK_ENTITY_BEAM);
    vk.drawing_bloom = old;
}

static void vk_draw_bloom_source_entities(const refdef_t *fd)
{
    bool old = vk.drawing_bloom;

    vk.drawing_bloom = true;
    vk_set_3d_viewport(fd);
    vk_draw_entities(fd, VK_ENTITY_BLOOM_SOURCE);
    vk.drawing_bloom = old;
}

static void vk_surface_color(const mface_t *face, float color[4])
{
    float intensity = vk_texture_intensity();

    if (face->drawflags & SURF_TRANS33) {
        Vector4Set(color, 1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }
    if (face->drawflags & SURF_TRANS66) {
        Vector4Set(color, 1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }
    if (face->drawflags & SURF_WARP) {
        Vector4Set(color, 1.0f / intensity, 1.0f / intensity,
                   1.0f / intensity, 1.0f);
        return;
    }

    uint32_t hash = FS_HashPath(face->texinfo->name, UINT32_MAX);
    float shade = 0.35f + fabsf(face->plane->normal[2]) * 0.35f;

    color[0] = (0.35f + ((hash >> 0) & 255) / 255.0f * 0.55f) * shade;
    color[1] = (0.35f + ((hash >> 8) & 255) / 255.0f * 0.55f) * shade;
    color[2] = (0.35f + ((hash >> 16) & 255) / 255.0f * 0.55f) * shade;
    color[3] = 1.0f;
}

static bool vk_sample_surface_light(const bsp_t *bsp, const mface_t *face,
                                    const refdef_t *fd,
                                    const vec3_t point, float color[4])
{
    float s, t, fracs, fract;
    int s0, t0, s1, t1;
    int smax = face->lm_width;
    int tmax = face->lm_height;
    int size = smax * tmax * 3;
    ptrdiff_t offset;

    if (!face->lightmap || !face->numstyles || smax < 1 || tmax < 1)
        return false;
    if (face->drawflags & vk.world.nolm_mask)
        return false;
    if (!bsp->lightmap || face->lightmap < bsp->lightmap)
        return false;

    offset = face->lightmap - bsp->lightmap;
    if (offset < 0 || (uint64_t)offset + (uint64_t)face->numstyles * size >
        (uint64_t)bsp->numlightmapbytes)
        return false;

    s = DotProduct(point, face->lm_axis[0]) + face->lm_offset[0];
    t = DotProduct(point, face->lm_axis[1]) + face->lm_offset[1];

    s = Q_clipf(s, 0.0f, smax - 1.0f);
    t = Q_clipf(t, 0.0f, tmax - 1.0f);
    s0 = floorf(s);
    t0 = floorf(t);
    s1 = min(s0 + 1, smax - 1);
    t1 = min(t0 + 1, tmax - 1);
    fracs = s - s0;
    fract = t - t0;

    Vector4Clear(color);
    float w1 = (1.0f - fracs) * (1.0f - fract);
    float w2 = fracs * (1.0f - fract);
    float w3 = fracs * fract;
    float w4 = (1.0f - fracs) * fract;

    const byte *lightmap = face->lightmap;
    for (int i = 0; i < face->numstyles; i++) {
        const byte *b1 = &lightmap[3 * (t0 * smax + s0)];
        const byte *b2 = &lightmap[3 * (t0 * smax + s1)];
        const byte *b3 = &lightmap[3 * (t1 * smax + s1)];
        const byte *b4 = &lightmap[3 * (t1 * smax + s0)];
        float style = vk_lightstyle_value(fd, face->styles[i]);

        color[0] += style * (w1 * b1[0] + w2 * b2[0] + w3 * b3[0] + w4 * b4[0]);
        color[1] += style * (w1 * b1[1] + w2 * b2[1] + w3 * b3[1] + w4 * b4[1]);
        color[2] += style * (w1 * b1[2] + w2 * b2[2] + w3 * b3[2] + w4 * b4[2]);

        lightmap += size;
    }

    color[0] = Q_clipf(color[0] / 255.0f, 0.0f, 1.0f);
    color[1] = Q_clipf(color[1] / 255.0f, 0.0f, 1.0f);
    color[2] = Q_clipf(color[2] / 255.0f, 0.0f, 1.0f);
    if (vk_coloredlightmaps && !vk_coloredlightmaps->integer) {
        float y = LUMINANCE(color[0], color[1], color[2]);
        color[0] = y;
        color[1] = y;
        color[2] = y;
    }
    color[3] = 1.0f;
    return true;
}

static bool vk_face_has_valid_lightmap(const bsp_t *bsp, const mface_t *face)
{
    if (!bsp || !bsp->lightmap || !face || !face->lightmap || !face->numstyles)
        return false;
    if (face->drawflags & vk.world.nolm_mask)
        return false;
    if (face->lm_width < 1 || face->lm_height < 1 ||
        face->lm_width > VK_MAX_LIGHTMAP_EXTENTS ||
        face->lm_height > VK_MAX_LIGHTMAP_EXTENTS)
        return false;
    if (face->lightmap < bsp->lightmap)
        return false;

    uint64_t size = (uint64_t)face->lm_width * face->lm_height * 3;
    ptrdiff_t offset = face->lightmap - bsp->lightmap;
    if (offset < 0 ||
        (uint64_t)offset + (uint64_t)face->numstyles * size >
        (uint64_t)bsp->numlightmapbytes)
        return false;

    return true;
}

static uint32_t vk_pixel_lightmap_texel(const mface_t *face, const refdef_t *fd,
                                        int s, int t)
{
    int size = face->lm_width * face->lm_height * 3;
    const byte *lightmap = face->lightmap;
    float rgb[3] = { 0.0f, 0.0f, 0.0f };

    for (int i = 0; i < face->numstyles; i++) {
        const byte *src = &lightmap[3 * (t * face->lm_width + s)];
        float style = vk_lightstyle_value(fd, face->styles[i]);
        rgb[0] += style * src[0];
        rgb[1] += style * src[1];
        rgb[2] += style * src[2];
        lightmap += size;
    }

    float brightest = max(rgb[0], max(rgb[1], rgb[2]));
    if (brightest > 255.0f)
        VectorScale(rgb, 255.0f / brightest, rgb);

    float saturation = vk_coloredlightmaps ?
        Cvar_ClampValue(vk_coloredlightmaps, 0.0f, 1.0f) : 1.0f;
    if (saturation != 1.0f) {
        float y = LUMINANCE(rgb[0], rgb[1], rgb[2]);
        rgb[0] = y + (rgb[0] - y) * saturation;
        rgb[1] = y + (rgb[1] - y) * saturation;
        rgb[2] = y + (rgb[2] - y) * saturation;
    }

    return MakeColor(Q_clipf(rgb[0], 0.0f, 255.0f),
                     Q_clipf(rgb[1], 0.0f, 255.0f),
                     Q_clipf(rgb[2], 0.0f, 255.0f), 255);
}

static void vk_fill_pixel_lightmap_atlas(uint32_t *pixels, uint32_t atlas_w,
                                         uint32_t atlas_h,
                                         const vk_world_face_t *faces,
                                         uint32_t face_count,
                                         const refdef_t *fd)
{
    size_t pixel_count = (size_t)atlas_w * atlas_h;
    for (size_t i = 0; i < pixel_count; i++)
        pixels[i] = MakeColor(255, 255, 255, 255);

    for (uint32_t i = 0; i < face_count; i++) {
        const vk_world_face_t *draw = &faces[i];
        const mface_t *face = draw->face;
        if (!face || !draw->pixel_lm_w || !draw->pixel_lm_h)
            continue;

        for (int t = -1; t <= face->lm_height; t++) {
            int src_t = Q_clipf(t, 0, face->lm_height - 1);
            for (int s = -1; s <= face->lm_width; s++) {
                int src_s = Q_clipf(s, 0, face->lm_width - 1);
                pixels[(draw->pixel_lm_y + t) * atlas_w +
                       draw->pixel_lm_x + s] =
                    vk_pixel_lightmap_texel(face, fd, src_s, src_t);
            }
        }
    }
}

static void vk_destroy_pixel_lightmap_staging(void)
{
    if (vk.world.pixel_lightmap_staging) {
        for (uint32_t i = 0; i < vk.world.pixel_lightmap_staging_count; i++) {
            if (vk.world.pixel_lightmap_staging_mapped &&
                vk.world.pixel_lightmap_staging_mapped[i])
                vk.UnmapMemory(vk.device,
                    vk.world.pixel_lightmap_staging[i].memory);
            vk_destroy_buffer(&vk.world.pixel_lightmap_staging[i]);
        }
        Z_Free(vk.world.pixel_lightmap_staging);
        vk.world.pixel_lightmap_staging = NULL;
    }
    vk.world.pixel_lightmap_staging_count = 0;
    if (vk.world.pixel_lightmap_staging_mapped) {
        Z_Free(vk.world.pixel_lightmap_staging_mapped);
        vk.world.pixel_lightmap_staging_mapped = NULL;
    }
    vk.world.pixel_lightmap_staging_size = 0;
    if (vk.world.pixel_lightmap_copies) {
        Z_Free(vk.world.pixel_lightmap_copies);
        vk.world.pixel_lightmap_copies = NULL;
    }
    vk.world.pixel_lightmap_copy_count = 0;
    vk.world.pixel_lightmap_copy_capacity = 0;
    vk.world.pixel_lightmap_update_pending = false;
}

static bool vk_create_pixel_lightmap_staging(void)
{
    VkDeviceSize size = (VkDeviceSize)vk.world.pixel_lightmap_texture.width *
        vk.world.pixel_lightmap_texture.height * sizeof(uint32_t);

    vk_destroy_pixel_lightmap_staging();
    if (!size || !vk.swapchain_image_count)
        return false;

    vk.world.pixel_lightmap_staging = Z_Mallocz(
        sizeof(*vk.world.pixel_lightmap_staging) * vk.swapchain_image_count);
    vk.world.pixel_lightmap_staging_mapped = Z_Mallocz(
        sizeof(*vk.world.pixel_lightmap_staging_mapped) * vk.swapchain_image_count);
    vk.world.pixel_lightmap_staging_count = vk.swapchain_image_count;

    for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
        vk_buffer_t *buffer = &vk.world.pixel_lightmap_staging[i];
        if (!vk_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              &buffer->buffer, &buffer->memory)) {
            vk_destroy_pixel_lightmap_staging();
            return false;
        }
        buffer->size = size;
        VkResult result = vk.MapMemory(vk.device, buffer->memory, 0, size, 0,
                                       &vk.world.pixel_lightmap_staging_mapped[i]);
        if (result != VK_SUCCESS) {
            vk_fail_result("vkMapMemory(pixel lightmap staging)", result);
            vk_destroy_pixel_lightmap_staging();
            return false;
        }
    }

    vk.world.pixel_lightmap_staging_size = size;
    return true;
}

static void vk_pixel_lightmap_plan(const bsp_t *bsp,
                                   vk_world_face_t *faces,
                                   uint32_t face_count,
                                   const refdef_t *fd)
{
    if (vk_pixel_lightmap_mode() < 1)
        return;

    typedef struct {
        const mface_t *face;
        int x, y;
    } vk_lm_plan_t;

    uint32_t valid = 0;
    uint64_t total_area = 1;
    int max_w = 1;

    for (uint32_t i = 0; i < face_count; i++) {
        faces[i].pixel_lm_x = faces[i].pixel_lm_y = 0;
        faces[i].pixel_lm_w = faces[i].pixel_lm_h = 0;
        const mface_t *face = faces[i].face;
        if (!vk_face_has_valid_lightmap(bsp, face)) {
            continue;
        }
        valid++;
        max_w = max(max_w, face->lm_width + 2);
        total_area += (uint64_t)(face->lm_width + 2) * (face->lm_height + 2);
    }

    int atlas_w = 1;
    while (atlas_w < max_w || (uint64_t)atlas_w * atlas_w < total_area)
        atlas_w <<= 1;
    atlas_w = min(atlas_w, 4096);

    if (!valid) {
        vk_destroy_texture_resource(&vk.world.pixel_lightmap_texture);
        return;
    }

    vk_lm_plan_t *plan = Z_Mallocz(sizeof(*plan) * face_count);
    int cx = 0, cy = 0, row_h = 0;
    for (uint32_t i = 0; i < face_count; i++) {
        const mface_t *face = faces[i].face;
        if (!vk_face_has_valid_lightmap(bsp, face))
            continue;
        int pack_w = face->lm_width + 2;
        int pack_h = face->lm_height + 2;
        if (cx + pack_w > atlas_w) {
            cx = 0;
            cy += row_h;
            row_h = 0;
        }
        plan[i].face = face;
        plan[i].x = cx + 1;
        plan[i].y = cy + 1;
        faces[i].pixel_lm_x = cx + 1;
        faces[i].pixel_lm_y = cy + 1;
        faces[i].pixel_lm_w = face->lm_width;
        faces[i].pixel_lm_h = face->lm_height;
        cx += pack_w;
        row_h = max(row_h, pack_h);
    }

    int atlas_h = 1;
    while (atlas_h < cy + row_h)
        atlas_h <<= 1;

    if (atlas_w > 4096 || atlas_h > 4096) {
        Com_WPrintf("Vulkan pixel lightmap plan too large: %dx%d\n",
                    atlas_w, atlas_h);
        for (uint32_t i = 0; i < face_count; i++) {
            faces[i].pixel_lm_x = faces[i].pixel_lm_y = 0;
            faces[i].pixel_lm_w = faces[i].pixel_lm_h = 0;
        }
        vk_destroy_texture_resource(&vk.world.pixel_lightmap_texture);
        Z_Free(plan);
        return;
    }

    size_t pixel_count = (size_t)atlas_w * atlas_h;
    uint32_t *pixels = Z_Malloc(sizeof(*pixels) * pixel_count);
    vk_fill_pixel_lightmap_atlas(pixels, atlas_w, atlas_h,
                                 faces, face_count, fd);

    vk_destroy_texture_resource(&vk.world.pixel_lightmap_texture);
    if (vk_upload_texture_data(&vk.world.pixel_lightmap_texture,
                               atlas_w, atlas_h, pixels, false)) {
        vk_update_texture_descriptor_with_sampler(
            &vk.world.pixel_lightmap_texture, vk.postprocess_sampler);
        if (!vk_create_pixel_lightmap_staging())
            Com_WPrintf("Couldn't create Vulkan pixel lightmap staging buffers\n");
    } else {
        Com_WPrintf("Couldn't upload Vulkan pixel lightmap atlas texture: %s\n",
                    Com_GetLastError());
    }
    Z_Free(pixels);
    Z_Free(plan);
}

static void vk_surface_vertex_color(const bsp_t *bsp, const mface_t *face,
                                    const refdef_t *fd,
                                    const vec3_t point, const float fallback[4],
                                    float color[4])
{
    if (face->drawflags & vk.world.nolm_mask) {
        Vector4Set(color, 1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }

    if (vk_vertexlight && vk_vertexlight->integer) {
        memcpy(color, fallback, sizeof(float) * 4);
        return;
    }

    if (!vk_sample_surface_light(bsp, face, fd, point, color))
        memcpy(color, fallback, sizeof(float) * 4);
}

static bool vk_face_is_drawable(mface_t *face)
{
    if (!face || !face->texinfo || !face->plane)
        return false;

    if (face->numsurfedges < 3)
        return false;
    if (face->drawflags & SURF_SKY)
        return false;
    if (face->drawflags & SURF_NODRAW)
        return false;

    return true;
}

static bool vk_face_edges_are_valid(const bsp_t *bsp, const mface_t *face)
{
    if (!bsp || !face || !face->firstsurfedge || !bsp->surfedges ||
        !bsp->edges || !bsp->vertices || bsp->numsurfedges <= 0 ||
        bsp->numedges <= 0 || bsp->numvertices <= 0)
        return false;

    uintptr_t surfedges_begin = (uintptr_t)bsp->surfedges;
    uintptr_t surfedges_end = surfedges_begin +
        sizeof(*bsp->surfedges) * (uintptr_t)bsp->numsurfedges;
    uintptr_t first = (uintptr_t)face->firstsurfedge;
    uintptr_t last = first +
        sizeof(*face->firstsurfedge) * (uintptr_t)face->numsurfedges;

    if (first < surfedges_begin || first >= surfedges_end ||
        last < first || last > surfedges_end)
        return false;

    for (int j = 0; j < face->numsurfedges; j++) {
        const msurfedge_t *surfedge = face->firstsurfedge + j;
        if (surfedge->edge >= (uint32_t)bsp->numedges)
            return false;

        const medge_t *edge = &bsp->edges[surfedge->edge];
        if (edge->v[surfedge->vert] >= (uint32_t)bsp->numvertices)
            return false;
    }

    return true;
}

#if USE_VULKAN_RAYTRACING
static float vk_polygon_area(const vk_vertex_t *vertices,
                             const vk_world_face_t *face)
{
    float area = 0.0f;
    const vec_t *base = vertices[face->first_vertex].position;
    for (uint32_t i = 1; i + 1 < face->edge_count; i++) {
        vec3_t a, b, cross;
        VectorSubtract(vertices[face->first_vertex + i].position, base, a);
        VectorSubtract(vertices[face->first_vertex + i + 1].position, base, b);
        CrossProduct(a, b, cross);
        area += VectorLength(cross) * 0.5f;
    }
    return area;
}

typedef struct {
    char name[MAX_QPATH];
    vec3_t color;
} vk_surface_light_color_t;

static void vk_surface_light_texture_color(const char *path, bool has_glow,
                                           vec3_t color)
{
    VectorSet(color, 1.0f, 1.0f, 1.0f);

    int width = 0, height = 0;
    byte *pixels = IMG_LoadPixels(path, &width, &height);
    if (!pixels)
        return;
    if (width <= 0 || height <= 0) {
        IMG_FreePixels(pixels);
        return;
    }

    int glow_width = 0, glow_height = 0;
    byte *glow_pixels = NULL;
    if (has_glow) {
        char glow_path[MAX_QPATH];
        COM_StripExtension(glow_path, path, sizeof(glow_path));
        if (Q_strlcat(glow_path, "_glow.pcx", sizeof(glow_path)) <
            sizeof(glow_path)) {
            glow_pixels = IMG_LoadPixels(glow_path, &glow_width, &glow_height);
        }
    }

    double sum[3] = { 0.0, 0.0, 0.0 };
    double weight_sum = 0.0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const byte *pixel = pixels + ((size_t)y * width + x) * 4;
            float alpha = pixel[3] / 255.0f;
            float luma = LUMINANCE(pixel[0], pixel[1], pixel[2]) / 255.0f;
            float weight;
            if (glow_pixels && glow_width > 0 && glow_height > 0) {
                int gx = x * glow_width / width;
                int gy = y * glow_height / height;
                const byte *glow = glow_pixels +
                    ((size_t)gy * glow_width + gx) * 4;
                weight = alpha * (glow[3] / 255.0f);
            } else {
                float luma2 = luma * luma;
                weight = alpha * luma2 * luma2;
            }
            if (weight <= 0.001f)
                continue;
            sum[0] += (pixel[0] / 255.0f) * weight;
            sum[1] += (pixel[1] / 255.0f) * weight;
            sum[2] += (pixel[2] / 255.0f) * weight;
            weight_sum += weight;
        }
    }

    IMG_FreePixels(pixels);
    if (glow_pixels)
        IMG_FreePixels(glow_pixels);
    if (weight_sum <= 0.0)
        return;

    color[0] = sum[0] / weight_sum;
    color[1] = sum[1] / weight_sum;
    color[2] = sum[2] / weight_sum;
    float peak = max(color[0], max(color[1], color[2]));
    if (peak <= 0.001f) {
        VectorSet(color, 1.0f, 1.0f, 1.0f);
        return;
    }
    VectorScale(color, 1.0f / peak, color);
    for (int i = 0; i < 3; i++)
        color[i] = color[i] * 0.95f + 0.05f;
}

static float vk_surface_light_face_score(const vk_surface_light_t *light,
                                         const vec3_t center, float radius)
{
    vec3_t delta;
    VectorSubtract(center, light->origin, delta);
    float distance = VectorLength(delta);
    float nearest = max(distance - radius, 0.0f);
    float falloff = Q_clipf(1.0f - nearest / light->range, 0.0f, 1.0f);
    falloff = falloff * falloff * (3.0f - 2.0f * falloff);
    float source = Q_clipf((DotProduct(light->normal, delta) + radius) /
                           max(distance + radius, 0.001f), 0.0f, 1.0f);
    return light->strength * falloff * (0.25f + 0.75f * source);
}

static byte vk_face_ao_phase(const mface_t *face)
{
    if (!face || !face->plane)
        return 0;

    vec3_t normal;
    VectorCopy(face->plane->normal, normal);
    float distance = face->plane->dist;

    // Canonicalize the plane equation so opposite-facing references to the
    // same geometric plane receive the same AO direction.
    int sign = 1;
    for (int i = 0; i < 3; i++) {
        if (fabsf(normal[i]) <= 0.0001f)
            continue;
        if (normal[i] < 0.0f)
            sign = -1;
        break;
    }
    if (sign < 0) {
        VectorNegate(normal, normal);
        distance = -distance;
    }

    int32_t quantized[4] = {
        lrintf(normal[0] * 4096.0f),
        lrintf(normal[1] * 4096.0f),
        lrintf(normal[2] * 4096.0f),
        lrintf(distance * 8.0f),
    };
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 4; i++) {
        hash ^= (uint32_t)quantized[i];
        hash *= 16777619u;
    }
    hash ^= hash >> 16;
    return hash & 255u;
}

static bool vk_surface_light_may_affect_face(const vk_surface_light_t *light,
                                             const vec3_t center, float radius)
{
    vec3_t delta;
    VectorSubtract(center, light->origin, delta);
    float distance_sq = DotProduct(delta, delta);
    float maximum_distance = light->range + radius;
    if (distance_sq >= maximum_distance * maximum_distance)
        return false;

    // The face lies entirely inside this sphere. If the sphere intersects the
    // emitter's forward half-space, some point on the face may receive light.
    // Do not cull by the receiver normal here: the fragment shader intentionally
    // treats BSP receivers as two-sided because derivative normals can flip with
    // triangle winding.
    return DotProduct(light->normal, delta) + radius > 0.0f;
}

static uint32_t vk_surface_island_find(uint32_t *parents, uint32_t face)
{
    uint32_t root = face;
    while (parents[root] != root)
        root = parents[root];
    while (parents[face] != face) {
        uint32_t next = parents[face];
        parents[face] = root;
        face = next;
    }
    return root;
}

static void vk_surface_island_union(uint32_t *parents, byte *ranks,
                                    uint32_t first, uint32_t second)
{
    first = vk_surface_island_find(parents, first);
    second = vk_surface_island_find(parents, second);
    if (first == second)
        return;
    if (ranks[first] < ranks[second]) {
        uint32_t swap = first;
        first = second;
        second = swap;
    }
    parents[second] = first;
    if (ranks[first] == ranks[second])
        ranks[first]++;
}

static bool vk_surface_island_eligible(const bsp_t *bsp,
                                       const vk_world_face_t *draw)
{
    const mface_t *face = draw->face;
    int excluded = SURF_TRANS_MASK | SURF_WARP | SURF_SKY | SURF_NODRAW;
    return face && face->plane && face->texinfo && draw->edge_count >= 3 &&
        !(face->drawflags & excluded) &&
        !(face->texinfo->c.flags & excluded) &&
        vk_face_edges_are_valid(bsp, face);
}

static bool vk_surface_faces_are_coplanar(const mface_t *first,
                                          const mface_t *second)
{
    vec3_t first_normal, second_normal;
    VectorCopy(first->plane->normal, first_normal);
    VectorCopy(second->plane->normal, second_normal);
    float first_dist = first->plane->dist;
    float second_dist = second->plane->dist;
    if (first->drawflags & DSURF_PLANEBACK) {
        VectorNegate(first_normal, first_normal);
        first_dist = -first_dist;
    }
    if (second->drawflags & DSURF_PLANEBACK) {
        VectorNegate(second_normal, second_normal);
        second_dist = -second_dist;
    }
    return DotProduct(first_normal, second_normal) > 0.9999f &&
        fabsf(first_dist - second_dist) < 0.1f;
}

static bool vk_surface_bounds_touch(const vec3_t first_mins,
                                    const vec3_t first_maxs,
                                    const vec3_t second_mins,
                                    const vec3_t second_maxs)
{
    const float tolerance = 0.25f;
    for (int axis = 0; axis < 3; axis++) {
        if (first_maxs[axis] + tolerance < second_mins[axis] ||
            second_maxs[axis] + tolerance < first_mins[axis])
            return false;
    }
    return true;
}

static bool vk_surface_segments_overlap(const vec3_t first_start,
                                        const vec3_t first_end,
                                        const vec3_t second_start,
                                        const vec3_t second_end)
{
    const float tolerance = 0.25f;
    vec3_t first_direction, second_direction;
    VectorSubtract(first_end, first_start, first_direction);
    VectorSubtract(second_end, second_start, second_direction);
    float first_length = VectorLength(first_direction);
    float second_length = VectorLength(second_direction);
    if (first_length <= tolerance || second_length <= tolerance)
        return false;
    VectorScale(first_direction, 1.0f / first_length, first_direction);
    VectorScale(second_direction, 1.0f / second_length, second_direction);
    if (fabsf(DotProduct(first_direction, second_direction)) < 0.9999f)
        return false;

    vec3_t delta, rejection;
    VectorSubtract(second_start, first_start, delta);
    VectorMA(delta, -DotProduct(delta, first_direction),
             first_direction, rejection);
    if (VectorLength(rejection) > tolerance)
        return false;
    VectorSubtract(second_end, first_start, delta);
    VectorMA(delta, -DotProduct(delta, first_direction),
             first_direction, rejection);
    if (VectorLength(rejection) > tolerance)
        return false;

    VectorSubtract(second_start, first_start, delta);
    float second_first = DotProduct(delta, first_direction);
    VectorSubtract(second_end, first_start, delta);
    float second_last = DotProduct(delta, first_direction);
    float second_min = min(second_first, second_last);
    float second_max = max(second_first, second_last);
    float overlap = min(first_length, second_max) - max(0.0f, second_min);
    return overlap >= tolerance;
}

static bool vk_surface_boundaries_touch(const vk_world_face_t *first,
                                        const vk_world_face_t *second,
                                        const vk_vertex_t *vertices)
{
    for (uint32_t i = 0; i < first->edge_count; i++) {
        const vec3_t first_start = {
            vertices[first->first_vertex + i].position[0],
            vertices[first->first_vertex + i].position[1],
            vertices[first->first_vertex + i].position[2],
        };
        const float *first_end = vertices[first->first_vertex +
            (i + 1) % first->edge_count].position;
        for (uint32_t j = 0; j < second->edge_count; j++) {
            const float *second_start =
                vertices[second->first_vertex + j].position;
            const float *second_end = vertices[second->first_vertex +
                (j + 1) % second->edge_count].position;
            if (vk_surface_segments_overlap(first_start, first_end,
                                            second_start, second_end))
                return true;
        }
    }
    return false;
}

static void vk_build_surface_lights(const bsp_t *bsp,
                                    vk_world_face_t *faces, uint32_t face_count,
                                    const vk_vertex_t *vertices)
{
    if (vk.world.surface_light_indices) {
        Z_Free(vk.world.surface_light_indices);
        vk.world.surface_light_indices = NULL;
    }
    vk.world.surface_light_count = 0;
    vk.world.surface_light_index_count = 0;
    for (uint32_t i = 0; i < face_count; i++) {
        faces[i].rt_light_offset = 0;
        faces[i].rt_light_count = 0;
    }
    if (!vk.raytracing_active || !vk.raytracing_pixel_ready)
        return;

    vk_surface_light_color_t color_cache[VK_MAX_SURFACE_LIGHTS];
    uint32_t color_count = 0;
    for (uint32_t i = 0; i < face_count &&
         vk.world.surface_light_count < VK_MAX_SURFACE_LIGHTS; i++) {
        mface_t *face = faces[i].face;
        if (!face || !face->texinfo || !face->plane ||
            (face->drawflags & (SURF_TRANS_MASK | SURF_SKY)) ||
            (face->texinfo->c.flags & (SURF_SKY | SURF_NODRAW)))
            continue;

        if (!(face->texinfo->c.flags & SURF_LIGHT))
            continue;

        float area = vk_polygon_area(vertices, &faces[i]);
        if (area <= 1.0f)
            continue;
        vk_surface_light_t *light =
            &vk.world.surface_lights[vk.world.surface_light_count++];
        VectorCopy(faces[i].center, light->origin);
        VectorCopy(face->plane->normal, light->normal);
        if (face->drawflags & DSURF_PLANEBACK)
            VectorNegate(light->normal, light->normal);
        VectorMA(light->origin, 1.0f, light->normal, light->origin);
        char name[MAX_QPATH];
        Q_concat(name, sizeof(name), "textures/", face->texinfo->name, ".wal");
        uint32_t color_index;
        for (color_index = 0; color_index < color_count; color_index++) {
            if (!Q_stricmp(color_cache[color_index].name, name))
                break;
        }
        if (color_index == color_count) {
            Q_strlcpy(color_cache[color_index].name, name,
                      sizeof(color_cache[color_index].name));
            bool has_glow = face->texinfo->image &&
                face->texinfo->image->texnum2 > 0 &&
                face->texinfo->image->texnum2 < MAX_RIMAGES;
            vk_surface_light_texture_color(name, has_glow,
                                           color_cache[color_index].color);
            color_count++;
        }
        VectorCopy(color_cache[color_index].color, light->color);
        float base = face->texinfo->c.value > 0 ?
            face->texinfo->c.value : 200.0f;
        light->radius = Q_clipf(sqrtf(area * 0.318309886f), 1.0f, 48.0f);
        light->range = Q_clipf(96.0f + base * 0.55f +
                               light->radius * 1.5f,
                               128.0f, 384.0f);
        light->strength = Q_clipf(192.0f * sqrtf(base / 200.0f),
                                  128.0f, 192.0f);
        // The live emissive control applies its perceptual response in the
        // shader. Keeping the map data unscaled avoids rebuilding the world.
    }

    size_t capacity = (size_t)face_count * VK_MAX_FACE_SURFACE_LIGHTS;
    uint32_t *indices = capacity ? Z_Malloc(capacity * sizeof(*indices)) : NULL;
    uint32_t *parents = face_count ? Z_Malloc(sizeof(*parents) * face_count) : NULL;
    byte *ranks = face_count ? Z_Mallocz(sizeof(*ranks) * face_count) : NULL;
    byte *eligible = face_count ? Z_Mallocz(sizeof(*eligible) * face_count) : NULL;
    uint32_t *edge_owners = bsp->numedges > 0 ?
        Z_Malloc(sizeof(*edge_owners) * (size_t)bsp->numedges) : NULL;
    vec3_t *island_mins = face_count ? Z_Malloc(sizeof(*island_mins) * face_count) : NULL;
    vec3_t *island_maxs = face_count ? Z_Malloc(sizeof(*island_maxs) * face_count) : NULL;
    uint32_t *island_faces = face_count ?
        Z_Mallocz(sizeof(*island_faces) * face_count) : NULL;
    uint32_t *island_offsets = face_count ?
        Z_Mallocz(sizeof(*island_offsets) * face_count) : NULL;
    byte *island_lights = face_count ?
        Z_Mallocz(sizeof(*island_lights) * face_count) : NULL;
    vec3_t *face_mins = face_count ?
        Z_Malloc(sizeof(*face_mins) * face_count) : NULL;
    vec3_t *face_maxs = face_count ?
        Z_Malloc(sizeof(*face_maxs) * face_count) : NULL;
    uint64_t island_start = vk_time_usec();

    for (uint32_t i = 0; i < face_count; i++) {
        parents[i] = i;
        eligible[i] = vk_surface_island_eligible(bsp, &faces[i]);
        if (!eligible[i])
            continue;
        for (uint32_t k = 0; k < faces[i].edge_count; k++) {
            const float *point = vertices[faces[i].first_vertex + k].position;
            if (!k) {
                VectorCopy(point, face_mins[i]);
                VectorCopy(point, face_maxs[i]);
            } else {
                for (int axis = 0; axis < 3; axis++) {
                    face_mins[i][axis] = min(face_mins[i][axis], point[axis]);
                    face_maxs[i][axis] = max(face_maxs[i][axis], point[axis]);
                }
            }
        }
    }
    for (int i = 0; i < bsp->numedges; i++)
        edge_owners[i] = UINT32_MAX;
    uint32_t exact_unions = 0;
    for (uint32_t i = 0; i < face_count; i++) {
        if (!eligible[i])
            continue;
        const mface_t *face = faces[i].face;
        for (uint32_t k = 0; k < face->numsurfedges; k++) {
            uint32_t edge = face->firstsurfedge[k].edge;
            uint32_t owner = edge_owners[edge];
            if (owner == UINT32_MAX) {
                edge_owners[edge] = i;
            } else if (vk_surface_faces_are_coplanar(faces[owner].face, face)) {
                if (vk_surface_island_find(parents, owner) !=
                    vk_surface_island_find(parents, i))
                    exact_unions++;
                vk_surface_island_union(parents, ranks, owner, i);
            }
        }
    }

    uint32_t geometry_unions = 0;
    for (uint32_t i = 0; i < face_count; i++) {
        if (!eligible[i])
            continue;
        for (uint32_t j = i + 1; j < face_count; j++) {
            if (!eligible[j] ||
                vk_surface_island_find(parents, i) ==
                    vk_surface_island_find(parents, j) ||
                !vk_surface_faces_are_coplanar(faces[i].face, faces[j].face) ||
                !vk_surface_bounds_touch(face_mins[i], face_maxs[i],
                                         face_mins[j], face_maxs[j]) ||
                !vk_surface_boundaries_touch(&faces[i], &faces[j], vertices))
                continue;
            vk_surface_island_union(parents, ranks, i, j);
            geometry_unions++;
        }
    }
    for (uint32_t i = 0; i < face_count; i++) {
        if (!eligible[i])
            continue;
        uint32_t root = vk_surface_island_find(parents, i);
        if (!island_faces[root]) {
            VectorCopy(face_mins[i], island_mins[root]);
            VectorCopy(face_maxs[i], island_maxs[root]);
        } else {
            for (int axis = 0; axis < 3; axis++) {
                island_mins[root][axis] = min(island_mins[root][axis],
                                              face_mins[i][axis]);
                island_maxs[root][axis] = max(island_maxs[root][axis],
                                              face_maxs[i][axis]);
            }
        }
        island_faces[root]++;
    }

    uint32_t candidate_max = 0, selected_max = 0;
    uint32_t island_count = 0, island_face_max = 0, island_face_total = 0;
    for (uint32_t root = 0; root < face_count; root++) {
        if (!island_faces[root] || vk_surface_island_find(parents, root) != root)
            continue;
        vec3_t center, extent;
        VectorAdd(island_mins[root], island_maxs[root], center);
        VectorScale(center, 0.5f, center);
        VectorSubtract(island_maxs[root], center, extent);
        float radius = VectorLength(extent);
        island_offsets[root] = vk.world.surface_light_index_count;
        uint32_t selected_indices[VK_MAX_FACE_SURFACE_LIGHTS];
        float selected_scores[VK_MAX_FACE_SURFACE_LIGHTS];
        uint32_t selected_count = 0;
        uint32_t candidate_count = 0;
        for (uint32_t j = 0; j < vk.world.surface_light_count; j++) {
            const vk_surface_light_t *light = &vk.world.surface_lights[j];
            if (!vk_surface_light_may_affect_face(light, center, radius))
                continue;
            candidate_count++;
            float score = vk_surface_light_face_score(light, center, radius);
            uint32_t position = selected_count;
            if (selected_count < VK_MAX_FACE_SURFACE_LIGHTS) {
                selected_count++;
            } else if (score <= selected_scores[selected_count - 1]) {
                continue;
            } else {
                position--;
            }
            while (position > 0 && score > selected_scores[position - 1]) {
                if (position < VK_MAX_FACE_SURFACE_LIGHTS) {
                    selected_scores[position] = selected_scores[position - 1];
                    selected_indices[position] = selected_indices[position - 1];
                }
                position--;
            }
            selected_scores[position] = score;
            selected_indices[position] = j;
        }
        for (uint32_t j = 0; j < selected_count; j++)
            indices[vk.world.surface_light_index_count++] = selected_indices[j];
        island_lights[root] = selected_count;
        candidate_max = max(candidate_max, candidate_count);
        selected_max = max(selected_max, selected_count);
        island_count++;
        island_face_total += island_faces[root];
        island_face_max = max(island_face_max, island_faces[root]);
    }
    for (uint32_t i = 0; i < face_count; i++) {
        if (!eligible[i])
            continue;
        uint32_t root = vk_surface_island_find(parents, i);
        faces[i].rt_light_offset = island_offsets[root];
        faces[i].rt_light_count = island_lights[root];
    }
    vk.world.surface_light_indices = indices;
    double island_msec = (vk_time_usec() - island_start) / 1000.0;
    Com_DPrintf("Vulkan RT surface lights: %u, islands %u, faces avg %.1f max %u, candidates max %u selected %u; unions %u exact + %u geometric, %.2f ms\n",
                vk.world.surface_light_count,
                island_count,
                island_count ? (double)island_face_total / island_count : 0.0,
                island_face_max,
                candidate_max, selected_max, exact_unions, geometry_unions,
                island_msec);
    (void)island_count;
    (void)island_face_total;
    (void)exact_unions;
    (void)geometry_unions;
    (void)island_msec;
    Z_Free(parents);
    Z_Free(ranks);
    Z_Free(eligible);
    Z_Free(edge_owners);
    Z_Free(island_mins);
    Z_Free(island_maxs);
    Z_Free(island_faces);
    Z_Free(island_offsets);
    Z_Free(island_lights);
    Z_Free(face_mins);
    Z_Free(face_maxs);
}

static bool vk_upload_surface_light_storage(void)
{
    size_t light_data_size = sizeof(vk_gpu_surface_light_header_t) +
        (size_t)vk.world.surface_light_index_count * sizeof(uint32_t);
    vk_gpu_surface_light_header_t *light_data = Z_Mallocz(light_data_size);
    light_data->light_count = vk.world.surface_light_count;
    light_data->index_count = vk.world.surface_light_index_count;
    light_data->reserved[0] = vk.world.rt_ao_ready ? 1u : 0u;
    for (uint32_t i = 0; i < vk.world.surface_light_count; i++) {
        const vk_surface_light_t *src = &vk.world.surface_lights[i];
        vk_gpu_surface_light_t *dst = &light_data->lights[i];
        VectorCopy(src->origin, dst->origin_range);
        dst->origin_range[3] = src->range;
        VectorCopy(src->color, dst->color_strength);
        dst->color_strength[3] = src->strength;
        VectorCopy(src->normal, dst->normal);
        dst->normal[3] = src->radius;
    }
    if (vk.world.surface_light_index_count)
        memcpy(light_data + 1, vk.world.surface_light_indices,
               (size_t)vk.world.surface_light_index_count * sizeof(uint32_t));

    bool ok = vk_upload_buffer(&vk.world.rt_light_buffer, light_data,
                               light_data_size,
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Z_Free(light_data);
    if (!ok)
        return false;

    VkDescriptorBufferInfo buffer_info = {
        .buffer = vk.world.rt_light_buffer.buffer,
        .range = VK_WHOLE_SIZE,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = vk.rt_descriptor_set,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &buffer_info,
    };
    vk.UpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    return true;
}

static bool vk_upload_surface_face_data(const vk_world_face_t *faces,
                                        uint32_t face_count,
                                        uint32_t vertex_count)
{
    vk_gpu_face_light_t *face_lights =
        Z_Mallocz((size_t)vertex_count * sizeof(*face_lights));
    for (uint32_t i = 0; i < face_count; i++) {
        for (uint32_t j = 0; j < faces[i].edge_count; j++) {
            vk_gpu_face_light_t *dst =
                &face_lights[faces[i].first_vertex + j];
            dst->offset = faces[i].rt_light_offset;
            dst->packed = (uint32_t)faces[i].rt_light_count |
                (vk_face_ao_phase(faces[i].face) << 8);
            // All faces on the same geometric plane share a phase, avoiding
            // brightness seams introduced by BSP face splits and triangulation.
        }
    }
    bool ok = vk_upload_buffer(&vk.world.rt_light_indices, face_lights,
                               (size_t)vertex_count * sizeof(*face_lights),
                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    Z_Free(face_lights);
    return ok;
}
#endif

static bool vk_build_world_mesh(bsp_t *bsp, const refdef_t *fd)
{
    if (!bsp || !bsp->faces) {
        Com_SetLastError("No BSP faces for Vulkan world mesh");
        return false;
    }

    uint64_t vertex_count = 0;
    uint64_t index_count = 0;
    uint32_t face_count = 0;
    uint32_t skipped_bad_edges = 0;

    for (int i = 0; i < bsp->numfaces; i++) {
        mface_t *face = &bsp->faces[i];
        if (!vk_face_is_drawable(face))
            continue;
        if (!vk_face_edges_are_valid(bsp, face)) {
            skipped_bad_edges++;
            continue;
        }
        if (!face->texinfo->image ||
            face->texinfo->image->texnum >= MAX_RIMAGES)
            continue;

        vertex_count += face->numsurfedges;
        index_count += (face->numsurfedges - 2) * 3;
        face_count++;
    }

    if (!vertex_count || !index_count)
        return true;
    if (vertex_count > UINT32_MAX || index_count > UINT32_MAX) {
        Com_SetLastError("Vulkan world mesh is too large");
        return false;
    }

    vk_vertex_t *vertices = Z_Malloc(sizeof(*vertices) * vertex_count);
    uint32_t *indices = Z_Malloc(sizeof(*indices) * index_count);
    vk_world_build_face_t *build_faces = Z_Malloc(sizeof(*build_faces) * face_count);
    vk_world_face_t *draw_faces = Z_Malloc(sizeof(*draw_faces) * face_count);
    uint32_t *texture_index_counts = Z_Mallocz(sizeof(*texture_index_counts) * MAX_RIMAGES);
    uint32_t *line_indices = NULL;
    uint32_t line_index_count = 0;
    uint32_t v = 0;
    uint32_t face_index = 0;
    bool pixel_lm_debug = vk_pixel_lightmap_mode() > 0;
    float *lmuv_data = pixel_lm_debug ?
        Z_Malloc(sizeof(*lmuv_data) * 2 * vertex_count) : NULL;
    if (!pixel_lm_debug) {
        vk_destroy_buffer(&vk.world.pixel_lmuv_buffer);
        vk_destroy_texture_resource(&vk.world.pixel_lightmap_texture);
    }

    for (int i = 0; i < bsp->numfaces; i++) {
        mface_t *face = &bsp->faces[i];
        if (!vk_face_is_drawable(face))
            continue;
        if (!vk_face_edges_are_valid(bsp, face))
            continue;
        image_t *image = face->texinfo->image;
        if (!image || image->texnum >= MAX_RIMAGES)
            continue;

        uint32_t first = v;
        float fallback_color[4];
        vk_surface_color(face, fallback_color);
        uint32_t texinfo_index = face->texinfo - bsp->texinfo;
        uint32_t logical_width = image->width;
        uint32_t logical_height = image->height;
        if (texinfo_index < (uint32_t)bsp->numtexinfo) {
            if (vk.world.texinfo_widths &&
                vk.world.texinfo_widths[texinfo_index])
                logical_width = vk.world.texinfo_widths[texinfo_index];
            if (vk.world.texinfo_heights &&
                vk.world.texinfo_heights[texinfo_index])
                logical_height = vk.world.texinfo_heights[texinfo_index];
        }
        float scale_s = logical_width ? 1.0f / logical_width : 1.0f;
        float scale_t = logical_height ? 1.0f / logical_height : 1.0f;
        if (face->drawflags & SURF_N64_UV) {
            scale_s *= 0.5f;
            scale_t *= 0.5f;
        }
        vec3_t center;
        vec3_t face_normal;

        VectorClear(center);
        VectorCopy(face->plane->normal, face_normal);
        if (face->drawflags & DSURF_PLANEBACK)
            VectorNegate(face_normal, face_normal);
        VectorNormalize(face_normal);

        for (int j = 0; j < face->numsurfedges; j++) {
            const msurfedge_t *surfedge = face->firstsurfedge + j;
            const medge_t *edge = bsp->edges + surfedge->edge;
            const mvertex_t *src = bsp->vertices + edge->v[surfedge->vert];
            float color[4];

            VectorCopy(src->point, vertices[v].position);
            VectorAdd(center, src->point, center);
            vk_surface_vertex_color(bsp, face, fd, src->point, fallback_color, color);
            memcpy(vertices[v].color, color, sizeof(vertices[v].color));
            vertices[v].uv[0] = (DotProduct(src->point, face->texinfo->axis[0]) +
                                 face->texinfo->offset[0]) * scale_s;
            vertices[v].uv[1] = (DotProduct(src->point, face->texinfo->axis[1]) +
                                 face->texinfo->offset[1]) * scale_t;
            VectorCopy(face_normal, vertices[v].normal);
            if (pixel_lm_debug) {
                float lms = 0.0f;
                float lmt = 0.0f;
                if (vk_face_has_valid_lightmap(bsp, face)) {
                    lms = (DotProduct(src->point, face->lm_axis[0]) +
                           face->lm_offset[0]) / max(face->lm_width - 1, 1);
                    lmt = (DotProduct(src->point, face->lm_axis[1]) +
                           face->lm_offset[1]) / max(face->lm_height - 1, 1);
                }
                lmuv_data[v * 2 + 0] = lms;
                lmuv_data[v * 2 + 1] = lmt;
            }
            v++;
        }

        VectorScale(center, 1.0f / face->numsurfedges, center);

        build_faces[face_index++] = (vk_world_build_face_t) {
            .first_vertex = first,
            .edge_count = face->numsurfedges,
            .texture_index = image->texnum,
            .center = { center[0], center[1], center[2] },
            .face = face,
        };
        texture_index_counts[image->texnum] += (face->numsurfedges - 2) * 3;
    }

    uint32_t batch_count = 0;
    for (uint32_t i = 0; i < MAX_RIMAGES; i++) {
        if (texture_index_counts[i])
            batch_count++;
    }

    vk_world_batch_t *batches = Z_Malloc(sizeof(*batches) * batch_count);
    uint32_t idx = 0;
    uint32_t batch = 0;
    uint32_t draw_face_count = 0;

    for (uint32_t texture_index = 0; texture_index < MAX_RIMAGES; texture_index++) {
        uint32_t texture_index_count = texture_index_counts[texture_index];
        if (!texture_index_count)
            continue;

        vk_world_batch_t *world_batch = &batches[batch++];
        *world_batch = (vk_world_batch_t) {
            .first_index = idx,
            .index_count = texture_index_count,
            .texture_index = texture_index,
            .first_face = draw_face_count,
        };

        for (uint32_t i = 0; i < face_index; i++) {
            const vk_world_build_face_t *face = &build_faces[i];
            if (face->texture_index != texture_index)
                continue;

            uint32_t first_index = idx;
            uint32_t face_index_count = (face->edge_count - 2) * 3;

            for (uint32_t j = 0; j < face->edge_count - 2; j++) {
                indices[idx++] = face->first_vertex;
                indices[idx++] = face->first_vertex + j + 1;
                indices[idx++] = face->first_vertex + j + 2;
            }

            draw_faces[draw_face_count++] = (vk_world_face_t) {
                .face = face->face,
                .first_vertex = face->first_vertex,
                .edge_count = face->edge_count,
                .first_index = first_index,
                .index_count = face_index_count,
                .center = { face->center[0], face->center[1], face->center[2] },
            };
            world_batch->face_count++;
        }
    }

#if USE_VULKAN_RAYTRACING
    vk_build_surface_lights(bsp, draw_faces, draw_face_count, vertices);
    vk.world.surface_lights_ready = false;
#endif
    vk_pixel_lightmap_plan(bsp, draw_faces, draw_face_count, fd);

    // Store final atlas coordinates per vertex.  Keeping the per-face atlas
    // transform in push constants forced mode 2 to submit every face as a
    // separate draw, even when all other surface state matched.  Atlas-space
    // coordinates make that transform vertex data and allow the ordinary
    // opaque-world batching path to be used by the pixel-lightmap pipeline.
    if (lmuv_data && vk.world.pixel_lightmap_texture.width &&
        vk.world.pixel_lightmap_texture.height) {
        float inv_aw = 1.0f / vk.world.pixel_lightmap_texture.width;
        float inv_ah = 1.0f / vk.world.pixel_lightmap_texture.height;

        for (uint32_t i = 0; i < draw_face_count; i++) {
            const vk_world_face_t *face = &draw_faces[i];
            if (!face->pixel_lm_w || !face->pixel_lm_h)
                continue;

            float scale_s = max(face->pixel_lm_w - 1, 0) * inv_aw;
            float scale_t = max(face->pixel_lm_h - 1, 0) * inv_ah;
            float offset_s = (face->pixel_lm_x + 0.5f) * inv_aw;
            float offset_t = (face->pixel_lm_y + 0.5f) * inv_ah;

            for (uint32_t j = 0; j < face->edge_count; j++) {
                uint32_t vertex = face->first_vertex + j;
                lmuv_data[vertex * 2 + 0] =
                    lmuv_data[vertex * 2 + 0] * scale_s + offset_s;
                lmuv_data[vertex * 2 + 1] =
                    lmuv_data[vertex * 2 + 1] * scale_t + offset_t;
            }
        }
    }

    bool ok = vk_upload_mesh(&vk.world.mesh, vertices, v, indices, idx);
#if USE_VULKAN_RAYTRACING
    if (ok && vk.raytracing_active) {
        uint32_t *rt_indices = Z_Malloc(sizeof(*rt_indices) * idx);
        uint32_t rt_index_count = 0;
        for (uint32_t i = 0; i < draw_face_count; i++) {
            const vk_world_face_t *face = &draw_faces[i];
            // BSP model 0 deliberately has no face range in Q2PRO.  Inline
            // models 1..n do, so static world faces are the complement of
            // those ranges.
            if (vk_world_face_is_inline(bsp, face->face) ||
                (face->face->drawflags & SURF_TRANS_MASK))
                continue;
            memcpy(rt_indices + rt_index_count, indices + face->first_index,
                   sizeof(*rt_indices) * face->index_count);
            rt_index_count += face->index_count;
        }
        vk_destroy_acceleration_structure(&vk.rt_tlas);
        vk_destroy_acceleration_structure(&vk.world.rt_blas);
        vk_destroy_buffer(&vk.world.rt_indices);
        vk.world.rt_index_count = 0;
        bool rt_ok = false;
        if (!rt_index_count) {
            Com_SetLastError(va("No opaque world triangles for ray tracing "
                                "(%u draw faces)", draw_face_count));
        } else if (!vk_upload_buffer(&vk.world.rt_indices, rt_indices,
                                     sizeof(*rt_indices) * rt_index_count,
                                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)) {
            vk_rt_prefix_error("world RT index upload");
        } else if (!vk_build_mesh_blas(&vk.world.rt_blas,
                                       &vk.world.mesh.vertices, v,
                                       &vk.world.rt_indices,
                                       rt_index_count)) {
            vk_rt_prefix_error("world BLAS build");
        } else if (!vk_build_world_tlas()) {
            vk_rt_prefix_error("world TLAS build");
        } else {
            vk.world.rt_index_count = rt_index_count;
            rt_ok = true;
            if (!vk_upload_surface_light_storage()) {
                Com_WPrintf("Couldn't upload Vulkan RT lights for static "
                            "bake: %s; using runtime lighting fallback\n",
                            Com_GetLastError());
            } else if (!vk_bake_world_rt_ao(bsp, draw_faces, draw_face_count,
                                            vertices, indices, lmuv_data)) {
                Com_WPrintf("Couldn't bake Vulkan RT static lighting: %s; "
                            "using runtime lighting fallback\n",
                            Com_GetLastError());
            }
        }
        Z_Free(rt_indices);
        if (!rt_ok) {
            Com_WPrintf("Couldn't build Vulkan world ray-tracing geometry: %s; disabling ray tracing\n",
                        Com_GetLastError());
            vk_destroy_acceleration_structure(&vk.world.rt_blas);
            vk_destroy_buffer(&vk.world.rt_indices);
            vk.world.rt_index_count = 0;
            vk.raytracing_active = false;
            Q_strlcpy(vk.raytracing_reason,
                      "world acceleration-structure build failed",
                      sizeof(vk.raytracing_reason));
        }
    }
    if (vk.raytracing_active && vk.raytracing_pixel_ready) {
        vk_update_rt_ao_descriptor();
        vk.world.surface_lights_ready = vk_upload_surface_light_storage() &&
            vk_upload_surface_face_data(draw_faces, draw_face_count, v);
    }
#endif
    line_indices = vk_build_line_indices(indices, idx, &line_index_count);
    if (ok) {
        vk_destroy_buffer(&vk.world.pixel_lmuv_buffer);
        if (lmuv_data) {
            size_t lmuv_size = sizeof(*lmuv_data) * 2 * v;
            if (!vk_upload_buffer(&vk.world.pixel_lmuv_buffer, lmuv_data, lmuv_size,
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
                Com_WPrintf("Couldn't upload Vulkan pixel lightmap lmuv buffer\n");
            }
        }
        vk_destroy_buffer(&vk.world.line_indices);
        vk.world.line_index_count = 0;
        if (line_indices && line_index_count) {
            if (vk_upload_buffer(&vk.world.line_indices, line_indices,
                                 sizeof(*line_indices) * line_index_count,
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
                vk.world.line_index_count = line_index_count;
            } else {
                Com_WPrintf("Couldn't upload Vulkan world outline indices\n");
            }
        }
        VkDeviceSize batch_index_size = sizeof(uint32_t) * idx *
            max(vk.swapchain_image_count, 1);
        vk_unmap_world_batch_indices();
        vk_destroy_buffer(&vk.world.batch_indices);
        if (!vk_create_buffer(batch_index_size,
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              &vk.world.batch_indices.buffer,
                              &vk.world.batch_indices.memory)) {
            Com_WPrintf("Couldn't create Vulkan world batch index buffer\n");
        } else {
            VkResult result = vk.MapMemory(vk.device,
                                           vk.world.batch_indices.memory,
                                           0, batch_index_size, 0,
                                           &vk.world.batch_index_mapped);
            if (result != VK_SUCCESS) {
                vk.world.batch_index_mapped = NULL;
                Com_WPrintf("Couldn't map Vulkan world batch index buffer\n");
            }
        }
        if (vk.world.batch_index_capacity < idx) {
            if (vk.world.batch_index_data)
                Z_Free(vk.world.batch_index_data);
            vk.world.batch_index_data = Z_Malloc(sizeof(uint32_t) * idx);
            vk.world.batch_index_capacity = idx;
        }
    }
    if (ok) {
        if (vk.world.batches)
            Z_Free(vk.world.batches);
        if (vk.world.faces)
            Z_Free(vk.world.faces);
        vk.world.batches = batches;
        vk.world.faces = draw_faces;
        vk.world.batch_count = batch;
        vk.world.face_count = draw_face_count;
        batches = NULL;
        draw_faces = NULL;
    }

    Z_Free(vertices);
    Z_Free(indices);
    if (lmuv_data)
        Z_Free(lmuv_data);
    if (line_indices)
        Z_Free(line_indices);
    Z_Free(build_faces);
    Z_Free(texture_index_counts);
    if (batches)
        Z_Free(batches);
    if (draw_faces)
        Z_Free(draw_faces);
    if (skipped_bad_edges)
        Com_WPrintf("Skipped %u Vulkan world faces with invalid edges\n",
                    skipped_bad_edges);
    return ok;
}

static float vk_world_size_for_bsp(const bsp_t *bsp)
{
    if (!bsp || !bsp->nodes)
        return 0.0f;

    vec_t size = 0.0f;
    for (int i = 0; i < 3; i++)
        size = max(size, bsp->nodes->maxs[i] - bsp->nodes->mins[i]);

    if (size > 4096.0f)
        return 8192.0f;
    if (size > 2048.0f)
        return 4096.0f;
    return 2048.0f;
}

static bool vk_world_lighting_modified(void)
{
    return (vk_modulate && vk_modulate->modified) ||
           (vk_modulate_world && vk_modulate_world->modified) ||
           (vk_brightness && vk_brightness->modified) ||
           (vk_intensity && vk_intensity->modified) ||
           (vk_dynamic && vk_dynamic->modified) ||
           (vk_fullbright && vk_fullbright->modified) ||
           (vk_lightmap && vk_lightmap->modified) ||
           (vk_pixel_lightmaps && vk_pixel_lightmaps->modified) ||
           (vk_coloredlightmaps && vk_coloredlightmaps->modified) ||
           (vk_vertexlight && vk_vertexlight->modified);
}

static bool vk_world_lightstyles_modified(const refdef_t *fd)
{
    if (!fd || !fd->lightstyles || !vk.world.cache ||
        (vk_vertexlight && vk_vertexlight->integer) ||
        (vk_fullbright && vk_fullbright->integer))
        return false;

    if (!vk.world.lightstyles_valid)
        return true;

    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        if (vk.world.lightstyle_used[i] &&
            vk.world.lightstyles[i] != fd->lightstyles[i].white)
            return true;
    }

    return false;
}

static void vk_save_world_lightstyles(const refdef_t *fd)
{
    if (!fd || !fd->lightstyles) {
        vk.world.lightstyles_valid = false;
        return;
    }

    for (int i = 0; i < MAX_LIGHTSTYLES; i++)
        vk.world.lightstyles[i] = fd->lightstyles[i].white;
    vk.world.lightstyles_valid = true;
}

static bool vk_queue_pixel_lightmap_update(const refdef_t *fd)
{
    if (!fd || !vk.world.faces || !vk.world.face_count ||
        !vk.world.pixel_lightmap_texture.image ||
        !vk.world.pixel_lightmap_staging_mapped ||
        vk.current_image >= vk.world.pixel_lightmap_staging_count ||
        !vk.world.pixel_lightmap_staging_mapped[vk.current_image])
        return false;

    if (vk.world.pixel_lightmap_copy_capacity < vk.world.face_count) {
        if (vk.world.pixel_lightmap_copies)
            Z_Free(vk.world.pixel_lightmap_copies);
        vk.world.pixel_lightmap_copies = Z_Malloc(
            sizeof(*vk.world.pixel_lightmap_copies) * vk.world.face_count);
        vk.world.pixel_lightmap_copy_capacity = vk.world.face_count;
    }

    uint32_t *dst =
        vk.world.pixel_lightmap_staging_mapped[vk.current_image];
    VkDeviceSize offset = 0;
    uint32_t copy_count = 0;

    for (uint32_t i = 0; i < vk.world.face_count; i++) {
        const vk_world_face_t *draw = &vk.world.faces[i];
        const mface_t *face = draw->face;
        if (!face || !draw->pixel_lm_w || !draw->pixel_lm_h)
            continue;

        bool dirty = false;
        for (int j = 0; j < face->numstyles; j++) {
            unsigned style = face->styles[j];
            if (style < MAX_LIGHTSTYLES &&
                vk.world.lightstyles[style] != fd->lightstyles[style].white) {
                dirty = true;
                break;
            }
        }
        if (!dirty)
            continue;

        uint32_t copy_w = face->lm_width + 2;
        uint32_t copy_h = face->lm_height + 2;
        VkDeviceSize copy_size =
            (VkDeviceSize)copy_w * copy_h * sizeof(uint32_t);
        if (offset + copy_size > vk.world.pixel_lightmap_staging_size)
            return false;

        uint32_t *face_dst = (uint32_t *)((byte *)dst + offset);
        for (int t = -1; t <= face->lm_height; t++) {
            int src_t = Q_clipf(t, 0, face->lm_height - 1);
            for (int s = -1; s <= face->lm_width; s++) {
                int src_s = Q_clipf(s, 0, face->lm_width - 1);
                *face_dst++ =
                    vk_pixel_lightmap_texel(face, fd, src_s, src_t);
            }
        }

        vk.world.pixel_lightmap_copies[copy_count++] = (VkBufferImageCopy) {
            .bufferOffset = offset,
            .bufferRowLength = copy_w,
            .bufferImageHeight = copy_h,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {
                draw->pixel_lm_x - 1,
                draw->pixel_lm_y - 1,
                0,
            },
            .imageExtent = { copy_w, copy_h, 1 },
        };
        offset += copy_size;
    }

    vk.world.pixel_lightmap_copy_count = copy_count;
    vk.world.pixel_lightmap_update_pending = copy_count != 0;
    return true;
}

static void vk_clear_world_lighting_modified(void)
{
    if (vk_modulate)
        vk_modulate->modified = false;
    if (vk_modulate_world)
        vk_modulate_world->modified = false;
    if (vk_brightness)
        vk_brightness->modified = false;
    if (vk_intensity)
        vk_intensity->modified = false;
    if (vk_dynamic)
        vk_dynamic->modified = false;
    if (vk_fullbright)
        vk_fullbright->modified = false;
    if (vk_lightmap)
        vk_lightmap->modified = false;
    if (vk_pixel_lightmaps) {
        if (vk_pixel_lightmap_mode() < 2) {
            vk_pixel_lightmaps_warned = false;
        }
        vk_pixel_lightmaps->modified = false;
    }
    if (vk_coloredlightmaps)
        vk_coloredlightmaps->modified = false;
    if (vk_vertexlight)
        vk_vertexlight->modified = false;
}

static void vk_rebuild_world_lighting(void)
{
    bool styles_modified = vk_world_lightstyles_modified(vk.fd_valid ? &vk.fd : NULL);
    bool lighting_modified = vk_world_lighting_modified();

    if (!vk.world.cache || (!lighting_modified && !styles_modified))
        return;

    // Pixel-lightmap mode keeps geometry and atlas placement static. Animated
    // lightstyles only change atlas texels, so queue an in-place GPU update
    // instead of rebuilding every world buffer and waiting for the queue idle.
    if (!lighting_modified && styles_modified &&
        vk_pixel_lightmap_mode() >= 2 && vk.fd_valid &&
        vk_queue_pixel_lightmap_update(&vk.fd)) {
        vk_save_world_lightstyles(&vk.fd);
        return;
    }

    if (!vk_build_world_mesh(vk.world.cache, vk.fd_valid ? &vk.fd : NULL))
        Com_WPrintf("Couldn't rebuild Vulkan world mesh: %s\n", Com_GetLastError());
    else if (vk.fd_valid && vk.fd.lightstyles)
        vk_save_world_lightstyles(vk.fd_valid ? &vk.fd : NULL);

    vk_clear_world_lighting_modified();
}

static void vk_register_world_images(bsp_t *bsp)
{
    char buffer[MAX_QPATH];

    if (!bsp || !bsp->texinfo)
        return;

    if (bsp->numtexinfo > 0) {
        vk.world.texinfo_widths = Z_Mallocz(
            sizeof(*vk.world.texinfo_widths) * bsp->numtexinfo);
        vk.world.texinfo_heights = Z_Mallocz(
            sizeof(*vk.world.texinfo_heights) * bsp->numtexinfo);
    }

    for (int i = 0; i < bsp->numtexinfo; i++) {
        mtexinfo_t *info = &bsp->texinfo[i];

        if ((info->c.flags & SURF_SKY) ||
            ((info->c.flags & SURF_NODRAW) && bsp->has_bspx)) {
            info->image = R_NOTEXTURE;
            continue;
        }

        imageflags_t flags = (info->c.flags & SURF_WARP) ? IF_TURBULENT : IF_NONE;
        Q_concat(buffer, sizeof(buffer), "textures/", info->name, ".wal");
        info->image = IMG_Find(buffer, IT_WALL, flags);
        if (info->image != R_NOTEXTURE) {
            vk.world.texinfo_widths[i] = info->image->width;
            vk.world.texinfo_heights[i] = info->image->height;
        }
    }
}

static void vk_prepare_face_lightmap(const bsp_t *bsp, mface_t *face)
{
    if (!bsp || !face || !face->texinfo)
        return;
    if (bsp->lm_decoupled) {
        float len0 = VectorLength(face->lm_axis[0]);
        float len1 = VectorLength(face->lm_axis[1]);
        face->lm_scale[0] = len0 ? 1.0f / len0 : 0.0f;
        face->lm_scale[1] = len1 ? 1.0f / len1 : 0.0f;
        return;
    }
    if (!vk_face_edges_are_valid(bsp, face))
        return;

    vec2_t mins = { 99999.0f, 99999.0f };
    vec2_t maxs = { -99999.0f, -99999.0f };

    for (int i = 0; i < face->numsurfedges; i++) {
        const msurfedge_t *surfedge = face->firstsurfedge + i;
        const medge_t *edge = bsp->edges + surfedge->edge;
        const mvertex_t *vert = bsp->vertices + edge->v[surfedge->vert];

        for (int axis = 0; axis < 2; axis++) {
            float tc = DotProduct(vert->point, face->texinfo->axis[axis]) +
                       face->texinfo->offset[axis];
            mins[axis] = min(mins[axis], tc);
            maxs[axis] = max(maxs[axis], tc);
        }
    }

    int bmins[2], bmaxs[2];
    bmins[0] = floorf(mins[0] / 16.0f);
    bmins[1] = floorf(mins[1] / 16.0f);
    bmaxs[0] = ceilf(maxs[0] / 16.0f);
    bmaxs[1] = ceilf(maxs[1] / 16.0f);

    VectorScale(face->texinfo->axis[0], 1.0f / 16.0f, face->lm_axis[0]);
    VectorScale(face->texinfo->axis[1], 1.0f / 16.0f, face->lm_axis[1]);
    face->lm_offset[0] = face->texinfo->offset[0] / 16.0f - bmins[0];
    face->lm_offset[1] = face->texinfo->offset[1] / 16.0f - bmins[1];
    face->lm_width = bmaxs[0] - bmins[0] + 1;
    face->lm_height = bmaxs[1] - bmins[1] + 1;
    face->lm_scale[0] = 16.0f;
    face->lm_scale[1] = 16.0f;
}

static void vk_prepare_world_surfaces(bsp_t *bsp)
{
    vk.world.has_sky = false;

    if (!bsp || !bsp->faces)
        return;

    int n64surfs = 0;

    memset(vk.world.lightstyle_used, 0, sizeof(vk.world.lightstyle_used));

    for (int i = 0; i < bsp->numfaces; i++) {
        mface_t *face = &bsp->faces[i];

        if (!face->texinfo)
            continue;

        face->drawflags |= face->texinfo->c.flags & ~DSURF_PLANEBACK;

        if (face->drawflags & SURF_SKY)
            vk.world.has_sky = true;

        for (int j = 0; j < face->numstyles; j++) {
            unsigned style = face->styles[j];
            if (style < MAX_LIGHTSTYLES)
                vk.world.lightstyle_used[style] = true;
        }

        if ((face->drawflags & SURF_NODRAW) && !bsp->has_bspx)
            face->drawflags &= ~SURF_NODRAW;

        if (face->drawflags & (SURF_N64_UV | SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y))
            n64surfs++;

        vk_prepare_face_lightmap(bsp, face);
    }

    vk.world.nolm_mask = SURF_NOLM_MASK_DEFAULT;
    if (bsp->has_bspx || n64surfs > 100)
        vk.world.nolm_mask = SURF_NOLM_MASK_REMASTER;
}

static void vk_mark_world_images_registered(bsp_t *bsp)
{
    if (!bsp || !bsp->texinfo)
        return;

    for (int i = 0; i < bsp->numtexinfo; i++) {
        image_t *image = bsp->texinfo[i].image;
        if (image)
            image->registration_sequence = r_registration_sequence;
    }
}

static bool vk_glare_lightmap_color(const bsp_t *bsp, const mface_t *surf,
                                    vec3_t color)
{
    int size = surf->lm_width * surf->lm_height * 3;
    int sc = surf->lm_width / 2;
    int tc = surf->lm_height / 2;
    ptrdiff_t offset;
    const byte *lightmap;

    if (!bsp->lightmap || !surf->lightmap || !surf->numstyles ||
        surf->lightmap < bsp->lightmap)
        return false;

    offset = surf->lightmap - bsp->lightmap;
    if (offset < 0 ||
        (uint64_t)offset + (uint64_t)surf->numstyles * size >
        (uint64_t)bsp->numlightmapbytes)
        return false;

    VectorClear(color);
    lightmap = surf->lightmap;
    for (int i = 0; i < surf->numstyles; i++) {
        const byte *pixel = lightmap + 3 * (tc * surf->lm_width + sc);

        color[0] += pixel[0];
        color[1] += pixel[1];
        color[2] += pixel[2];
        lightmap += size;
    }

    // GL_BuildGlareList samples the shader lightmap atlas. Reproduce the
    // color adjustment performed while that atlas is built.
    float brightest = max(color[0], max(color[1], color[2]));
    if (brightest > 255.0f)
        VectorScale(color, 255.0f / brightest, color);

    float saturation = vk_coloredlightmaps ?
        Cvar_ClampValue(vk_coloredlightmaps, 0.0f, 1.0f) : 1.0f;
    if (saturation != 1.0f) {
        float y = LUMINANCE(color[0], color[1], color[2]);
        color[0] = y + (color[0] - y) * saturation;
        color[1] = y + (color[1] - y) * saturation;
        color[2] = y + (color[2] - y) * saturation;
    }

    VectorScale(color, 1.0f / 255.0f, color);
    return true;
}

static void vk_build_glare_list(bsp_t *bsp)
{
    glr.num_glare_sources = 0;
    memset(vk.glare_query_counts, 0, sizeof(vk.glare_query_counts));

    if (!bsp || !bsp->faces)
        return;

    for (int i = 0; i < bsp->numfaces; i++) {
        mface_t *surf = &bsp->faces[i];

        if ((surf->drawflags & SURF_NODRAW) || !surf->texinfo ||
            !surf->texinfo->image || !surf->texinfo->image->texnum2 ||
            !surf->lightmap || !surf->plane || !surf->firstsurfedge ||
            (surf->drawflags & vk.world.nolm_mask) ||
            surf->numsurfedges <= 0 || surf->lm_width <= 0 ||
            surf->lm_height <= 0 ||
            surf->lm_width > VK_MAX_LIGHTMAP_EXTENTS ||
            surf->lm_height > VK_MAX_LIGHTMAP_EXTENTS)
            continue;
        if (!vk_face_edges_are_valid(bsp, surf))
            continue;

        vec3_t lightcolor;
        if (!vk_glare_lightmap_color(bsp, surf, lightcolor))
            continue;

        float brightness = (lightcolor[0] + lightcolor[1] + lightcolor[2]) *
            (1.0f / 3.0f);

        if (brightness < (vk_glare_threshold ? vk_glare_threshold->value : 0.3f))
            continue;
        if (glr.num_glare_sources >= MAX_GLARE_SOURCES)
            break;

        vec3_t center = { 0, 0, 0 };
        const msurfedge_t *src_surfedge = surf->firstsurfedge;
        for (int j = 0; j < surf->numsurfedges; j++) {
            const medge_t *src_edge = bsp->edges + src_surfedge->edge;
            const mvertex_t *src_vert = bsp->vertices + src_edge->v[src_surfedge->vert];
            VectorAdd(center, src_vert->point, center);
            src_surfedge++;
        }
        VectorScale(center, 1.0f / surf->numsurfedges, center);

        vec3_t normal;
        VectorCopy(surf->plane->normal, normal);
        if (surf->drawflags & DSURF_PLANEBACK)
            VectorNegate(normal, normal);

        glare_source_t *gs = &glr.glare_sources[glr.num_glare_sources++];
        VectorMA(center, 2.0f, normal, gs->origin);
        VectorCopy(normal, gs->normal);
        VectorCopy(lightcolor, gs->lightcolor);
        gs->brightness = brightness;
        gs->visibility = 0.0f;
        gs->visible = false;
    }
}

static void vk_load_world(const char *name)
{
    char buffer[MAX_QPATH];
    bsp_t *bsp;
    int ret;

    if (!name || !*name)
        return;

    Q_concat(buffer, sizeof(buffer), "maps/", name, ".bsp");
    ret = BSP_Load(buffer, &bsp);
    if (!bsp)
        Com_Error(ERR_DROP, "%s: couldn't load %s: %s",
                  __func__, buffer, BSP_ErrorString(ret));

    if (vk.world.cache == bsp) {
        vk_mark_world_images_registered(bsp);
        vk_build_glare_list(bsp);
        bsp->refcount--;
        return;
    }

    vk_free_world();
    vk.world.cache = bsp;
    vk.world.size = vk_world_size_for_bsp(bsp);

    vk_register_world_images(bsp);
    vk_load_rt_materials();
    vk_prepare_world_surfaces(bsp);

    if (!vk_build_world_mesh(bsp, NULL))
        Com_WPrintf("Couldn't build Vulkan world mesh: %s\n", Com_GetLastError());
    vk_build_glare_list(bsp);
    vk_clear_world_lighting_modified();
}

static void vk_draw_polyblend(const refdef_t *fd)
{
    if (!vk_polyblend || !vk_polyblend->integer)
        return;

    if (fd->screen_blend[3])
        vk_blend_rect(fd->x, fd->y, fd->width, fd->height, fd->screen_blend);

    if (fd->damage_blend[3]) {
        float frac = vk_damageblend_frac ?
            Cvar_ClampValue(vk_damageblend_frac, 0.0f, 0.5f) : 0.2f;

        vk_blend_vignette(fd->x, fd->y, fd->width, fd->height,
                          fd->damage_blend, frac);
    }
}

#if USE_DEBUG
static int vk_auto_scale(void)
{
    int scale = 1;

    if (r_config.height < r_config.width) {
        if (r_config.height >= 2160)
            scale = 4;
        else if (r_config.height >= 1080)
            scale = 2;
    } else {
        if (r_config.width >= 3840)
            scale = 4;
        else if (r_config.width >= 1920)
            scale = 2;
    }

    if (vid && vid->get_dpi_scale)
        scale = max(scale, vid->get_dpi_scale());

    return scale;
}

static void vk_draw_stat_string(int x, int y, const char *fmt, ...)
{
    va_list argptr;
    char buffer[MAX_STRING_CHARS];

    va_start(argptr, fmt);
    Q_vsnprintf(buffer, sizeof(buffer), fmt, argptr);
    va_end(argptr);

    VKR_DrawString(x, y, 0, -1, buffer, r_charset);
}

static void vk_draw_stats(void)
{
    int x = 10, y = 10;
#if USE_VULKAN_RAYTRACING
    const int stat_lines = 30;
#else
    const int stat_lines = 26;
#endif

    VKR_SetScale(1.0f / vk_auto_scale());
    VKR_DrawFill8(8, 8, 25 * 8, stat_lines * 10 + 2, 4);

    vk_draw_stat_string(x, y, "Nodes visible  : %i", glr.nodes_visible); y += 10;
    vk_draw_stat_string(x, y, "Nodes culled   : %i", c.nodesCulled); y += 10;
    vk_draw_stat_string(x, y, "Nodes drawn    : %i", c.nodesDrawn); y += 10;
    vk_draw_stat_string(x, y, "Leaves drawn   : %i", c.leavesDrawn); y += 10;
    vk_draw_stat_string(x, y, "Faces drawn    : %i", c.facesDrawn); y += 10;
    vk_draw_stat_string(x, y, "Faces culled   : %i", c.facesCulled); y += 10;
    vk_draw_stat_string(x, y, "Boxes culled   : %i", c.boxesCulled); y += 10;
    vk_draw_stat_string(x, y, "Spheres culled : %i", c.spheresCulled); y += 10;
    vk_draw_stat_string(x, y, "RtBoxes culled : %i", c.rotatedBoxesCulled); y += 10;
    vk_draw_stat_string(x, y, "Shadows culled : %i", c.shadowsCulled); y += 10;
    vk_draw_stat_string(x, y, "Tris drawn     : %i", c.trisDrawn); y += 10;
    vk_draw_stat_string(x, y, "Tex switches   : %i", c.texSwitches); y += 10;
    vk_draw_stat_string(x, y, "Tex uploads    : %i", c.texUploads); y += 10;
    vk_draw_stat_string(x, y, "LM texels      : %i", c.lightTexels); y += 10;
    vk_draw_stat_string(x, y, "Batches drawn  : %i", c.batchesDrawn); y += 10;
    vk_draw_stat_string(x, y, "Faces / batch  : %.1f", c.batchesDrawn ? (float)c.facesDrawn / c.batchesDrawn : 0.0f); y += 10;
    vk_draw_stat_string(x, y, "Tris / batch   : %.1f", c.batchesDrawn ? (float)c.facesTris / c.batchesDrawn : 0.0f); y += 10;
    vk_draw_stat_string(x, y, "2D batches     : %i", c.batchesDrawn2D); y += 10;
    vk_draw_stat_string(x, y, "Draw calls     : %i", c.batchesDrawn + c.batchesDrawn2D); y += 10;
    vk_draw_stat_string(x, y, "Pipeline binds : %i", c.pipelineBinds); y += 10;
    vk_draw_stat_string(x, y, "Total entities : %i", glr.fd.num_entities); y += 10;
    vk_draw_stat_string(x, y, "Total dlights  : %i", glr.fd.num_dlights); y += 10;
    vk_draw_stat_string(x, y, "Total particles: %i", glr.fd.num_particles); y += 10;
    vk_draw_stat_string(x, y, "Uniform uploads: %i", c.uniformUploads); y += 10;
    vk_draw_stat_string(x, y, "Array binds    : %i", c.vertexArrayBinds); y += 10;
    vk_draw_stat_string(x, y, "Occl. queries  : %i", c.occlusionQueries); y += 10;
#if USE_VULKAN_RAYTRACING
    vk_draw_stat_string(x, y, "RT active      : %s",
                        vk.raytracing_active ? "yes" : "no"); y += 10;
    vk_draw_stat_string(x, y, "RT surf lights : %u",
                        vk.world.surface_light_count); y += 10;
    vk_draw_stat_string(x, y, "RT static bake : %s A%u E%u %ums",
                        vk.world.rt_ao_ready ? "yes" : "no",
                        vk.world.rt_ao_samples,
                        vk.world.rt_emissive_samples,
                        vk.world.rt_bake_msec); y += 10;
    vk_draw_stat_string(x, y, "RT query mode  : %s",
                        vk.raytracing_quad_sharing ? "adaptive" : "pixel"); y += 10;
#endif

    VKR_SetScale(1.0f);
}
#endif

static void vk_log_perf_stats(void)
{
    if (!vk_perf_stats || !vk_perf_stats->integer || !vk.fd_valid)
        return;
    if (vk.fd.time < vk.perf_stats_time + 1.0f)
        return;

    vk.perf_stats_time = vk.fd.time;
    Com_Printf("VK perf: mode=%s imgs=%u fif=%u draws=%i 3d=%i world=%i ent=%i part=%i bloom=%i other=%i 2d=%i chars=%i pics=%i rects=%i pipe=%i desc=%i push=%i vb=%i tris=%i faces=%i ents=%i parts=%i gpu=%uus wait=%uus acq=%uus rec=%uus sub=%uus pres=%uus\n",
               vk_present_mode_name(vk.present_mode),
               vk.swapchain_image_count, vk_frames_in_flight_value(),
               c.batchesDrawn + c.batchesDrawn2D,
               c.batchesDrawn, c.worldBatches, c.entityBatches,
               c.particleBatches, c.bloomBatches, c.otherBatches,
               c.batchesDrawn2D,
               c.charsDrawn2D, c.picsDrawn2D, c.rectsDrawn2D,
               c.pipelineBinds, c.texSwitches, c.uniformUploads,
               c.vertexArrayBinds, c.trisDrawn, c.facesDrawn,
               glr.fd.num_entities, glr.fd.num_particles,
               vk.gpu_frame_usec,
               vk.wait_usec, vk.acquire_usec, vk.record_usec,
               vk.submit_usec, vk.present_usec);
}

bool VKR_Init(bool total)
{
    if (!total) {
        r_registration_sequence = 1;
        vk_build_gamma_table();
        IMG_Init();
        IMG_SetUploadBackend(&vk_image_upload);
        if (!vk_create_default_texture() || !vk_create_shell_texture()) {
            Com_WPrintf("Couldn't recreate Vulkan default textures: %s\n",
                        Com_GetLastError());
            IMG_Shutdown();
            return false;
        }
        IMG_GetPalette();
        return true;
    }

    Com_Printf("------- VKR_Init -------\n");
    Com_Printf("Using video driver: %s\n", vid->name);

    vk_drawentities = Cvar_Get("gl_drawentities", "1", CVAR_CHEAT);
    vk_drawsky = Cvar_Get("gl_drawsky", "1", 0);
    vk_drawsky->changed = vk_drawsky_changed;
    vk_swapinterval = Cvar_Get("gl_swapinterval", "1", CVAR_ARCHIVE);
    vk_swapinterval->changed = vk_swapinterval_changed;
    vk_present_mode = Cvar_Get("vk_present_mode", "0", CVAR_ARCHIVE);
    vk_present_mode->changed = vk_present_mode_changed;
    vk_finish = Cvar_Get("gl_finish", "0", 0);
    vk_texturemode = Cvar_Get("gl_texturemode", "GL_LINEAR_MIPMAP_LINEAR",
                              CVAR_ARCHIVE);
    vk_texturemode->changed = vk_texturemode_changed;
    vk_texturemode->generator = vk_texturemode_g;
    vk_anisotropy = Cvar_Get("gl_anisotropy", "1", 0);
    vk_anisotropy->changed = vk_texturemode_changed;
    vk_round_down = Cvar_Get("gl_round_down", "0", CVAR_FILES);
    vk_picmip = Cvar_Get("gl_picmip", "0", CVAR_FILES);
    vk_downsample_skins = Cvar_Get("gl_downsample_skins", "1", CVAR_FILES);
    vk_bilerp_chars = Cvar_Get("gl_bilerp_chars", "0", 0);
    vk_bilerp_chars->changed = vk_sampler_selection_changed;
    vk_bilerp_pics = Cvar_Get("gl_bilerp_pics", "1", 0);
    vk_bilerp_pics->changed = vk_sampler_selection_changed;
    vk_bilerp_skies = Cvar_Get("gl_bilerp_skies", "1", 0);
    vk_bilerp_skies->changed = vk_sampler_selection_changed;
    vk_cubemaps = Cvar_Get("gl_cubemaps", "0", CVAR_FILES);
    vk_saturation = Cvar_Get("gl_saturation", "1", CVAR_FILES);
    vk_invert = Cvar_Get("gl_invert", "0", CVAR_FILES);
    vk_gamma_scale_pics = Cvar_Get("gl_gamma_scale_pics", "0", CVAR_FILES);
    vk_upscale_pcx = Cvar_Get("gl_upscale_pcx", "0", CVAR_FILES);
#if USE_MD5
    vk_md5_load = Cvar_Get("gl_md5_load", "1", CVAR_FILES);
    vk_md5_use = Cvar_Get("gl_md5_use", "1", 0);
    vk_md5_distance = Cvar_Get("gl_md5_distance", "2048", 0);
#endif
    vk_gamma = Cvar_Get("vid_gamma", "1", CVAR_ARCHIVE);
    if (r_config.flags & QVF_GAMMARAMP) {
        vk_gamma->changed = vk_gamma_changed;
        vk_gamma->flags &= ~CVAR_FILES;
    } else {
        vk_gamma->flags |= CVAR_FILES;
    }
    vk_build_gamma_table();
    if (r_config.flags & QVF_GAMMARAMP)
        vk_gamma_changed(vk_gamma);
    vk_partscale = Cvar_Get("gl_partscale", "2", 0);
    vk_partstyle = Cvar_Get("gl_partstyle", "0", 0);
    vk_partshape = Cvar_Get("gl_partshape", "0", 0);
    vk_partshape->changed = vk_partshape_changed;
    vk_beamstyle = Cvar_Get("gl_beamstyle", "0", 0);
    vk_flarespeed = Cvar_Get("gl_flarespeed", "8", 0);
    vk_lightgrid = Cvar_Get("gl_lightgrid", "1", 0);
    vk_fullbright = Cvar_Get("r_fullbright", "0", CVAR_CHEAT);
    vk_cull_models = Cvar_Get("gl_cull_models", "1", 0);
    vk_shadows = Cvar_Get("gl_shadows", "0", CVAR_ARCHIVE);
    vk_stencilbits = Cvar_Get("gl_stencilbits", "8", CVAR_REFRESH);
    vk_celshading = Cvar_Get("gl_celshading", "0", 0);
    vk_dotshading = Cvar_Get("gl_dotshading", "1", 0);
    vk_draworder = Cvar_Get("gl_draworder", "1", 0);
    vk_showorigins = Cvar_Get("gl_showorigins", "0", CVAR_CHEAT);
    vk_showtearing = Cvar_Get("gl_showtearing", "0", CVAR_CHEAT);
    vk_showbloom = Cvar_Get("gl_showbloom", "0", CVAR_CHEAT);
    vk_waterwarp = Cvar_Get("gl_waterwarp", "0", 0);
    vk_bloom_sigma = Cvar_Get("gl_bloom_sigma", "4", 0);
    vk_bloom_downsample = Cvar_Get("vk_bloom_downsample", "4", 0);
    vk_bloom_streaks = Cvar_Get("vk_bloom_streaks", "0.45", CVAR_ARCHIVE);
    vk_bloom_ghosts = Cvar_Get("vk_bloom_ghosts", "0.5", CVAR_ARCHIVE);
    vk_bloom_shafts = Cvar_Get("vk_bloom_shafts", "0.4", CVAR_ARCHIVE);
    vk_glare = Cvar_Get("gl_glare", "0", CVAR_ARCHIVE);
    vk_glare->changed = vk_glare_changed;
    vk_glare_threshold = Cvar_Get("gl_glare_threshold", "0.3", 0);
    vk_glare_threshold->changed = vk_glare_threshold_changed;
    vk_glare_size = Cvar_Get("gl_glare_size", "24", 0);
    vk_glare_intensity = Cvar_Get("gl_glare_intensity", "0.5", 0);
    vk_perf_stats = Cvar_Get("vk_perf_stats", "0", 0);
    vk_frames_in_flight = Cvar_Get("vk_frames_in_flight", "2", CVAR_ARCHIVE);
    vk_device = Cvar_Get("vk_device", "", CVAR_ARCHIVE | CVAR_REFRESH);
    vk_devicelist = Cvar_Get("vk_devicelist", "\"automatic\" \"\"", CVAR_ROM);
    vk_raytracing = Cvar_Get("vk_raytracing", "0", CVAR_ARCHIVE | CVAR_REFRESH);
    vk_rt_emissive = Cvar_Get("vk_rt_emissive", "0.35", CVAR_ARCHIVE);
    vk_rt_ao = Cvar_Get("vk_rt_ao", "0.24", CVAR_ARCHIVE);
    vk_rt_skylight = Cvar_Get("vk_rt_skylight", "0.6", CVAR_ARCHIVE);
    vk_rt_environment = Cvar_Get("vk_rt_environment", "0.65", CVAR_ARCHIVE);
    vk_rt_afterglow = Cvar_Get("vk_rt_afterglow", "0.35", CVAR_ARCHIVE);
    vk_rt_sunlight = Cvar_Get("vk_rt_sunlight", "0.65", CVAR_ARCHIVE);
    vk_rt_reflections = Cvar_Get("vk_rt_reflections", "0.35", CVAR_ARCHIVE);
    vk_rt_specular = Cvar_Get("vk_rt_specular", "0.45", CVAR_ARCHIVE);
    vk_rt_liquids = Cvar_Get("vk_rt_liquids", "0.65", CVAR_ARCHIVE);
    vk_rt_bounce = Cvar_Get("vk_rt_bounce", "0.65", CVAR_ARCHIVE);
    vk_rt_caustics = Cvar_Get("vk_rt_caustics", "0.65", CVAR_ARCHIVE);
    vk_rt_shadow_fringe = Cvar_Get("vk_rt_shadow_fringe", "0.6", CVAR_ARCHIVE);
    vk_rt_debug = Cvar_Get("vk_rt_debug", "0", 0);
#if USE_DEBUG
    vk_showstats = Cvar_Get("gl_showstats", "0", 0);
#endif
    gl_showtris = Cvar_Get("gl_showtris", "0", CVAR_CHEAT);
    vk_modulate = Cvar_Get("gl_modulate", "1", CVAR_ARCHIVE);
    vk_modulate_entities = Cvar_Get("gl_modulate_entities", "1", 0);
    vk_doublelight_entities = Cvar_Get("gl_doublelight_entities", "1", 0);
    vk_fontshadow = Cvar_Get("gl_fontshadow", "0", 0);
    gl_glowmap_intensity = Cvar_Get("gl_glowmap_intensity", "0.75", 0);
    r_lava_glowmaps = Cvar_Get("r_lava_glowmaps", "1", 0);
    vk_modulate_world = Cvar_Get("gl_modulate_world", "1", 0);
    vk_coloredlightmaps = Cvar_Get("gl_coloredlightmaps", "1", 0);
    vk_dynamic = Cvar_Get("gl_dynamic", "1", 0);
    vk_dlight_falloff = Cvar_Get("gl_dlight_falloff", "1", 0);
    vk_brightness = Cvar_Get("gl_brightness", "0", 0);
    vk_fog = Cvar_Get("gl_fog", "1", 0);
    vk_intensity = Cvar_Get("intensity", "2", 0);
    vk_shaders = Cvar_Get("gl_shaders", "1", CVAR_FILES);

    gl_intensity = vk_intensity;
    gl_shaders = vk_shaders;
    gl_modulate_world = vk_modulate_world;
    gl_modulate_entities = vk_modulate_entities;
    gl_brightness = vk_brightness;
    // Console and screen line layout use the renderer-neutral gl_fontshadow
    // pointer.  The OpenGL backend initializes it directly; expose Vulkan's
    // handle as well so shadow modes 1 and 2 add the matching vertical space.
    gl_fontshadow = vk_fontshadow;
    vk_znear = Cvar_Get("gl_znear", "2", CVAR_CHEAT);
    vk_drawworld = Cvar_Get("gl_drawworld", "1", CVAR_CHEAT);
    vk_novis = Cvar_Get("gl_novis", "0", 0);
    vk_lockpvs = Cvar_Get("gl_lockpvs", "0", CVAR_CHEAT);
    vk_lightmap = Cvar_Get("gl_lightmap", "0", CVAR_CHEAT);
    vk_pixel_lightmaps = Cvar_Get("vk_pixel_lightmaps", "2", 0);
    vk_vertexlight = Cvar_Get("gl_vertexlight", "0", 0);
    vk_nobind = Cvar_Get("gl_nobind", "0", CVAR_CHEAT);
    vk_clear = Cvar_Get("gl_clear", "0", 0);
    vk_clearcolor = Cvar_Get("gl_clearcolor", "black", 0);
    vk_clearcolor->generator = Com_Color_g;
    vk_polyblend = Cvar_Get("gl_polyblend", "1", 0);
    vk_damageblend_frac = Cvar_Get("gl_damageblend_frac", "0.2", 0);
    gl_bloom = Cvar_Get("gl_bloom", "0", 0);
    vk_cull_nodes = Cvar_Get("gl_cull_nodes", "1", 0);
#if USE_DEBUG
    vk_debug_distfrac = Cvar_Get("gl_debug_distfrac", "0.004", 0);
    vk_debug_linewidth = Cvar_Get("gl_debug_linewidth", "2", 0);
#endif

    if (!vid->init())
        return false;

    if (!vk_load_global() ||
        !vk_create_instance() ||
        !vk_create_surface() ||
        !vk_pick_physical_device() ||
        !vk_create_device() ||
        !vk_create_frame_resources() ||
        !vk_create_glare_query_pool() ||
        !vk_create_timestamp_query_pool()) {
        VKR_Shutdown(true);
        return false;
    }

    if (!vk_create_skybox_mesh())
        Com_WPrintf("Couldn't create Vulkan skybox mesh: %s\n", Com_GetLastError());
    if (!vk_create_sprite_quad())
        Com_WPrintf("Couldn't create Vulkan sprite quad: %s\n", Com_GetLastError());
    if (!vk_create_particle_buffer())
        Com_WPrintf("Couldn't create Vulkan particle buffer: %s\n", Com_GetLastError());
    if (!vk_create_beam_cylinder())
        Com_WPrintf("Couldn't create Vulkan beam cylinder: %s\n", Com_GetLastError());
    if (!vk_create_null_model())
        Com_WPrintf("Couldn't create Vulkan null model: %s\n", Com_GetLastError());
#if USE_DEBUG
    if (!vk_create_debug_line_buffer())
        Com_WPrintf("Couldn't create Vulkan debug line buffer: %s\n", Com_GetLastError());
    if (!vk_create_debug_text_buffers())
        Com_WPrintf("Couldn't create Vulkan debug text buffers: %s\n", Com_GetLastError());
#endif
    if (!vk_create_particle_texture())
        Com_WPrintf("Couldn't create Vulkan particle texture: %s\n", Com_GetLastError());
    if (!vk_create_beam_texture())
        Com_WPrintf("Couldn't create Vulkan beam texture: %s\n", Com_GetLastError());

    r_registration_sequence = 1;
    IMG_Init();
    IMG_SetUploadBackend(&vk_image_upload);
    if (!vk_create_default_texture())
        Com_WPrintf("Couldn't create Vulkan default texture: %s\n", Com_GetLastError());
    if (!vk_create_shell_texture())
        Com_WPrintf("Couldn't create Vulkan shell texture: %s\n", Com_GetLastError());
    IMG_GetPalette();
#if USE_DEBUG
    r_charset = VKR_RegisterImage("conchars", IT_FONT, IF_PERMANENT);
#endif

    Cmd_AddCommand("strings", vk_strings_f);
    Cmd_AddCommand("modellist", vk_model_list_f);
#if USE_DEBUG
    R_ClearDebugLines();
    Cmd_AddCommand("cleardebuglines", R_ClearDebugLines);
    Cmd_AddMacro("gl_viewcluster", vk_viewcluster_m);
    Cmd_AddMacro("gl_viewleaf", vk_viewleaf_m);
#endif

    Com_Printf("------------------------\n");
    return true;
}

void VKR_Shutdown(bool total)
{
    if (!total) {
        if (vk.device && vk.DeviceWaitIdle)
            vk.DeviceWaitIdle(vk.device);
        vk_free_world();
        vk_free_models(true);
#if USE_DEBUG
        r_charset = 0;
#endif
        if (r_numImages) {
            IMG_FreeAll();
            IMG_Shutdown();
        }
        return;
    }

    if (vk_swapinterval)
        vk_swapinterval->changed = NULL;
    if (vk_present_mode)
        vk_present_mode->changed = NULL;
    if (vk_texturemode)
        vk_texturemode->changed = NULL;
    if (vk_texturemode)
        vk_texturemode->generator = NULL;
    if (vk_drawsky)
        vk_drawsky->changed = NULL;
    if (vk_anisotropy)
        vk_anisotropy->changed = NULL;
    if (vk_bilerp_chars)
        vk_bilerp_chars->changed = NULL;
    if (vk_bilerp_pics)
        vk_bilerp_pics->changed = NULL;
    if (vk_bilerp_skies)
        vk_bilerp_skies->changed = NULL;
    if (vk_gamma)
        vk_gamma->changed = NULL;
    if (vk_partshape)
        vk_partshape->changed = NULL;
    if (vk_glare)
        vk_glare->changed = NULL;
    if (vk_glare_threshold)
        vk_glare_threshold->changed = NULL;
    if (vk_clearcolor)
        vk_clearcolor->generator = NULL;

    Cmd_RemoveCommand("strings");
    Cmd_RemoveCommand("modellist");
#if USE_DEBUG
    Cmd_RemoveCommand("cleardebuglines");
    r_charset = 0;
#endif

    if (r_numImages) {
        IMG_FreeAll();
        vk_destroy_texture_resource(&vk.textures[0]);
        vk_destroy_texture_resource(&vk.textures[1]);
        IMG_Shutdown();
    }

    vk_destroy_texture_resource(&vk.raw_texture);
    vk_destroy_texture_resource(&vk.particle_texture);
    vk_destroy_texture_resource(&vk.beam_texture);

    vk_destroy_swapchain();

    if (vk.device && vk.DeviceWaitIdle)
        vk.DeviceWaitIdle(vk.device);

    if (vk.glare_query_pool) {
        vk.DestroyQueryPool(vk.device, vk.glare_query_pool, NULL);
        vk.glare_query_pool = VK_NULL_HANDLE;
    }
    if (vk.timestamp_query_pool) {
        vk.DestroyQueryPool(vk.device, vk.timestamp_query_pool, NULL);
        vk.timestamp_query_pool = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++) {
        if (vk.frame_fence[i]) {
            vk.DestroyFence(vk.device, vk.frame_fence[i], NULL);
            vk.frame_fence[i] = VK_NULL_HANDLE;
        }
        if (vk.render_finished[i]) {
            vk.DestroySemaphore(vk.device, vk.render_finished[i], NULL);
            vk.render_finished[i] = VK_NULL_HANDLE;
        }
        if (vk.image_available[i]) {
            vk.DestroySemaphore(vk.device, vk.image_available[i], NULL);
            vk.image_available[i] = VK_NULL_HANDLE;
        }
    }

    if (vk.command_pool) {
        vk.DestroyCommandPool(vk.device, vk.command_pool, NULL);
        vk.command_pool = VK_NULL_HANDLE;
    }

    vk_free_world();
    vk_free_models(true);
    vk_destroy_mesh(&vk.skybox);
    vk_destroy_mesh(&vk.sprite_quad);
    vk_destroy_buffer(&vk.sprite_quad_line_indices);
    vk.sprite_quad_line_index_count = 0;
    vk_destroy_mesh(&vk.null_model);
    vk_destroy_mesh(&vk.beam_cylinder);
    vk_destroy_buffer(&vk.beam_cylinder_line_indices);
    vk.beam_cylinder_line_index_count = 0;
    if (vk.particle_vertices_mapped) {
        vk.UnmapMemory(vk.device, vk.particle_vertices.memory);
        vk.particle_vertices_mapped = NULL;
    }
    vk_destroy_buffer(&vk.particle_vertices);
    vk_destroy_buffer(&vk.debug_lines);
    vk_destroy_buffer(&vk.debug_text_vertices);
    vk_destroy_buffer(&vk.debug_text_indices);

#if USE_VULKAN_RAYTRACING
    if (vk.rt_ao_pipeline) {
        vk.DestroyPipeline(vk.device, vk.rt_ao_pipeline, NULL);
        vk.rt_ao_pipeline = VK_NULL_HANDLE;
    }
    if (vk.rt_ao_pipeline_layout) {
        vk.DestroyPipelineLayout(vk.device, vk.rt_ao_pipeline_layout, NULL);
        vk.rt_ao_pipeline_layout = VK_NULL_HANDLE;
    }
    if (vk.rt_ao_render_pass) {
        vk.DestroyRenderPass(vk.device, vk.rt_ao_render_pass, NULL);
        vk.rt_ao_render_pass = VK_NULL_HANDLE;
    }
#endif

    if (vk.sampler) {
        vk.DestroySampler(vk.device, vk.sampler, NULL);
        vk.sampler = VK_NULL_HANDLE;
    }
    if (vk.sky_sampler) {
        vk.DestroySampler(vk.device, vk.sky_sampler, NULL);
        vk.sky_sampler = VK_NULL_HANDLE;
    }
    if (vk.postprocess_sampler) {
        vk.DestroySampler(vk.device, vk.postprocess_sampler, NULL);
        vk.postprocess_sampler = VK_NULL_HANDLE;
    }
    if (vk.nearest_sampler) {
        vk.DestroySampler(vk.device, vk.nearest_sampler, NULL);
        vk.nearest_sampler = VK_NULL_HANDLE;
    }
    if (vk.sky_nearest_sampler) {
        vk.DestroySampler(vk.device, vk.sky_nearest_sampler, NULL);
        vk.sky_nearest_sampler = VK_NULL_HANDLE;
    }

    if (vk.descriptor_pool) {
        vk.DestroyDescriptorPool(vk.device, vk.descriptor_pool, NULL);
        vk.descriptor_pool = VK_NULL_HANDLE;
    }

    if (vk.texture_set_layout) {
        vk.DestroyDescriptorSetLayout(vk.device, vk.texture_set_layout, NULL);
        vk.texture_set_layout = VK_NULL_HANDLE;
    }
    if (vk.ssr_set_layout) {
        vk.DestroyDescriptorSetLayout(vk.device, vk.ssr_set_layout, NULL);
        vk.ssr_set_layout = VK_NULL_HANDLE;
    }
#if USE_VULKAN_RAYTRACING
    if (vk.rt_set_layout) {
        vk.DestroyDescriptorSetLayout(vk.device, vk.rt_set_layout, NULL);
        vk.rt_set_layout = VK_NULL_HANDLE;
    }
#endif

    if (vk.rect_pipeline_layout) {
        vk.DestroyPipelineLayout(vk.device, vk.rect_pipeline_layout, NULL);
        vk.rect_pipeline_layout = VK_NULL_HANDLE;
    }
    if (vk.ssr_pipeline_layout) {
        vk.DestroyPipelineLayout(vk.device, vk.ssr_pipeline_layout, NULL);
        vk.ssr_pipeline_layout = VK_NULL_HANDLE;
    }
    if (vk.pixel_world_pipeline_layout) {
        vk.DestroyPipelineLayout(vk.device, vk.pixel_world_pipeline_layout, NULL);
        vk.pixel_world_pipeline_layout = VK_NULL_HANDLE;
    }

    if (vk.device && vk.DestroyDevice) {
        vk.DestroyDevice(vk.device, NULL);
        vk.device = VK_NULL_HANDLE;
    }

    if (vk.surface && vk.DestroySurfaceKHR) {
        vk.DestroySurfaceKHR(vk.instance, vk.surface, NULL);
        vk.surface = VK_NULL_HANDLE;
    }

    if (vk.instance && vk.DestroyInstance) {
        vk.DestroyInstance(vk.instance, NULL);
        vk.instance = VK_NULL_HANDLE;
    }

    if (vk.library) {
        Sys_FreeLibrary(vk.library);
        vk.library = NULL;
    }

    if (vid)
        vid->shutdown();

    memset(&vk, 0, sizeof(vk));
}

void VKR_BeginRegistration(const char *map)
{
    r_registration_sequence++;
    memset(vk.flare_fracs, 0, sizeof(vk.flare_fracs));
    memset(vk.flare_times, 0, sizeof(vk.flare_times));
    memset(vk.afterglow_lights, 0, sizeof(vk.afterglow_lights));
    vk.afterglow_frame = 0;
    vk.afterglow_time = 0.0f;
    vk_load_world(map);
}

qhandle_t VKR_RegisterModel(const char *name)
{
    char normalized[MAX_QPATH];
    size_t namelen;
    byte *rawdata;
    int ret;
    vk_model_t *model;
    qhandle_t handle;
    bool supported;

    if (!name || !*name)
        return 0;
    if (*name == '*')
        return ~Q_atoi(name + 1);

    namelen = FS_NormalizePathBuffer(normalized, name, sizeof(normalized));
    if (!namelen || namelen >= sizeof(normalized))
        return 0;

    model = vk_find_model(normalized);
    if (model) {
        model->registration_sequence = r_registration_sequence;
        for (int i = 0; i < model->frame_count; i++) {
            if (model->type == VK_MODEL_SPRITE && model->frames[i].image)
                model->frames[i].image->registration_sequence = r_registration_sequence;
        }
        for (int i = 0; i < model->skin_count; i++) {
            if (model->skins[i])
                model->skins[i]->registration_sequence = r_registration_sequence;
        }
#if USE_MD5
        for (int i = 0; i < model->md5_skin_count; i++) {
            if (model->md5_skins[i])
                model->md5_skins[i]->registration_sequence = r_registration_sequence;
        }
#endif
        return (model - vk.models) + 1;
    }

    ret = FS_LoadFile(normalized, (void **)&rawdata);
    if (!rawdata)
        return 0;

    handle = 0;
    supported = false;
    if (ret >= 4 && LittleLong(*(uint32_t *)rawdata) == SP2_IDENT) {
        supported = true;
        handle = vk_load_sprite_model(normalized, rawdata, ret);
    } else if (ret >= 4 && LittleLong(*(uint32_t *)rawdata) == MD2_IDENT) {
        supported = true;
        handle = vk_load_md2_model(normalized, rawdata, ret);
    }
#if USE_MD3
    else if (ret >= 4 && LittleLong(*(uint32_t *)rawdata) == MD3_IDENT) {
        supported = true;
        handle = vk_load_md3_model(normalized, rawdata, ret);
    }
#endif

    FS_FreeFile(rawdata);
    if (!handle && supported)
        Com_WPrintf("Couldn't load Vulkan model %s\n", normalized);
    return handle;
}

qhandle_t VKR_RegisterImage(const char *name, imagetype_t type, imageflags_t flags)
{
    return IMG_RegisterImage(name, type, flags);
}

void VKR_SetSky(const char *name, float rotate, bool autorotate, const vec3_t axis)
{
    char pathname[MAX_QPATH];

    memset(vk.sky_images, 0, sizeof(vk.sky_images));
    vk.sky_cubemap = -1;
    vk.sky_rotate = 0.0f;
    vk.sky_autorotate = false;
    VectorSet(vk.sky_axis, 0.0f, 0.0f, 1.0f);

    if (!name || !*name || (vk_drawsky && !vk_drawsky->integer))
        return;

    if (rotate && VectorNormalize2(axis, vk.sky_axis) >= 0.001f) {
        vk.sky_rotate = rotate;
        vk.sky_autorotate = autorotate;
    }

    if (vk_cubemaps && vk_cubemaps->integer) {
        const image_t *image;

        if (Q_concat(pathname, sizeof(pathname), "sky/", name, ".tga") >=
            sizeof(pathname))
            return;
        image = IMG_Find(pathname, IT_SKY, IF_CUBEMAP);
        if (image && image != R_SKYTEXTURE) {
            vk_cubemap_t *cubemap = vk_find_cubemap(image);
            if (cubemap) {
                vk.sky_cubemap = cubemap - vk.cubemaps;
                return;
            }
        }
    }

    for (uint32_t i = 0; i < 6; i++) {
        const image_t *image;

        if (Q_concat(pathname, sizeof(pathname), "env/", name,
                     com_env_suf[i], ".tga") >= sizeof(pathname)) {
            memset(vk.sky_images, 0, sizeof(vk.sky_images));
            return;
        }

        image = IMG_Find(pathname, IT_SKY, IF_NONE);
        if (!image || image == R_SKYTEXTURE || !image->texnum ||
            image->texnum >= MAX_RIMAGES) {
            memset(vk.sky_images, 0, sizeof(vk.sky_images));
            return;
        }

        vk.sky_images[i] = image->texnum;
    }
}

void VKR_EndRegistration(void)
{
    vk_free_models(false);
    IMG_FreeUnused();
}

void VKR_RenderFrame(const refdef_t *fd)
{
    if (!fd || !vk.frame_active || !vk.render_pass_active ||
        !vk.command_buffers || vk.current_image >= vk.swapchain_image_count)
        return;

    vk.fd = *fd;
    vk.fd_valid = true;
#if USE_VULKAN_RAYTRACING
    vk.view_liquid_kind = vk.raytracing_active ? vk_view_liquid_kind(&vk.fd) : 0;
#else
    vk.view_liquid_kind = 0;
#endif
    if (!vk_dynamic_lights_enabled())
        vk.fd.num_dlights = 0;
    vk_update_afterglow(&vk.fd);
    glr.fd = vk.fd;
    fd = &vk.fd;

    vk_rebuild_world_lighting();
    vk_setup_world_frustum(fd);
    vk_update_world_view(fd);
    vk_set_3d_viewport(fd);

    bool drawworld = !(fd->rdflags & RDF_NOWORLDMODEL) &&
        (!vk_drawworld || vk_drawworld->integer);
    if (drawworld) {
        vk_mark_world_faces(fd);
    } else {
        vk.world.sky_visible = false;
    }

    if (drawworld)
        vk_draw_skybox(fd);

    if (drawworld && vk.world.mesh.index_count) {
        mat4_t mvp;

        vk_world_mvp(mvp, fd);
        vk_draw_world_mesh(mvp, false, vk.world_pipeline, VK_WORLD_OPAQUE,
                           1.0f, fd, NULL, NULL);
        vk_draw_world_outlines(mvp, false, VK_WORLD_OPAQUE, fd, NULL);
    }

    vk_draw_entities(fd, VK_ENTITY_BMODEL);
    vk_draw_entities(fd, VK_ENTITY_OPAQUE);
    vk_draw_entities(fd, VK_ENTITY_ALPHA_BACK);
    vk_draw_entities(fd, VK_ENTITY_BMODEL_ALPHA);
    if (drawworld && vk.world.mesh.index_count) {
        mat4_t mvp;

        vk_world_mvp(mvp, fd);
        vk_draw_world_mesh(mvp, false, vk.world_blend_pipeline, VK_WORLD_ALPHA,
                           1.0f, fd, NULL, NULL);
        vk_draw_world_outlines(mvp, false, VK_WORLD_ALPHA, fd, NULL);
    }
    vk_draw_entities(fd, VK_ENTITY_BEAM);
    vk_draw_particles(fd);
    vk_draw_glare(fd);
    vk_draw_entities(fd, VK_ENTITY_ALPHA_FRONT);
#if USE_DEBUG
    vk_draw_debug_lines(fd);
    vk_draw_debug_texts(fd);
#endif

    vk_finish_postprocess_scene();
}

void VKR_LightPoint(const vec3_t origin, vec3_t light)
{
    const refdef_t *fd = vk.fd_valid ? &vk.fd : NULL;

    if (vk_fullbright && vk_fullbright->integer) {
        VectorSet(light, 1.0f, 1.0f, 1.0f);
        return;
    }

    if (!vk_static_light_point(origin, fd, light))
        VectorSet(light, 1.0f, 1.0f, 1.0f);

    vk_add_dynamic_lights(fd, origin, light);

    if (vk_doublelight_entities && vk_doublelight_entities->integer)
        VectorScale(light, vk_entity_light_modulate(), light);

    light[0] = Q_clipf(light[0], 0.0f, 1.0f);
    light[1] = Q_clipf(light[1], 0.0f, 1.0f);
    light[2] = Q_clipf(light[2], 0.0f, 1.0f);
}

void VKR_ClearColor(void)
{
    vk.color.u32 = U32_WHITE;
    vk.alt_color.u32 = U32_WHITE;
    vk.color_set = false;
}

void VKR_SetAlpha(float alpha)
{
    byte a = Q_clipf(alpha, 0.0f, 1.0f) * 255;
    if (!vk.color_set) {
        vk.color.u32 = U32_WHITE;
        vk.alt_color.u32 = U32_WHITE;
        vk.color_set = true;
    }
    vk.color.u8[3] = a;
    vk.alt_color.u8[3] = a;
}

void VKR_SetColor(uint32_t color)
{
    vk.color.u32 = color;
    vk.alt_color.u32 = U32_WHITE;
    vk.alt_color.u8[3] = vk.color.u8[3];
    vk.color_set = true;
}

void VKR_SetClipRect(const clipRect_t *clip)
{
    if (clip) {
        vk.clip.left = max(clip->left, 0);
        vk.clip.top = max(clip->top, 0);
        vk.clip.right = min(clip->right, vk_2d_width());
        vk.clip.bottom = min(clip->bottom, vk_2d_height());
        if (vk.clip.right < vk.clip.left ||
            vk.clip.bottom < vk.clip.top) {
            vk.clip_set = false;
            return;
        }
        vk.clip_set = true;
    } else {
        vk.clip_set = false;
    }
}

float VKR_ClampScale(cvar_t *var)
{
    if (!var)
        return 1.0f;

    if (var->value)
        return 1.0f / Cvar_ClampValue(var, 1.0f, 10.0f);

    int scale = 1;

    if (r_config.height < r_config.width) {
        if (r_config.height >= 2160)
            scale = 4;
        else if (r_config.height >= 1080)
            scale = 2;
    } else {
        if (r_config.width >= 3840)
            scale = 4;
        else if (r_config.width >= 1920)
            scale = 2;
    }

    if (vid && vid->get_dpi_scale)
        scale = max(scale, vid->get_dpi_scale());

    return 1.0f / scale;
}

void VKR_SetScale(float scale)
{
    vk.scale = scale;
}

static int vk_font_shadow(void)
{
    return vk_fontshadow ? Q_clip(vk_fontshadow->integer, 0, 2) : 0;
}

void VKR_DrawChar(int x, int y, int flags, int ch, qhandle_t font)
{
    int old_2d = c.batchesDrawn2D;
    int shadow = vk_font_shadow();

    if ((ch & 127) == 32)
        return;

    if (shadow > 0)
        flags |= UI_DROPSHADOW;

    if (flags & UI_ALTCOLOR)
        ch |= 0x80;
    if (flags & UI_XORCOLOR)
        ch ^= 0x80;

    float s = (ch & 15) * 0.0625f;
    float t = ((ch & 255) >> 4) * 0.0625f;
    color_t saved = vk.color;
    bool saved_set = vk.color_set;
    color_t draw_color = (ch & 0x80) ?
        vk.alt_color : (vk.color_set ? vk.color : (color_t){ .u32 = U32_WHITE });

    if ((flags & UI_DROPSHADOW) && ch != 0x83) {
        byte alpha = draw_color.u8[3];

        vk.color.u32 = MakeColor(0, 0, 0, alpha);
        vk.color_set = true;
        vk_draw_texture_rect(x + 1, y + 1, CONCHAR_WIDTH, CONCHAR_HEIGHT,
                             s, t, s + 0.0625f, t + 0.0625f, font);
        if (shadow > 1) {
            vk_draw_texture_rect(x + 2, y + 2, CONCHAR_WIDTH, CONCHAR_HEIGHT,
                                 s, t, s + 0.0625f, t + 0.0625f, font);
        }

        vk.color = saved;
        vk.color_set = saved_set;
    }

    vk.color = draw_color;
    vk.color_set = true;
    vk_draw_texture_rect(x, y, CONCHAR_WIDTH, CONCHAR_HEIGHT,
                         s, t, s + 0.0625f, t + 0.0625f, font);
    vk.color = saved;
    vk.color_set = saved_set;
    c.charsDrawn2D += c.batchesDrawn2D - old_2d;
}

int VKR_DrawString(int x, int y, int flags, size_t max_chars,
                   const char *string, qhandle_t font)
{
    while (max_chars-- && *string) {
        VKR_DrawChar(x, y, flags, (byte)*string++, font);
        x += CONCHAR_WIDTH;
    }

    return x;
}

bool VKR_GetPicSize(int *w, int *h, qhandle_t pic)
{
    return IMG_GetPicSize(w, h, pic);
}

void VKR_DrawPic(int x, int y, qhandle_t pic)
{
    if (pic <= 0 || pic >= r_numImages)
        return;

    const image_t *image = IMG_ForHandle(pic);
    vk_draw_texture_rect(x, y, image->width, image->height,
                         image->sl, image->tl, image->sh, image->th, pic);
}

void VKR_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic)
{
    if (pic <= 0 || pic >= r_numImages)
        return;

    const image_t *image = IMG_ForHandle(pic);
    vk_draw_texture_rect(x, y, w, h,
                         image->sl, image->tl, image->sh, image->th, pic);
}

void VKR_DrawKeepAspectPic(int x, int y, int w, int h, qhandle_t pic)
{
    if (pic <= 0 || pic >= r_numImages)
        return;

    const image_t *image = IMG_ForHandle(pic);
    if (image->flags & IF_SCRAP) {
        VKR_DrawStretchPic(x, y, w, h, pic);
        return;
    }

    float scale_w = w;
    float scale_h = h * image->aspect;
    float scale = max(scale_w, scale_h);
    float s = (1.0f - scale_w / scale) * 0.5f;
    float t = (1.0f - scale_h / scale) * 0.5f;

    vk_draw_texture_rect(x, y, w, h, s, t, 1.0f - s, 1.0f - t, pic);
}

void VKR_DrawStretchRaw(int x, int y, int w, int h)
{
    color_t saved = vk.color;
    bool saved_set = vk.color_set;

    vk.color.u32 = U32_WHITE;
    vk.color_set = true;
    vk_draw_texture_resource(x, y, w, h, 0, 0, 1, 1, &vk.raw_texture);
    vk.color = saved;
    vk.color_set = saved_set;
}

void VKR_UpdateRawPic(int pic_w, int pic_h, const uint32_t *pic)
{
    if (pic_w <= 0 || pic_h <= 0 || !pic)
        return;

    if (!vk_upload_texture_data(&vk.raw_texture, pic_w, pic_h, pic, false)) {
        Com_WPrintf("Couldn't upload Vulkan raw texture: %s\n",
                    Com_GetLastError());
    } else {
        vk_update_texture_descriptor_with_sampler(&vk.raw_texture,
                                                  vk_sampler_for_pic_flags(IF_NONE));
    }
}

void VKR_TileClear(int x, int y, int w, int h, qhandle_t pic)
{
    const float div64 = 1.0f / 64.0f;
    color_t saved = vk.color;
    bool saved_set = vk.color_set;

    vk.color.u32 = U32_WHITE;
    vk.color_set = true;
    vk_draw_texture_rect(x, y, w, h, x * div64, y * div64,
                         (x + w) * div64, (y + h) * div64, pic);
    vk.color = saved;
    vk.color_set = saved_set;
}

void VKR_DrawFill8(int x, int y, int w, int h, int c)
{
    vk_clear_rect(x, y, w, h, d_8to24table[c & 0xff]);
}

void VKR_DrawFill32(int x, int y, int w, int h, uint32_t color)
{
    vk_clear_rect(x, y, w, h, color);
}

static void vk_recreate_signaled_frame_fence(void)
{
    if (!vk.device || !vk.CreateFence || !vk.DestroyFence)
        return;

    if (vk.frame_fence[vk.frame_index]) {
        vk.DestroyFence(vk.device, vk.frame_fence[vk.frame_index], NULL);
        vk.frame_fence[vk.frame_index] = VK_NULL_HANDLE;
    }

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    VkResult result = vk.CreateFence(vk.device, &fence_info, NULL,
                                     &vk.frame_fence[vk.frame_index]);
    if (result != VK_SUCCESS) {
        vk.frame_fence[vk.frame_index] = VK_NULL_HANDLE;
        Com_EPrintf("vkCreateFence failed while recovering submit failure: Vulkan error %d\n", result);
    }
}

static bool vk_screenshot_format_offsets(int offsets[3])
{
    switch (vk.swapchain_format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
        offsets[0] = 2;
        offsets[1] = 1;
        offsets[2] = 0;
        return true;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
        offsets[0] = 0;
        offsets[1] = 1;
        offsets[2] = 2;
        return true;
    default:
        return false;
    }
}

int VKR_ReadPixels(screenshot_t *s)
{
    if (!vk.swapchain || !vk.swapchain_transfer_src ||
        !vk.swapchain_images || !vk.swapchain_layouts)
        return Q_ERR(ENOTSUP);
    if (vk.frame_active)
        return Q_ERR(EBUSY);
    if (!vk.swapchain_extent.width || !vk.swapchain_extent.height)
        return Q_ERR(EINVAL);

    int offsets[3];
    if (!vk_screenshot_format_offsets(offsets))
        return Q_ERR(ENOTSUP);

    if (vk.swapchain_extent.width > INT_MAX ||
        vk.swapchain_extent.height > INT_MAX)
        return Q_ERR(EOVERFLOW);

    int width = vk.swapchain_extent.width;
    int height = vk.swapchain_extent.height;

    if (width > INT_MAX / 4)
        return Q_ERR(EOVERFLOW);

    int src_rowbytes = width * 4;
    if (height > INT_MAX / src_rowbytes)
        return Q_ERR(EOVERFLOW);

    int src_size = src_rowbytes * height;
    int dst_rowbytes = width * 3;
    if (height > INT_MAX / dst_rowbytes)
        return Q_ERR(EOVERFLOW);

    int dst_size = dst_rowbytes * height;
    vk_buffer_t readback = { 0 };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    void *mapped = NULL;
    int ret = Q_ERR_FAILURE;

    if (!vk_create_buffer(src_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &readback.buffer, &readback.memory))
        return Q_ERR_FAILURE;

    if (vk.DeviceWaitIdle(vk.device) != VK_SUCCESS)
        goto out;

    VkImageLayout old_layout = vk.swapchain_layouts[vk.current_image];
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        ret = Q_ERR(ENOTSUP);
        goto out;
    }
    if (!vk_begin_immediate(&cmd))
        goto out;

    vk_transition_image(cmd, vk.current_image,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copy = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageExtent = {
            .width = width,
            .height = height,
            .depth = 1,
        },
    };
    vk.CmdCopyImageToBuffer(cmd, vk.swapchain_images[vk.current_image],
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            readback.buffer, 1, &copy);

    VkAccessFlags dst_access = 0;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        dst_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    vk_transition_image(cmd, vk.current_image, old_layout, dst_access, dst_stage);

    bool copied = vk_end_immediate(cmd);
    cmd = VK_NULL_HANDLE;
    if (!copied)
        goto out;

    VkResult result = vk.MapMemory(vk.device, readback.memory, 0,
                                   readback.size, 0, &mapped);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkMapMemory", result);
        goto out;
    }

    s->bpp = 3;
    s->rowbytes = dst_rowbytes;
    s->pixels = R_Malloc(dst_size);
    s->width = width;
    s->height = height;

    const byte *src = mapped;
    byte *dst = s->pixels;
    for (int y = 0; y < height; y++) {
        // Screenshot encoders expect OpenGL's bottom-up row order, while a
        // Vulkan image-to-buffer copy returns the swapchain's top row first.
        const byte *src_pixel = src + (height - 1 - y) * src_rowbytes;
        byte *dst_pixel = dst + y * dst_rowbytes;

        for (int x = 0; x < width; x++, src_pixel += 4, dst_pixel += 3) {
            dst_pixel[0] = src_pixel[offsets[0]];
            dst_pixel[1] = src_pixel[offsets[1]];
            dst_pixel[2] = src_pixel[offsets[2]];
        }
    }

    ret = Q_ERR_SUCCESS;

out:
    if (mapped)
        vk.UnmapMemory(vk.device, readback.memory);
    if (cmd)
        vk.FreeCommandBuffers(vk.device, vk.command_pool, 1, &cmd);
    vk_destroy_buffer(&readback);
    return ret;
}

static void vk_begin_render_pass_sized(VkRenderPass render_pass,
                                       VkFramebuffer framebuffer,
                                       VkClearColorValue color,
                                       uint32_t width, uint32_t height)
{
    VkClearValue clear[3] = {
        { .color = color },
        { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } } },
        { .depthStencil = { .depth = 1.0f, .stencil = 0 } },
    };
    uint32_t clear_count;
    if (render_pass == vk.bloom_render_pass) {
        clear_count = 1;
    } else if (vk.mrt_bloom) {
        clear_count = 3;
    } else {
        clear[1] = clear[2];
        clear_count = 2;
    }
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {
            .offset = { 0, 0 },
            .extent = { width, height },
        },
        .clearValueCount = clear_count,
        .pClearValues = clear,
    };

    vk.CmdBeginRenderPass(vk.command_buffers[vk.current_image], &render_pass_info,
                          VK_SUBPASS_CONTENTS_INLINE);
    vk.render_pass_active = true;
}

static void vk_begin_render_pass(VkRenderPass render_pass, VkFramebuffer framebuffer,
                                 VkClearColorValue color)
{
    vk_begin_render_pass_sized(render_pass, framebuffer, color,
                               vk.swapchain_extent.width,
                               vk.swapchain_extent.height);
}

static void vk_render_ssr(void)
{
    if (!vk.frame_ssr || !vk.fd_valid)
        return;

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    if (vk.render_pass_active) {
        vk.CmdEndRenderPass(cmd);
        vk.render_pass_active = false;
    }
    vk_transition_scene(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vk_transition_color_target(cmd, &vk.bloom_source_texture,
                               &vk.bloom_source_layout,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_ACCESS_SHADER_READ_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vk_transition_depth(cmd, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vk_transition_color_target(cmd, &vk.ssr_texture, &vk.ssr_layout,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    const VkClearColorValue black = { .float32 = { 0, 0, 0, 0 } };
    uint32_t width = max(vk.ssr_texture.width, 1);
    uint32_t height = max(vk.ssr_texture.height, 1);
    vk_begin_render_pass_sized(vk.bloom_render_pass, vk.ssr_framebuffer,
                               black, width, height);

    mat4_t proj, view;
    vk_projection_matrix(proj, vk.fd.fov_x, vk.fd.fov_y, vk.fd.rdflags);
    vk_view_matrix(view, &vk.fd);
    vk_ssr_push_t push = {
        .projection = { proj[0], proj[5], proj[10], proj[14] },
        .control = {
            vk_rt_reflections ? Cvar_ClampValue(vk_rt_reflections, 0.0f, 1.0f) : 0.0f,
            vk_rt_debug && vk_rt_debug->integer >= 4 &&
                vk_rt_debug->integer <= 7 ?
                (float)vk_rt_debug->integer : 0.0f,
            1.0f / max((float)vk.scene_texture.width, 1.0f),
            1.0f / max((float)vk.scene_texture.height, 1.0f),
        },
        .view_up = { view[8], view[9], view[10], 0.0f },
    };
    vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.ssr_pipeline);
    vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             vk.ssr_pipeline_layout, 0, 1,
                             &vk.ssr_descriptor_set, 0, NULL);
    vk.CmdPushConstants(cmd, vk.ssr_pipeline_layout,
                        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
    c.trisDrawn += 2;
    vk.CmdEndRenderPass(cmd);
    vk.render_pass_active = false;
    vk_reset_bind_cache();
    vk_transition_color_target(cmd, &vk.ssr_texture, &vk.ssr_layout,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_ACCESS_SHADER_READ_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vk_transition_color_target(cmd, &vk.ssr_resolve_texture,
                               &vk.ssr_resolve_layout,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    width = max(vk.ssr_resolve_texture.width, 1);
    height = max(vk.ssr_resolve_texture.height, 1);
    vk_begin_render_pass_sized(vk.bloom_render_pass,
                               vk.ssr_resolve_framebuffer,
                               black, width, height);
    vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                       vk.ssr_resolve_pipeline);
    vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             vk.ssr_pipeline_layout, 0, 1,
                             &vk.ssr_resolve_descriptor_set, 0, NULL);
    vk.CmdPushConstants(cmd, vk.ssr_pipeline_layout,
                        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
    c.trisDrawn += 2;
    vk.CmdEndRenderPass(cmd);
    vk.render_pass_active = false;
    vk_reset_bind_cache();
    vk_transition_color_target(cmd, &vk.ssr_resolve_texture,
                               &vk.ssr_resolve_layout,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_ACCESS_SHADER_READ_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

static void vk_composite_ssr(void)
{
    if (!vk.frame_ssr)
        return;
    const vec4_t white = { 1, 1, 1, 1 };
    vk_draw_refdef_texture(vk.texture_pipeline,
                          &vk.ssr_resolve_texture, white);
}

static void vk_finish_postprocess_scene(void)
{
    if (!vk.frame_bloom && !vk.frame_waterwarp && !vk.frame_ssr)
        return;

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    bool bloom = vk.frame_bloom;
    bool waterwarp = vk.frame_waterwarp;

    vk_render_ssr();

    if (bloom) {
        const vec4_t white = { 1.0f, 1.0f, 1.0f, 1.0f };
        const VkClearColorValue black = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
        uint32_t bloom_w = max(vk.bloom_texture.width, 1);
        uint32_t bloom_h = max(vk.bloom_texture.height, 1);
        vec4_t downscale_step = {
            1.0f / (float)max(vk.bloom_source_texture.width, 1),
            1.0f / (float)max(vk.bloom_source_texture.height, 1),
            vk_bloom_ghosts ?
                Cvar_ClampValue(vk_bloom_ghosts, 0.0f, 1.0f) : 0.5f,
            vk_bloom_shafts ?
                Cvar_ClampValue(vk_bloom_shafts, 0.0f, 1.0f) : 0.4f,
        };
        float sigma = vk_bloom_sigma ? Cvar_ClampValue(vk_bloom_sigma, 1.0f, 25.0f) : 4.0f;
        sigma *= max((float)vk.fd.height, 1.0f) / 1080.0f;
        // The fixed 9-tap kernel is normalized around sigma ~= 2 texels.
        // OpenGL applies its requested sigma in the quarter-resolution
        // target. Scale the offsets so the screen-space radius stays equal
        // when vk_bloom_downsample changes.
        sigma = max(sigma, 1.0f) * 2.0f /
            (float)vk_bloom_downsample_value();
        int iterations = (vk_showbloom && vk_showbloom->integer >= 2) ? 0 :
            (gl_bloom ? Cvar_ClampInteger(gl_bloom, 1, 8) : 1);
        float streaks = vk_bloom_streaks ?
            Cvar_ClampValue(vk_bloom_streaks, 0.0f, 1.0f) : 0.45f;
        float horizontal_stretch = 1.0f + 3.0f * streaks;
        vec4_t blur_x = {
            sigma * horizontal_stretch / (float)bloom_w,
            0.0f,
            0.0f,
            1.0f,
        };
        vec4_t blur_y = {
            0.0f,
            sigma / (float)bloom_h,
            0.0f,
            1.0f,
        };

        if (vk.render_pass_active && vk.fd_valid) {
            vk_draw_bloom_source_entities(&vk.fd);
            vk_draw_bloom_beams(&vk.fd);
            vk_draw_bloom_only_entities(&vk.fd);
        }
        if (vk.render_pass_active) {
            vk.CmdEndRenderPass(cmd);
            vk.render_pass_active = false;
        }

        vk_transition_scene(cmd,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        vk_transition_color_target(cmd, &vk.bloom_source_texture, &vk.bloom_source_layout,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   VK_ACCESS_SHADER_READ_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        vk_transition_color_target(cmd, &vk.bloom_texture, &vk.bloom_layout,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        vk_begin_render_pass_sized(vk.bloom_render_pass, vk.bloom_framebuffer,
                                   black, bloom_w, bloom_h);
        vk_draw_fullscreen_texture_sized(vk.bloom_downscale_pipeline,
                                         &vk.bloom_source_texture, downscale_step,
                                         bloom_w, bloom_h);
        vk.CmdEndRenderPass(cmd);
        vk.render_pass_active = false;

        for (int i = 0; i < iterations; i++) {
            vk_transition_color_target(cmd, &vk.bloom_texture, &vk.bloom_layout,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                       VK_ACCESS_SHADER_READ_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            vk_transition_color_target(cmd, &vk.blur_texture, &vk.blur_layout,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            vk_begin_render_pass_sized(vk.bloom_render_pass, vk.blur_framebuffer,
                                       black, bloom_w, bloom_h);
            vk_draw_fullscreen_texture_sized(vk.bloom_blur_pipeline,
                                             &vk.bloom_texture, blur_x,
                                             bloom_w, bloom_h);
            vk.CmdEndRenderPass(cmd);
            vk.render_pass_active = false;

            vk_transition_color_target(cmd, &vk.blur_texture, &vk.blur_layout,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                       VK_ACCESS_SHADER_READ_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            vk_transition_color_target(cmd, &vk.bloom_texture, &vk.bloom_layout,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            vk_begin_render_pass_sized(vk.bloom_render_pass, vk.bloom_framebuffer,
                                       black, bloom_w, bloom_h);
            vk_draw_fullscreen_texture_sized(vk.bloom_blur_pipeline,
                                             &vk.blur_texture, blur_y,
                                             bloom_w, bloom_h);
            vk.CmdEndRenderPass(cmd);
            vk.render_pass_active = false;
        }

        vk_transition_color_target(cmd, &vk.bloom_texture, &vk.bloom_layout,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   VK_ACCESS_SHADER_READ_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        vk_transition_color_target(cmd, &vk.bloom_source_texture,
                                   &vk.bloom_source_layout,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        vk_begin_render_pass(vk.render_pass, vk.framebuffers[vk.current_image],
                             vk_frame_clear_color());
        if (vk_showbloom && vk_showbloom->integer) {
            vk_draw_refdef_texture(vk.texture_pipeline, &vk.bloom_texture, white);
        } else {
            vk_composite_scene_texture();
            vk_draw_refdef_texture(vk.bloom_add_pipeline, &vk.bloom_texture, white);
            vk_composite_ssr();
        }
    }

    if (!bloom && (waterwarp || vk.frame_ssr)) {
        if (vk.render_pass_active) {
            vk.CmdEndRenderPass(cmd);
            vk.render_pass_active = false;
        }

        vk_transition_scene(cmd,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        vk_transition_color_target(cmd, &vk.bloom_source_texture,
                                   &vk.bloom_source_layout,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        vk_begin_render_pass(vk.render_pass, vk.framebuffers[vk.current_image],
                             vk_frame_clear_color());
        vk_composite_scene_texture();
        vk_composite_ssr();
    }

    vk.frame_bloom = false;
    vk.frame_waterwarp = false;
    vk.frame_ssr = false;
}

void VKR_BeginFrame(void)
{
    memset(&c, 0, sizeof(c));
    vk.wait_usec = 0;
    vk.acquire_usec = 0;
    vk.record_usec = 0;
    vk.submit_usec = 0;
    vk.present_usec = 0;
    vk.frame_start_usec = 0;

    if (!vk.swapchain) {
        unsigned now = Sys_Milliseconds();

        if (!vk.swapchain_retry_time ||
            (int)(now - vk.swapchain_retry_time) >= 0) {
            vk_recreate_swapchain("deferred retry");
        }
        if (!vk.swapchain)
            return;
    }

    if (gl_bloom && gl_bloom->modified) {
        bool enable_mrt = gl_bloom->integer > 0 ||
            (vk_raytracing && vk_raytracing->integer && vk_rt_reflections &&
             vk_rt_reflections->value > 0.001f);
        gl_bloom->modified = false;
        if (enable_mrt != vk.mrt_bloom &&
            !vk_recreate_swapchain("bloom configuration change"))
            return;
    }
    if (vk_rt_reflections && vk_rt_reflections->modified) {
        bool enable_mrt = (gl_bloom && gl_bloom->integer > 0) ||
            (vk_raytracing && vk_raytracing->integer &&
             vk_rt_reflections->value > 0.001f);
        vk_rt_reflections->modified = false;
        if (enable_mrt != vk.mrt_bloom &&
            !vk_recreate_swapchain("ray-traced reflection change"))
            return;
    }
    if (vk_bloom_downsample && vk_bloom_downsample->modified) {
        vk_bloom_downsample->modified = false;
        if (vk.mrt_bloom &&
            !vk_recreate_swapchain("bloom downsample change"))
            return;
    }

    VkSemaphore image_available = vk.image_available[vk.frame_index];
    VkFence frame_fence = vk.frame_fence[vk.frame_index];

    if (!vk.swapchain || !frame_fence || !image_available || vk.frame_active)
        return;

    uint64_t start = vk_time_usec();
    VkResult result = vk.WaitForFences(vk.device, 1, &frame_fence,
                                       VK_TRUE, UINT64_MAX);
    vk.wait_usec = vk_time_usec() - start;
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkWaitForFences failed: Vulkan error %d\n", result);
        return;
    }
    if (vk.timestamp_query_pool && vk.timestamp_valid[vk.frame_index]) {
        uint64_t ticks[2];
        uint32_t first = vk.frame_index * 2;
        result = vk.GetQueryPoolResults(vk.device, vk.timestamp_query_pool,
                                        first, 2, sizeof(ticks), ticks,
                                        sizeof(ticks[0]),
                                        VK_QUERY_RESULT_64_BIT);
        if (result == VK_SUCCESS && ticks[1] >= ticks[0]) {
            double usec = (double)(ticks[1] - ticks[0]) *
                vk.physical_device_properties.limits.timestampPeriod / 1000.0;
            vk.gpu_frame_usec = min(usec, (double)UINT_MAX);
        }
        vk.timestamp_valid[vk.frame_index] = false;
    }
    vk_read_glare_queries(vk.frame_index);

    if (!vk.image_acquired) {
        start = vk_time_usec();
        result = vk.AcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX,
                                        image_available, VK_NULL_HANDLE,
                                        &vk.current_image);
        vk.acquire_usec = vk_time_usec() - start;
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            vk_recreate_swapchain("image acquisition out of date");
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            Com_EPrintf("vkAcquireNextImageKHR failed: Vulkan error %d\n", result);
            return;
        }
    }
    // The acquired image and its semaphore now belong to this active frame.
    vk.image_acquired = false;

    if (vk.image_fences[vk.current_image]) {
        start = vk_time_usec();
        result = vk.WaitForFences(vk.device, 1,
                                  &vk.image_fences[vk.current_image],
                                  VK_TRUE, UINT64_MAX);
        vk.wait_usec += vk_time_usec() - start;
        if (result != VK_SUCCESS) {
            Com_EPrintf("vkWaitForFences failed: Vulkan error %d\n", result);
            return;
        }
    }
    vk.frame_start_usec = vk_time_usec();
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    result = vk.ResetCommandBuffer(cmd, 0);
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkResetCommandBuffer failed: Vulkan error %d\n", result);
        return;
    }
    vk_reset_bind_cache();

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    result = vk.BeginCommandBuffer(cmd, &begin_info);
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkBeginCommandBuffer failed: Vulkan error %d\n", result);
        return;
    }

    if (vk.glare_query_pool) {
        uint32_t first = vk.frame_index * MAX_GLARE_SOURCES;
        vk.CmdResetQueryPool(cmd, vk.glare_query_pool, first,
                             MAX_GLARE_SOURCES);
    }
    if (vk.timestamp_query_pool) {
        uint32_t first = vk.frame_index * 2;
        vk.CmdResetQueryPool(cmd, vk.timestamp_query_pool, first, 2);
        vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             vk.timestamp_query_pool, first);
    }

    vk_transition_image(cmd, vk.current_image,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    vk.frame_bloom = vk_bloom_enabled_for_frame();
    vk.frame_waterwarp = vk_waterwarp_enabled_for_frame();
    vk.frame_ssr = vk_ssr_enabled_for_frame();
    bool postprocess = vk.frame_bloom || vk.frame_waterwarp || vk.frame_ssr;

    // The postprocess target is selected from the previous refdef before the
    // client submits this frame. Do not otherwise reuse that refdef: menu-only
    // frames after a disconnect must not redraw stale bloom entities or blend.
    vk.fd_valid = false;

    if (postprocess) {
        vk_transition_scene(cmd,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        if (vk.mrt_bloom) {
            vk_transition_color_target(cmd, &vk.bloom_source_texture,
                                       &vk.bloom_source_layout,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        }
    }

    vk_transition_depth(cmd, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

    VkClearValue clear[3] = {
        { .color = vk_frame_clear_color() },
        { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } } },
        { .depthStencil = { .depth = 1.0f, .stencil = 0 } },
    };
    uint32_t clear_count = vk.mrt_bloom ? 3 : 2;
    if (!vk.mrt_bloom)
        clear[1] = clear[2];
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk.render_pass,
        .framebuffer = postprocess ? vk.scene_framebuffer : vk.framebuffers[vk.current_image],
        .renderArea = {
            .offset = { 0, 0 },
            .extent = vk.swapchain_extent,
        },
        .clearValueCount = clear_count,
        .pClearValues = clear,
    };

    vk.CmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vk.render_pass_active = true;


    vk.frame_active = true;
}

static void vk_record_pixel_lightmap_update(VkCommandBuffer cmd)
{
    if (!vk.world.pixel_lightmap_update_pending ||
        !vk.world.pixel_lightmap_texture.image ||
        !vk.world.pixel_lightmap_staging ||
        !vk.world.pixel_lightmap_copies ||
        !vk.world.pixel_lightmap_copy_count ||
        vk.current_image >= vk.world.pixel_lightmap_staging_count)
        return;

    vk_buffer_t *staging =
        &vk.world.pixel_lightmap_staging[vk.current_image];
    if (!staging->buffer)
        return;

    vk_texture_barrier(cmd, vk.world.pixel_lightmap_texture.image,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_ACCESS_SHADER_READ_BIT,
                       VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);

    vk.CmdCopyBufferToImage(cmd, staging->buffer,
                            vk.world.pixel_lightmap_texture.image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            vk.world.pixel_lightmap_copy_count,
                            vk.world.pixel_lightmap_copies);

    vk_texture_barrier(cmd, vk.world.pixel_lightmap_texture.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_ACCESS_SHADER_READ_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vk.world.pixel_lightmap_update_pending = false;
    vk.world.pixel_lightmap_copy_count = 0;
}

void VKR_EndFrame(void)
{
    if (!vk.frame_active)
        return;

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    bool bloom = vk.frame_bloom;
    bool waterwarp = vk.frame_waterwarp;

    if (bloom) {
        const vec4_t white = { 1.0f, 1.0f, 1.0f, 1.0f };
        const VkClearColorValue black = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
        uint32_t bloom_w = max(vk.bloom_texture.width, 1);
        uint32_t bloom_h = max(vk.bloom_texture.height, 1);
        vec4_t downscale_step = {
            1.0f / (float)max(vk.bloom_source_texture.width, 1),
            1.0f / (float)max(vk.bloom_source_texture.height, 1),
            vk_bloom_ghosts ?
                Cvar_ClampValue(vk_bloom_ghosts, 0.0f, 1.0f) : 0.5f,
            vk_bloom_shafts ?
                Cvar_ClampValue(vk_bloom_shafts, 0.0f, 1.0f) : 0.4f,
        };
        float sigma = vk_bloom_sigma ? Cvar_ClampValue(vk_bloom_sigma, 1.0f, 25.0f) : 4.0f;
        sigma *= max((float)vk.fd.height, 1.0f) / 1080.0f;
        sigma = max(sigma, 1.0f) * 2.0f /
            (float)vk_bloom_downsample_value();
        int iterations = (vk_showbloom && vk_showbloom->integer >= 2) ? 0 :
            (gl_bloom ? Cvar_ClampInteger(gl_bloom, 1, 8) : 1);
        float streaks = vk_bloom_streaks ?
            Cvar_ClampValue(vk_bloom_streaks, 0.0f, 1.0f) : 0.45f;
        float horizontal_stretch = 1.0f + 3.0f * streaks;
        vec4_t blur_x = {
            sigma * horizontal_stretch / (float)bloom_w,
            0.0f,
            0.0f,
            1.0f,
        };
        vec4_t blur_y = {
            0.0f,
            sigma / (float)bloom_h,
            0.0f,
            1.0f,
        };

        if (vk.render_pass_active) {
            vk.CmdEndRenderPass(cmd);
            vk.render_pass_active = false;
        }

        vk_transition_scene(cmd,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        vk_transition_color_target(cmd, &vk.bloom_source_texture, &vk.bloom_source_layout,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   VK_ACCESS_SHADER_READ_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        vk_transition_color_target(cmd, &vk.bloom_texture, &vk.bloom_layout,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        vk_begin_render_pass_sized(vk.bloom_render_pass, vk.bloom_framebuffer,
                                   black, bloom_w, bloom_h);
        vk_draw_fullscreen_texture_sized(vk.bloom_downscale_pipeline,
                                         &vk.bloom_source_texture, downscale_step,
                                         bloom_w, bloom_h);
        vk.CmdEndRenderPass(cmd);
        vk.render_pass_active = false;

        for (int i = 0; i < iterations; i++) {
            vk_transition_color_target(cmd, &vk.bloom_texture, &vk.bloom_layout,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                       VK_ACCESS_SHADER_READ_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            vk_transition_color_target(cmd, &vk.blur_texture, &vk.blur_layout,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            vk_begin_render_pass_sized(vk.bloom_render_pass, vk.blur_framebuffer,
                                       black, bloom_w, bloom_h);
            vk_draw_fullscreen_texture_sized(vk.bloom_blur_pipeline,
                                             &vk.bloom_texture, blur_x,
                                             bloom_w, bloom_h);
            vk.CmdEndRenderPass(cmd);
            vk.render_pass_active = false;

            vk_transition_color_target(cmd, &vk.blur_texture, &vk.blur_layout,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                       VK_ACCESS_SHADER_READ_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            vk_transition_color_target(cmd, &vk.bloom_texture, &vk.bloom_layout,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            vk_begin_render_pass_sized(vk.bloom_render_pass, vk.bloom_framebuffer,
                                       black, bloom_w, bloom_h);
            vk_draw_fullscreen_texture_sized(vk.bloom_blur_pipeline,
                                             &vk.blur_texture, blur_y,
                                             bloom_w, bloom_h);
            vk.CmdEndRenderPass(cmd);
            vk.render_pass_active = false;
        }

        vk_transition_color_target(cmd, &vk.bloom_texture, &vk.bloom_layout,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   VK_ACCESS_SHADER_READ_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        vk_begin_render_pass(vk.render_pass, vk.framebuffers[vk.current_image],
                             vk_frame_clear_color());
        if (vk_showbloom && vk_showbloom->integer) {
            vk_draw_refdef_texture(vk.texture_pipeline, &vk.bloom_texture, white);
        } else {
            vk_composite_scene_texture();
            vk_draw_refdef_texture(vk.bloom_add_pipeline, &vk.bloom_texture, white);
        }
    }

    if (!bloom && waterwarp) {
        if (vk.render_pass_active) {
            vk.CmdEndRenderPass(cmd);
            vk.render_pass_active = false;
        }

        vk_transition_scene(cmd,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        vk_begin_render_pass(vk.render_pass, vk.framebuffers[vk.current_image],
                             vk_frame_clear_color());
        vk_composite_scene_texture();
    }

    if (vk.fd_valid)
        vk_draw_polyblend(&vk.fd);

#if USE_DEBUG
    if (vk_showstats && vk_showstats->integer)
        vk_draw_stats();
#endif

    vk_draw_tearing();

    if (vk.render_pass_active) {
        vk.CmdEndRenderPass(cmd);
        vk.render_pass_active = false;
    }

    vk_record_pixel_lightmap_update(cmd);

    if (vk.timestamp_query_pool) {
        vk.CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             vk.timestamp_query_pool, vk.frame_index * 2 + 1);
    }

    vk_transition_image(cmd, vk.current_image,
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        0,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    uint64_t start = vk_time_usec();
    VkResult result = vk.EndCommandBuffer(cmd);
    vk.record_usec = vk_time_usec() - vk.frame_start_usec;
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkEndCommandBuffer failed: Vulkan error %d\n", result);
        vk.frame_active = false;
        return;
    }

    VkSemaphore image_available = vk.image_available[vk.frame_index];
    VkSemaphore render_finished = vk.render_finished[vk.frame_index];
    VkFence frame_fence = vk.frame_fence[vk.frame_index];

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &image_available,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &render_finished,
    };

    result = vk.ResetFences(vk.device, 1, &frame_fence);
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkResetFences failed: Vulkan error %d\n", result);
        vk.frame_active = false;
        return;
    }

    start = vk_time_usec();
    result = vk.QueueSubmit(vk.graphics_queue, 1, &submit_info, frame_fence);
    vk.submit_usec = vk_time_usec() - start;
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkQueueSubmit failed: Vulkan error %d\n", result);
        vk_recreate_signaled_frame_fence();
        vk.frame_active = false;
        return;
    }
    vk.image_fences[vk.current_image] = frame_fence;
    if (vk.timestamp_query_pool)
        vk.timestamp_valid[vk.frame_index] = true;

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &render_finished,
        .swapchainCount = 1,
        .pSwapchains = &vk.swapchain,
        .pImageIndices = &vk.current_image,
    };

    start = vk_time_usec();
    result = vk.QueuePresentKHR(vk.present_queue, &present_info);
    vk.present_usec = vk_time_usec() - start;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        vk_recreate_swapchain(result == VK_ERROR_OUT_OF_DATE_KHR ?
                              "presentation out of date" :
                              "presentation suboptimal");
    } else if (result != VK_SUCCESS) {
        Com_EPrintf("vkQueuePresentKHR failed: Vulkan error %d\n", result);
    }

    if (vk_finish && vk_finish->integer)
        vk.DeviceWaitIdle(vk.device);

    vk_log_perf_stats();

    vk.frame_active = false;
    vk.frame_index = (vk.frame_index + 1) % vk_frames_in_flight_value();
}

void VKR_ModeChanged(int width, int height, int flags)
{
    r_config.width = width;
    r_config.height = height;
    r_config.flags = flags | QVF_VIDEOSYNC;

    if (!vk.device)
        return;

    vk_recreate_swapchain("video mode change");
}

bool VKR_VideoSync(void)
{
    VkFence frame_fence = vk.frame_fence[vk.frame_index];
    VkSemaphore image_available = vk.image_available[vk.frame_index];

    if (!vk.swapchain || !frame_fence || !image_available || vk.frame_active)
        return true;

    // In GPU-synchronized mode, do not commit the client to a render frame
    // until both its frame slot and a presentable swapchain image are ready.
    // Mailbox can otherwise block vkAcquireNextImageKHR for several
    // milliseconds after simulation and input have already advanced.
    VkResult result = vk.WaitForFences(vk.device, 1, &frame_fence,
                                       VK_TRUE, 0);
    if (result == VK_TIMEOUT)
        return false;
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkWaitForFences failed: Vulkan error %d\n", result);
        return false;
    }

    if (!vk.image_acquired) {
        result = vk.AcquireNextImageKHR(vk.device, vk.swapchain, 0,
                                        image_available, VK_NULL_HANDLE,
                                        &vk.current_image);
        if (result == VK_NOT_READY || result == VK_TIMEOUT)
            return false;
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            vk_recreate_swapchain("video sync acquisition out of date");
            return false;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            Com_EPrintf("vkAcquireNextImageKHR failed: Vulkan error %d\n",
                        result);
            return false;
        }
        vk.image_acquired = true;
    }

    VkFence image_fence = vk.image_fences[vk.current_image];
    if (!image_fence)
        return true;
    result = vk.WaitForFences(vk.device, 1, &image_fence, VK_TRUE, 0);
    if (result == VK_TIMEOUT)
        return false;
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkWaitForFences failed: Vulkan error %d\n", result);
        return false;
    }
    return true;
}

r_opengl_config_t VKR_GetGLConfig(void)
{
    return (r_opengl_config_t){ 0 };
}

#endif
