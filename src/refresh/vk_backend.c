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

static const uint32_t vk_waterwarp_frag_spv[] =
#include "vk_waterwarp_frag_spv.h"
;

static const uint32_t vk_bloom_blur_frag_spv[] =
#include "vk_bloom_blur_frag_spv.h"
;

static const uint32_t vk_bloom_downscale_frag_spv[] =
#include "vk_bloom_downscale_frag_spv.h"
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

static const uint32_t vk_world_frag_spv[] =
#include "vk_world_frag_spv.h"
;

static const uint32_t vk_world_alpha_frag_spv[] =
#include "vk_world_alpha_frag_spv.h"
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

static const uint32_t vk_world_glow_frag_spv[] =
#include "vk_world_glow_frag_spv.h"
;

static const uint32_t vk_alias_vert_spv[] =
#include "vk_alias_vert_spv.h"
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

typedef struct {
    mface_t *face;
    uint32_t first_vertex;
    uint32_t edge_count;
    uint32_t first_index;
    uint32_t index_count;
    vec3_t center;
    uint16_t pixel_lm_x, pixel_lm_y;
    uint16_t pixel_lm_w, pixel_lm_h;
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
    int skin_count;
    uint32_t vertex_count;
} vk_model_t;

typedef struct {
    bsp_t *cache;
    vk_mesh_t mesh;
    vk_buffer_t pixel_lmuv_buffer;
    vk_texture_t pixel_lightmap_texture;
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
    cplane_t frustum[4];
    vec3_t vieworg;
    int viewcluster;
    int nolm_mask;
    unsigned drawframe;
    unsigned visframe;
    float lightstyles[MAX_LIGHTSTYLES];
    bool lightstyles_valid;
    bool sky_visible;
} vk_world_t;

typedef struct {
    float rect[4];
    float color[4];
    float screen[2];
} vk_rect_push_t;

typedef struct {
    mat4_t mvp;
    float color[4];
} vk_color3d_push_t;

typedef struct {
    mat4_t mvp;
    float color[4];
    float scroll[4];
    float dlight[4];
    float fog[4];
    float intensity;
} vk_world_push_t;

typedef struct {
    mat4_t mvp;
    float color[4];
    float scroll[4];
    float dlight[4];
    float fog[4];
    float intensity;
    float _pad;
    float lm_scale[2];
    float lm_offset[2];
} vk_world_pixel_push_t;

typedef char vk_world_pixel_lm_scale_offset_check[
    offsetof(vk_world_pixel_push_t, lm_scale) == 136 ? 1 : -1];
typedef char vk_world_pixel_lm_offset_offset_check[
    offsetof(vk_world_pixel_push_t, lm_offset) == 144 ? 1 : -1];

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
} vk_alias_push_t;

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
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;

    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkCreateDevice CreateDevice;
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures;
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

    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties physical_device_properties;
    VkPhysicalDeviceFeatures physical_device_features;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkCommandPool command_pool;
    VkDescriptorSetLayout texture_set_layout;
    VkDescriptorPool descriptor_pool;
    VkSampler sampler;
    VkSampler sky_sampler;
    VkSampler postprocess_sampler;
    VkSampler nearest_sampler;
    VkSampler sky_nearest_sampler;
    VkPipelineLayout rect_pipeline_layout;
    VkPipelineLayout pixel_world_pipeline_layout;
    VkPipeline rect_pipeline;
    VkPipeline vignette_pipeline;
    VkPipeline texture_pipeline;
    VkPipeline waterwarp_pipeline;
    VkPipeline bloom_downscale_pipeline;
    VkPipeline bloom_blur_pipeline;
    VkPipeline bloom_add_pipeline;
    VkPipeline color3d_pipeline;
    VkPipeline line3d_pipeline;
    VkPipeline debug_line_pipeline;
    VkPipeline debug_text_pipeline;
    VkPipeline beam_pipeline;
    VkPipeline world_pipeline;
    VkPipeline world_alpha_pipeline;
    VkPipeline world_glow_pipeline;
    VkPipeline pixel_world_pipeline;
    VkPipeline pixel_world_alpha_pipeline;
    VkPipeline sky_pipeline;
    VkPipeline sprite_pipeline;
    VkPipeline sprite_alpha_pipeline;
    VkPipeline particle_add_pipeline;
    VkPipeline glare_pipeline;
    VkPipeline alias_pipeline;
    VkPipeline alias_alpha_pipeline;
    VkPipeline alias_depth_pipeline;
    VkPipeline alias_blend_pipeline;
    VkPipeline alias_shadow_pipeline;
    VkPipeline alias_line_pipeline;
    VkSwapchainKHR swapchain;
    VkRenderPass render_pass;
    VkRenderPass bloom_render_pass;
    VkFormat swapchain_format;
    VkFormat depth_format;
    VkExtent2D swapchain_extent;
    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;
    VkImage *swapchain_images;
    VkImageView *swapchain_views;
    VkFramebuffer *framebuffers;
    VkPresentModeKHR present_mode;
    VkFence *image_fences;
    VkFramebuffer scene_framebuffer;
    VkFramebuffer bloom_source_framebuffer;
    VkFramebuffer bloom_framebuffer;
    VkFramebuffer blur_framebuffer;
    VkImageLayout *swapchain_layouts;
    VkImageLayout scene_layout;
    VkImageLayout bloom_source_layout;
    VkImageLayout bloom_layout;
    VkImageLayout blur_layout;
    VkCommandBuffer *command_buffers;
    uint32_t swapchain_image_count;
    bool swapchain_transfer_src;
    vk_queue_families_t queues;
    VkSemaphore image_available[VK_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore render_finished[VK_MAX_FRAMES_IN_FLIGHT];
    VkFence frame_fence[VK_MAX_FRAMES_IN_FLIGHT];
    uint32_t frame_index;
    uint32_t current_image;
    bool frame_active;
    bool render_pass_active;
    bool drawing_bloom;
    vk_draw_scope_t draw_scope;
    bool frame_bloom;
    bool frame_waterwarp;
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
    vk_texture_t particle_texture;
    vk_texture_t beam_texture;
    vk_mesh_t test_triangle;
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
    float sky_rotate;
    bool sky_autorotate;
    vec3_t sky_axis;
    refdef_t fd;
    bool fd_valid;
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
    VkPipeline bound_pipeline;
    VkDescriptorSet bound_texture_descriptor;
    VkDescriptorSet bound_pixel_world_descriptors[2];
} vk_state_t;

static vk_state_t vk;
static cvar_t *vk_show_test_triangle;
static cvar_t *vk_gl_test;
static cvar_t *vk_drawentities;
static cvar_t *vk_gl_drawentities;
static cvar_t *vk_drawsky;
static cvar_t *vk_gl_drawsky;
static cvar_t *vk_swapinterval;
static cvar_t *vk_finish;
static cvar_t *vk_texturemode;
static cvar_t *vk_anisotropy;
static cvar_t *vk_round_down;
static cvar_t *vk_picmip;
static cvar_t *vk_downsample_skins;
static cvar_t *vk_bilerp_chars;
static cvar_t *vk_bilerp_pics;
static cvar_t *vk_bilerp_skies;
static cvar_t *vk_saturation;
static cvar_t *vk_invert;
static cvar_t *vk_gamma;
static cvar_t *vk_gamma_scale_pics;
static cvar_t *vk_partscale;
static cvar_t *vk_partstyle;
static cvar_t *vk_partshape;
static cvar_t *vk_beamstyle;
static cvar_t *vk_flarespeed;
static cvar_t *vk_lightgrid;
static cvar_t *vk_gl_lightgrid;
static cvar_t *vk_fullbright;
static cvar_t *vk_cull_models;
static cvar_t *vk_shadows;
static cvar_t *vk_stencilbits;
static cvar_t *vk_dotshading;
static cvar_t *vk_draworder;
static cvar_t *vk_showorigins;
static cvar_t *vk_showtearing;
static cvar_t *vk_showbloom;
static cvar_t *vk_waterwarp;
static cvar_t *vk_bloom_sigma;
static cvar_t *vk_bloom_downsample;
static cvar_t *vk_glare;
static cvar_t *vk_glare_threshold;
static cvar_t *vk_glare_size;
static cvar_t *vk_glare_intensity;
static cvar_t *vk_perf_stats;
static cvar_t *vk_frames_in_flight;
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
static cvar_t *vk_world_textures;
static cvar_t *vk_world_vis;
static cvar_t *vk_cull_nodes;
static cvar_t *vk_world_cull;
#if USE_DEBUG
static cvar_t *vk_debug_distfrac;
#endif
static byte vk_gammatable[256];
static bool vk_pixel_lightmaps_warned;
static bool vk_pixel_lightmaps_draw_logged;
static bool vk_pixel_lightmaps_scope_logged;
static bool vk_pixel_lightmaps_atlas_logged;
static bool vk_pixel_lightmaps_lmuv_logged;

static bool vk_upload_texture(image_t *image, byte *pic);
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
static bool vk_create_particle_texture(void);
static bool vk_create_beam_texture(void);
static uint32_t vk_frames_in_flight_value(void);
static uint32_t vk_bloom_downsample_value(void);
static void vk_entity_axis(const entity_t *ent, vec3_t axis[3]);
static void vk_entity_mvp(mat4_t out, const refdef_t *fd,
                          const entity_t *ent, const vec3_t axis[3]);
static bool vk_create_swapchain(int width, int height);
static bool vk_recreate_swapchain(void);
static bool vk_create_test_triangle(void);
static void vk_destroy_mesh(vk_mesh_t *mesh);
static void vk_free_world(void);
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
    float saturation = vk_saturation ?
        Cvar_ClampValue(vk_saturation, 0.0f, 1.0f) : 1.0f;
    bool world = type == IT_WALL && !(flags & IF_TURBULENT);
    bool invert = world && vk_invert && vk_invert->integer;
    bool scale_pics = !(r_config.flags & QVF_GAMMARAMP) &&
        vk_gamma_scale_pics && vk_gamma_scale_pics->integer &&
        type != IT_WALL && type != IT_SKIN;

    if (!world && !scale_pics)
        return;
    if (world && saturation == 1.0f && !invert && !scale_pics)
        return;

    byte *p = pic;
    int count = width * height;
    for (int i = 0; i < count; i++, p += 4) {
        float r = p[0];
        float g = p[1];
        float b = p[2];

        if (world) {
            float y = LUMINANCE(r, g, b);
            r = y + (r - y) * saturation;
            g = y + (g - y) * saturation;
            b = y + (b - y) * saturation;
        }

        p[0] = invert ? 255 - Q_clipf(r, 0.0f, 255.0f) :
            Q_clipf(r, 0.0f, 255.0f);
        p[1] = invert ? 255 - Q_clipf(g, 0.0f, 255.0f) :
            Q_clipf(g, 0.0f, 255.0f);
        p[2] = invert ? 255 - Q_clipf(b, 0.0f, 255.0f) :
            Q_clipf(b, 0.0f, 255.0f);

        if (scale_pics) {
            p[0] = vk_gammatable[p[0]];
            p[1] = vk_gammatable[p[1]];
            p[2] = vk_gammatable[p[2]];
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

    gamma = 1.0f / gamma;
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
    if (!vk_upload_texture(image, pic)) {
        Com_WPrintf("Couldn't upload Vulkan texture %s: %s\n",
                    image->name, Com_GetLastError());
    }
}

static void vk_unload_image(image_t *image)
{
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
    vk_destroy_mesh(&vk.world.mesh);
    vk_destroy_buffer(&vk.world.pixel_lmuv_buffer);
    vk_destroy_texture_resource(&vk.world.pixel_lightmap_texture);
    vk_pixel_lightmaps_atlas_logged = false;
    vk_pixel_lightmaps_lmuv_logged = false;
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
        vec3_t scale, translate;

        LittleVector(src_frame->scale, scale);
        LittleVector(src_frame->translate, translate);
        ClearBounds(alias_frames[frame].bounds[0], alias_frames[frame].bounds[1]);

        for (uint32_t i = 0; i < numindices; i++) {
            if (remap[i] != i)
                continue;

            const dmd2trivertx_t *src_vert = &src_frame->verts[vert_indices[i]];
            vk_vertex_t *dst = &vertices[frame * numverts + final_indices[i]];

            dst->position[0] = src_vert->v[0] * scale[0] + translate[0];
            dst->position[1] = src_vert->v[1] * scale[1] + translate[1];
            dst->position[2] = src_vert->v[2] * scale[2] + translate[2];
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

        alias_frames[frame].radius = RadiusFromBounds(alias_frames[frame].bounds[0],
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
    if (model->skin_count)
        model->skins = Z_Mallocz(sizeof(model->skins[0]) * model->skin_count);

    const char *src_skin = (const char *)rawdata + header.ofs_skins;
    for (int i = 0; i < model->skin_count; i++) {
        char skin_name[MD2_MAX_SKINNAME];

        if (!Q_memccpy(skin_name, src_skin, 0, sizeof(skin_name)))
            model->skins[i] = R_NOTEXTURE;
        else
            model->skins[i] = IMG_Find(skin_name, IT_SKIN, IF_NONE);
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
    if (skin_count)
        skins = Z_Mallocz(sizeof(*skins) * skin_count);

    const dmd3frame_t *src_frame =
        (const dmd3frame_t *)(rawdata + header.ofs_frames);
    for (uint32_t frame = 0; frame < header.num_frames; frame++) {
        vec3_t mins, maxs, translate;

        LittleVector(src_frame[frame].mins, mins);
        LittleVector(src_frame[frame].maxs, maxs);
        LittleVector(src_frame[frame].translate, translate);
        VectorScale(mins, MD3_XYZ_SCALE, alias_frames[frame].bounds[0]);
        VectorScale(maxs, MD3_XYZ_SCALE, alias_frames[frame].bounds[1]);
        VectorAdd(alias_frames[frame].bounds[0], translate,
                  alias_frames[frame].bounds[0]);
        VectorAdd(alias_frames[frame].bounds[1], translate,
                  alias_frames[frame].bounds[1]);
        alias_frames[frame].radius =
            RadiusFromBounds(alias_frames[frame].bounds[0],
                             alias_frames[frame].bounds[1]);
    }

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
            else
                skins[info->skin_start + skin] =
                    IMG_Find(skin_name, IT_SKIN, IF_NONE);
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

                dst->position[0] =
                    (int16_t)LittleShort(src_vert[vert].point[0]) * MD3_XYZ_SCALE +
                    translate[0];
                dst->position[1] =
                    (int16_t)LittleShort(src_vert[vert].point[1]) * MD3_XYZ_SCALE +
                    translate[1];
                dst->position[2] =
                    (int16_t)LittleShort(src_vert[vert].point[2]) * MD3_XYZ_SCALE +
                    translate[2];
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

    if (!vk_upload_buffer(&uploaded.vertices, vertices,
                          sizeof(*vertices) * vertex_count,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
        goto fail;
    if (!vk_upload_buffer(&uploaded.indices, indices,
                          sizeof(*indices) * index_count,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
        goto fail;

    uploaded.index_count = index_count;
    vk_destroy_mesh(mesh);
    *mesh = uploaded;
    return true;

fail:
    vk_destroy_mesh(&uploaded);
    return false;
}

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
                                   uint32_t height, VkFormat format)
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
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
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

    return vk.sampler;
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
    vk_update_texture_descriptor(&vk.beam_texture);
    vk_update_texture_descriptor(&vk.world.pixel_lightmap_texture);
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

    if (scaled_width != width || scaled_height != height) {
        scaled = FS_AllocTempMem(scaled_width * scaled_height * 4);
        vk_resample_texture(pic, width, height, scaled, scaled_width, scaled_height);
    }

    image->upload_width = scaled_width;
    image->upload_height = scaled_height;

    bool mipmaps = image->type == IT_WALL || image->type == IT_SKIN;
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

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = PRODUCT,
        .applicationVersion = VK_MAKE_VERSION(REVISION, 0, 0),
        .pEngineName = PRODUCT,
        .engineVersion = VK_MAKE_VERSION(REVISION, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
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

    for (uint32_t i = 0; i < count; i++) {
        vk_queue_families_t queues = vk_find_queues(devices[i]);
        if (queues.has_graphics && queues.has_present && vk_has_swapchain_extension(devices[i])) {
            vk.physical_device = devices[i];
            vk.queues = queues;
            break;
        }
    }

    Z_Free(devices);

    if (!vk.physical_device) {
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
}

static bool vk_create_device(void)
{
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_infos[2];
    uint32_t queue_info_count = 0;
    const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceFeatures features = { 0 };

    queue_infos[queue_info_count++] = (VkDeviceQueueCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.queues.graphics_family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };

    if (vk.queues.present_family != vk.queues.graphics_family) {
        queue_infos[queue_info_count++] = (VkDeviceQueueCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vk.queues.present_family,
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
    }

    if (vk.physical_device_features.samplerAnisotropy)
        features.samplerAnisotropy = VK_TRUE;
    if (vk.physical_device_features.fillModeNonSolid)
        features.fillModeNonSolid = VK_TRUE;
    if (vk.physical_device_features.wideLines)
        features.wideLines = VK_TRUE;
    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queue_info_count,
        .pQueueCreateInfos = queue_infos,
        .enabledExtensionCount = q_countof(extensions),
        .ppEnabledExtensionNames = extensions,
        .pEnabledFeatures = &features,
    };

    VkResult result = vk.CreateDevice(vk.physical_device, &create_info, NULL, &vk.device);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateDevice", result);

    if (!vk_load_device())
        return false;

    vk.GetDeviceQueue(vk.device, vk.queues.graphics_family, 0, &vk.graphics_queue);
    vk.GetDeviceQueue(vk.device, vk.queues.present_family, 0, &vk.present_queue);
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
        .maxLod = 0.0f,
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
        .maxLod = 0.0f,
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

    vk_recreate_swapchain();
}

static void vk_drawsky_changed(cvar_t *self)
{
    extern void CL_SetSky(void);

    (void)self;

    CL_SetSky();
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

    const uint32_t texture_descriptor_count = MAX_RIMAGES * 2 + 9;
    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = texture_descriptor_count,
    };
    VkDescriptorPoolCreateInfo pool_info_desc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = texture_descriptor_count,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    result = vk.CreateDescriptorPool(vk.device, &pool_info_desc,
                                     NULL, &vk.descriptor_pool);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateDescriptorPool", result);

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
        .size = max(max(max(sizeof(vk_draw_push_t), sizeof(vk_color3d_push_t)),
                        sizeof(vk_world_push_t)), sizeof(vk_alias_push_t)),
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &vk.texture_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    result = vk.CreatePipelineLayout(vk.device, &layout_info,
                                     NULL, &vk.rect_pipeline_layout);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreatePipelineLayout", result);

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
    if (!vk_swapinterval || vk_swapinterval->integer)
        return VK_PRESENT_MODE_FIFO_KHR;

    for (uint32_t i = 0; i < count; i++) {
        if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
            return modes[i];
    }

    for (uint32_t i = 0; i < count; i++) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            return modes[i];
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

    if (vk.world_pipeline) {
        vk.DestroyPipeline(vk.device, vk.world_pipeline, NULL);
        vk.world_pipeline = VK_NULL_HANDLE;
    }

    if (vk.world_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.world_alpha_pipeline, NULL);
        vk.world_alpha_pipeline = VK_NULL_HANDLE;
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

    if (vk.particle_add_pipeline) {
        vk.DestroyPipeline(vk.device, vk.particle_add_pipeline, NULL);
        vk.particle_add_pipeline = VK_NULL_HANDLE;
    }

    if (vk.glare_pipeline) {
        vk.DestroyPipeline(vk.device, vk.glare_pipeline, NULL);
        vk.glare_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_pipeline, NULL);
        vk.alias_pipeline = VK_NULL_HANDLE;
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

    if (vk.bloom_source_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.bloom_source_framebuffer, NULL);
        vk.bloom_source_framebuffer = VK_NULL_HANDLE;
    }

    if (vk.bloom_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.bloom_framebuffer, NULL);
        vk.bloom_framebuffer = VK_NULL_HANDLE;
    }

    if (vk.blur_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.blur_framebuffer, NULL);
        vk.blur_framebuffer = VK_NULL_HANDLE;
    }

    vk_destroy_texture_resource(&vk.scene_texture);
    vk_destroy_texture_resource(&vk.bloom_source_texture);
    vk_destroy_texture_resource(&vk.bloom_texture);
    vk_destroy_texture_resource(&vk.blur_texture);
    vk.scene_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.bloom_source_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.bloom_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.blur_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vk.depth_view) {
        vk.DestroyImageView(vk.device, vk.depth_view, NULL);
        vk.depth_view = VK_NULL_HANDLE;
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

static bool vk_recreate_swapchain(void)
{
    if (!vk.device)
        return false;

    int width = r_config.width;
    int height = r_config.height;

    vk_destroy_swapchain();
    if (!vk_create_swapchain(width, height)) {
        Com_EPrintf("Couldn't recreate Vulkan swapchain: %s\n", Com_GetLastError());
        vk_destroy_swapchain();
        return false;
    }

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
    VkAttachmentDescription attachments[] = {
        {
            .format = vk.swapchain_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = vk.depth_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = vk_shadow_stencil_enabled() ?
                VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };

    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference depth_ref = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = &depth_ref,
    };

    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = q_countof(attachments),
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };

    VkResult result = vk.CreateRenderPass(vk.device, &create_info, NULL, &vk.render_pass);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateRenderPass", result);

    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    result = vk.CreateRenderPass(vk.device, &create_info, NULL, &vk.bloom_render_pass);
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
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
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

    return true;
}

static bool vk_create_framebuffers(void)
{
    vk.framebuffers = Z_Mallocz(sizeof(*vk.framebuffers) * vk.swapchain_image_count);

    for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
        VkImageView attachments[] = { vk.swapchain_views[i], vk.depth_view };
        VkFramebufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vk.render_pass,
            .attachmentCount = q_countof(attachments),
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
        VkFramebuffer *framebuffer;
        VkImageLayout *layout;
        VkRenderPass render_pass;
        uint32_t width;
        uint32_t height;
    } targets[] = {
        { &vk.scene_texture, &vk.scene_framebuffer, &vk.scene_layout, vk.render_pass,
          vk.swapchain_extent.width, vk.swapchain_extent.height },
        { &vk.bloom_source_texture, &vk.bloom_source_framebuffer,
          &vk.bloom_source_layout, vk.bloom_render_pass,
          vk.swapchain_extent.width, vk.swapchain_extent.height },
        { &vk.bloom_texture, &vk.bloom_framebuffer, &vk.bloom_layout,
          vk.bloom_render_pass, max(vk.swapchain_extent.width / bloom_downsample, 1),
          max(vk.swapchain_extent.height / bloom_downsample, 1) },
        { &vk.blur_texture, &vk.blur_framebuffer, &vk.blur_layout,
          vk.bloom_render_pass, max(vk.swapchain_extent.width / bloom_downsample, 1),
          max(vk.swapchain_extent.height / bloom_downsample, 1) },
    };

    for (size_t i = 0; i < q_countof(targets); i++) {
        if (!vk_create_color_target(targets[i].texture,
                                    targets[i].width,
                                    targets[i].height,
                                    vk.swapchain_format))
            return false;

        vk_update_texture_descriptor_with_sampler(targets[i].texture,
                                                  vk.postprocess_sampler);

        VkImageView attachments[] = { targets[i].texture->view, vk.depth_view };
        VkFramebufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = targets[i].render_pass,
            .attachmentCount = q_countof(attachments),
            .pAttachments = attachments,
            .width = targets[i].width,
            .height = targets[i].height,
            .layers = 1,
        };

        VkResult result = vk.CreateFramebuffer(vk.device, &create_info, NULL,
                                               targets[i].framebuffer);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateFramebuffer", result);

        *targets[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
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
    bool ok = vert && frag && alpha;

    if (vert)
        vk.DestroyShaderModule(vk.device, vert, NULL);
    if (frag)
        vk.DestroyShaderModule(vk.device, frag, NULL);
    if (alpha)
        vk.DestroyShaderModule(vk.device, alpha, NULL);

    if (ok)
        Com_Printf("Vulkan pixel lightmap shader modules validated\n");
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

    Com_Printf("Vulkan pixel lightmap pipeline layout created\n");
    return true;
}

static void vk_print_pixel_lightmap_mode(void)
{
    int mode = vk_pixel_lightmap_mode();

    if (mode <= 0) {
        Com_Printf("Vulkan pixel lightmaps: disabled\n");
    } else if (mode == 1) {
        Com_Printf("Vulkan pixel lightmaps: upload-only validation mode\n");
    } else {
        Com_Printf("Vulkan pixel lightmaps: draw mode requested\n");
    }
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
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
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
                                          VkExtent2D extent)
{
    VkShaderModule vert = vk_create_shader_module(vk_tex_vert_spv, sizeof(vk_tex_vert_spv));
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
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
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

static bool vk_create_texture_pipeline(void)
{
    return vk_create_texture_pipeline_ex(&vk.texture_pipeline,
                                          vk_tex_frag_spv,
                                          sizeof(vk_tex_frag_spv),
                                          false,
                                          vk.swapchain_extent);
}

static uint32_t vk_bloom_downsample_value(void)
{
    int value = vk_bloom_downsample ? Cvar_ClampInteger(vk_bloom_downsample, 2, 16) : 4;

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
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
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
                                     bool glowmap)
{
    VkShaderModule vert = vk_create_shader_module(vk_world_vert_spv,
                                                  sizeof(vk_world_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag = glowmap ?
        vk_create_shader_module(vk_world_glow_frag_spv,
                                sizeof(vk_world_glow_frag_spv)) : alpha_test ?
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
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = additive ?
            VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = additive ?
            VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
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

static bool vk_create_pixel_world_pipeline(VkPipeline *pipeline, bool alpha_test)
{
    if (vk_pixel_lightmap_mode() < 2)
        return true;
    if (!vk.pixel_world_pipeline_layout) {
        Com_SetLastError("No Vulkan pixel world pipeline layout");
        return false;
    }

    VkShaderModule vert = vk_create_shader_module(vk_world_pixel_vert_spv,
                                                  sizeof(vk_world_pixel_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag = alpha_test ?
        vk_create_shader_module(vk_world_pixel_alpha_frag_spv,
                                sizeof(vk_world_pixel_alpha_frag_spv)) :
        vk_create_shader_module(vk_world_pixel_frag_spv,
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
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
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

    Com_Printf("Vulkan pixel lightmap %s pipeline created\n",
               alpha_test ? "alpha" : "opaque");
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
    VkShaderModule vert = vk_create_shader_module(vk_alias_vert_spv,
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
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = color_write ?
            (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT) : 0,
    };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
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
        Com_SetLastError("Vulkan surface has zero extent");
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
    vk.depth_format = vk_choose_depth_format(vk_shadow_stencil_requested());
    if (vk.depth_format == VK_FORMAT_UNDEFINED) {
        Com_SetLastError("No supported Vulkan depth format");
        return false;
    }
    Com_Printf("Vulkan depth format: %s, shadow stencil %s\n",
               vk_format_name(vk.depth_format),
               vk_shadow_stencil_enabled() ? "enabled" : "disabled");
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

    vk_print_pixel_lightmap_mode();
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
        !vk_create_texture_pipeline_ex(&vk.waterwarp_pipeline,
                                       vk_waterwarp_frag_spv,
                                       sizeof(vk_waterwarp_frag_spv),
                                       false,
                                       vk.swapchain_extent) ||
        !vk_create_texture_pipeline_ex(&vk.bloom_downscale_pipeline,
                                       vk_bloom_downscale_frag_spv,
                                       sizeof(vk_bloom_downscale_frag_spv),
                                       false,
                                       bloom_extent) ||
        !vk_create_texture_pipeline_ex(&vk.bloom_blur_pipeline,
                                       vk_bloom_blur_frag_spv,
                                       sizeof(vk_bloom_blur_frag_spv),
                                       false,
                                       bloom_extent) ||
        !vk_create_texture_pipeline_ex(&vk.bloom_add_pipeline,
                                       vk_tex_frag_spv,
                                       sizeof(vk_tex_frag_spv),
                                       true,
                                       vk.swapchain_extent) ||
        !vk_create_color3d_pipeline(&vk.color3d_pipeline, VK_TRUE, VK_TRUE, VK_FALSE,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) ||
        !vk_create_color3d_pipeline(&vk.line3d_pipeline, VK_TRUE, VK_TRUE, VK_FALSE,
                                    VK_PRIMITIVE_TOPOLOGY_LINE_LIST) ||
        !vk_create_color3d_pipeline(&vk.debug_line_pipeline, VK_FALSE, VK_FALSE, VK_TRUE,
                                    VK_PRIMITIVE_TOPOLOGY_LINE_LIST) ||
        !vk_create_color3d_pipeline(&vk.beam_pipeline, VK_TRUE, VK_FALSE, VK_TRUE,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) ||
        !vk_create_world_pipeline(&vk.world_pipeline, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE) ||
        !vk_create_world_pipeline(&vk.world_alpha_pipeline, VK_TRUE, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE) ||
        !vk_create_world_pipeline(&vk.world_glow_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_TRUE, VK_TRUE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_pipeline, VK_FALSE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_alpha_pipeline, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.sky_pipeline, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE) ||
        !vk_create_world_pipeline(&vk.sprite_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE) ||
        !vk_create_world_pipeline(&vk.sprite_alpha_pipeline, VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE) ||
        !vk_create_world_pipeline(&vk.particle_add_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE) ||
        !vk_create_world_pipeline(&vk.glare_pipeline, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE) ||
        !vk_create_world_pipeline(&vk.debug_text_pipeline, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE) ||
        !vk_create_alias_pipeline(&vk.alias_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_alpha_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_depth_pipeline, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_blend_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_shadow_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_TRUE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_TRUE, -1.0f, -2.0f, &shadow_depth_stencil) ||
        !vk_create_alias_pipeline(&vk.alias_line_pipeline, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_depth_resources() ||
        !vk_create_framebuffers() ||
        !vk_create_scene_target())
        return false;

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
    vk.CmdBindVertexBuffers(cmd, first, count, buffers, offsets);
    c.vertexArrayBinds++;
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
    return gl_bloom && gl_bloom->integer > 0 &&
        vk.scene_framebuffer && vk.bloom_source_framebuffer &&
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
    VkPipeline pipeline = vk.texture_pipeline;

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

    lerp.backlerp = Q_clip(ent->backlerp, 0.0f, 1.0f);

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
        radius *= ent->scale ? ent->scale : 1.0f;

        for (int i = 0; i < 4; i++) {
            if (PlaneDiffFast(ent->origin, &vk.world.frustum[i]) < -radius) {
                c.spheresCulled++;
                return true;
            }
        }
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

static bool vk_create_test_triangle(void)
{
    static const vk_vertex_t vertices[] = {
        { { -24.0f, -16.0f, -96.0f }, { 1.0f, 0.15f, 0.10f, 1.0f }, { 0.0f, 0.0f } },
        { {  24.0f, -16.0f, -96.0f }, { 0.1f, 0.85f, 0.25f, 1.0f }, { 1.0f, 0.0f } },
        { {   0.0f,  24.0f, -96.0f }, { 0.1f, 0.35f, 1.00f, 1.0f }, { 0.5f, 1.0f } },
    };
    static const uint32_t indices[] = { 0, 1, 2 };

    if (!vk_upload_mesh(&vk.test_triangle, vertices, q_countof(vertices),
                        indices, q_countof(indices)))
        return false;

    return true;
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
    uint32_t pixels[16 * 16];
    int shape = vk_partshape ? Cvar_ClampInteger(vk_partshape, 0, 2) : 0;

    if (shape == 1) {
        memset(pixels, 0, sizeof(pixels));
        for (int y = 3; y <= 12; y++) {
            for (int x = 3; x <= 12; x++) {
                pixels[y * 16 + x] = MakeColor(255, 255, 255, 255 * 0.6f);
            }
        }
    } else {
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                float fx = x - 16 / 2 + 0.5f;
                float fy = y - 16 / 2 + 0.5f;
                float f = sqrtf(fx * fx + fy * fy);
                byte alpha;

                f = 1.0f - f / ((16 - shape) / 2.0f - 0.5f);
                f *= 1 << shape;
                alpha = 255 * Q_clipf(f, 0.0f, 1.0f - shape * 0.2f);
                pixels[y * 16 + x] = MakeColor(255, 255, 255, alpha);
            }
        }
    }

    if (!vk_upload_texture_data(&vk.particle_texture, 16, 16, pixels, false))
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
            float f = abs(x - 16 / 2) - 0.5f;
            byte alpha;

            f = 1.0f - f / (16 / 2 - 2.5f);
            alpha = 255 * Q_clipf(f, 0.0f, 1.0f);
            pixels[y * 16 + x] = MakeColor(255, 255, 255, alpha);
        }
    }

    return vk_upload_texture_data(&vk.beam_texture, 16, 16, pixels, false);
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

static void vk_draw_mesh(const vk_mesh_t *mesh, const mat4_t mvp, const float color[4])
{
    if (!vk.render_pass_active || !vk.color3d_pipeline ||
        !mesh->vertices.buffer || !mesh->indices.buffer || !mesh->index_count)
        return;

    vk_color3d_push_t push;
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    memcpy(push.color, color, sizeof(push.color));

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = sizeof(vk.particle_batch) * vk.current_image;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.color3d_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &mesh->vertices.buffer, &offset);
    vk.CmdBindIndexBuffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
    c.trisDrawn += mesh->index_count / 3;
    vk_count_batch3d();
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
    vk.CmdBindIndexBuffer(cmd, vk.null_model.indices.buffer, 0,
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
    vk_world_push_t push;
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
    vk.CmdBindIndexBuffer(cmd, vk.debug_text_indices.buffer, 0,
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
    return 1.0f;
}

static bool vk_world_face_in_pass(const mface_t *face, vk_world_pass_t pass)
{
    bool translucent = face->drawflags & SURF_TRANS_MASK;

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

static void vk_world_dynamic_light(const vk_world_face_t *face,
                                   const refdef_t *fd, const entity_t *ent,
                                   const vec3_t axis[3], float dlight[4])
{
    Vector4Clear(dlight);

    if (!face || !face->face || !face->face->plane ||
        !fd || fd->num_dlights <= 0 || !fd->dlights ||
        !vk_dynamic_lights_enabled() ||
        (face->face->drawflags & vk.world.nolm_mask))
        return;

    for (int i = 0; i < fd->num_dlights; i++) {
        const dlight_t *light = &fd->dlights[i];
        vec3_t light_origin;
        float plane_dist = vk_world_face_light_plane_dist(face->face, light,
                                                         ent, axis);
        float f;

        vk_world_face_light_origin(light, ent, axis, light_origin);
        f = vk_world_dynamic_light_fraction(light, face->face, light_origin, plane_dist);
        if (f <= 0.0f)
            continue;

        f *= 1.0f / 255.0f;
        VectorMA(dlight, f, light->color, dlight);
    }

    dlight[0] = Q_clipf(dlight[0], 0.0f, 1.0f);
    dlight[1] = Q_clipf(dlight[1], 0.0f, 1.0f);
    dlight[2] = Q_clipf(dlight[2], 0.0f, 1.0f);
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

    bool use_marked = marked_only ||
        (vk_world_vis && vk_world_vis->integer && vk.world.face_count);
    vk_color3d_push_t push;
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    Vector4Set(push.color, 1.0f, 0.0f, 0.0f, 1.0f);

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.line3d_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.world.mesh.vertices.buffer, &offset);
    vk.CmdBindIndexBuffer(cmd, vk.world.line_indices.buffer, 0,
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

static void vk_draw_world_mesh(const mat4_t mvp, bool marked_only,
                               VkPipeline pipeline, vk_world_pass_t pass,
                               float entity_alpha, const refdef_t *fd,
                               const entity_t *ent, const vec3_t axis[3])
{
    const vk_mesh_t *mesh = &vk.world.mesh;
    bool use_marked = marked_only ||
        (vk_world_vis && vk_world_vis->integer && vk.world.face_count);
    bool special_light_mode = (vk_lightmap && vk_lightmap->integer) ||
        (vk_fullbright && vk_fullbright->integer) ||
        (vk_vertexlight && vk_vertexlight->integer);
    bool pixel_mode = vk_pixel_lightmap_mode() >= 2;
    bool pixel_requested = vk_pixel_lightmap_mode() >= 2 &&
        pass == VK_WORLD_OPAQUE && !vk.drawing_bloom && !ent &&
        !special_light_mode;
    bool pixel_ready = vk.pixel_world_pipeline && vk.pixel_world_alpha_pipeline &&
        vk.pixel_world_pipeline_layout && vk.world.pixel_lmuv_buffer.buffer &&
        vk.world.pixel_lightmap_texture.descriptor_set;
    bool pixel_world = pixel_requested && pixel_ready;
    bool world_batching = pass == VK_WORLD_OPAQUE && !ent && !vk.drawing_bloom &&
        !pixel_world;

    if (!vk.render_pass_active || !pipeline ||
        !mesh->vertices.buffer || !mesh->indices.buffer || !mesh->index_count ||
        !vk.world.batch_count || !vk.world.batches || !vk.world.faces)
        return;

    vk_draw_scope_t old_scope = vk.draw_scope;
    vk.draw_scope = VK_DRAW_WORLD;

    if (pixel_mode && !pixel_requested && !vk_pixel_lightmaps_scope_logged) {
        Com_Printf("Vulkan pixel lightmaps mode 2 applies only to opaque world surfaces\n");
        vk_pixel_lightmaps_scope_logged = true;
    }

    if (pixel_requested && !pixel_ready) {
        if (!vk_pixel_lightmaps_warned) {
            Com_WPrintf("Vulkan pixel lightmaps mode 2 needs vk_pixel_lightmaps 2 before video init; using mode 1\n");
            vk_pixel_lightmaps_warned = true;
        }
        Cvar_Set("vk_pixel_lightmaps", "1");
    }
    if (pixel_world && !vk_pixel_lightmaps_draw_logged) {
        Com_Printf("Vulkan pixel lightmaps: drawing opaque world with pixel pipeline\n");
        vk_pixel_lightmaps_draw_logged = true;
    }

    vk_world_pixel_push_t push;
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    push.color[0] = 1.0f;
    push.color[1] = 1.0f;
    push.color[2] = 1.0f;
    push.color[3] = entity_alpha;
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    vk_fog_params(fd, push.fog);
    push.intensity = vk_texture_intensity();
    push._pad = 0.0f;
    push.lm_scale[0] = -1.0f;
    push.lm_scale[1] = -1.0f;
    push.lm_offset[0] = 0.0f;
    push.lm_offset[1] = 0.0f;

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;
    VkBuffer pixel_buffers[] = {
        mesh->vertices.buffer,
        vk.world.pixel_lmuv_buffer.buffer,
    };
    VkDeviceSize pixel_offsets[] = { 0, 0 };
    uint32_t *batch_index_mapped = vk.world.batch_index_mapped;
    uint32_t batch_index_base = vk.current_image * vk.world.batch_index_capacity;
    uint32_t batch_index_cursor = 0;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                       pixel_world ? vk.pixel_world_pipeline : pipeline);
    if (pixel_world)
        vk_bind_vertex_buffers(cmd, 0, 2, pixel_buffers, pixel_offsets);
    else
        vk_bind_vertex_buffers(cmd, 0, 1, &mesh->vertices.buffer, &offset);
    vk.CmdBindIndexBuffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    if (pixel_world) {
        vk_bind_pixel_world_descriptor(cmd, 1,
                                       vk.world.pixel_lightmap_texture.descriptor_set);
        vk_push_pixel_world_constants(cmd, sizeof(push), &push);
    } else {
        vk_push_constants(cmd, sizeof(vk_world_push_t), &push);
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
                    vk.CmdBindIndexBuffer(cmd, vk.world.batch_indices.buffer, bind_offset, VK_INDEX_TYPE_UINT32); \
                } \
                vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline); \
                vk_bind_texture_descriptor(cmd, texture->descriptor_set); \
                vk_push_constants(cmd, sizeof(vk_world_push_t), &push); \
                if (draw_group) \
                    vk.CmdDrawIndexed(cmd, group_count, 1, 0, 0, 0); \
                vk.CmdBindIndexBuffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32); \
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

            if (!face->face || !face->face->texinfo || !face->face->plane) {
                VK_FLUSH_WORLD_GROUP();
                continue;
            }
            if (use_marked && face->face->drawframe != vk.world.drawframe) {
                VK_FLUSH_WORLD_GROUP();
                continue;
            }
            if (!vk_world_face_in_pass(face->face, pass)) {
                VK_FLUSH_WORLD_GROUP();
                continue;
            }

            const image_t *image = vk_world_face_image(face->face, fd, ent);
            if (!image || image->texnum >= MAX_RIMAGES) {
                VK_FLUSH_WORLD_GROUP();
                continue;
            }

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
                vk_push_constants(cmd, sizeof(vk_world_push_t), &push);
                vk.CmdDrawIndexed(cmd, face->index_count, 1, face->first_index, 0, 0);
                c.trisDrawn += face->index_count / 3;
                vk_count_batch3d();
                continue;
            }

            vec4_t group_dlight;
            bool can_group = world_batching &&
                batch_index_mapped && vk.world.batch_indices.buffer &&
                vk.world.batch_index_data &&
                image->texnum == batch->texture_index &&
                !(face->face->drawflags & (SURF_ALPHATEST | SURF_FLOWING |
                                           SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y |
                                           SURF_WARP | SURF_TRANS33 | SURF_TRANS66)) &&
                !(face->face->drawflags & vk.world.nolm_mask) &&
                !vk_world_face_glowmap_enabled(face->face, image);

            if (can_group && fd && fd->num_dlights > 0 && vk_dynamic_lights_enabled()) {
                vk_world_dynamic_light(face, fd, ent, axis, group_dlight);
                can_group = group_dlight[0] == 0.0f && group_dlight[1] == 0.0f &&
                    group_dlight[2] == 0.0f;
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
                    push.dlight[3] = fd ? fd->time : 0.0f;
                    push.intensity = vk_texture_intensity();
                    push.lm_scale[0] = -1.0f;
                    push.lm_scale[1] = -1.0f;
                    push.lm_offset[0] = 0.0f;
                    push.lm_offset[1] = 0.0f;
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

            if (pixel_world) {
                face_pipeline = (face->face->drawflags & SURF_ALPHATEST) ?
                    vk.pixel_world_alpha_pipeline : vk.pixel_world_pipeline;
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
            vk_world_dynamic_light(face, fd, ent, axis, push.dlight);
            push.dlight[3] = fd ? fd->time : 0.0f;
            push.intensity = vk_texture_intensity();
            push.color[3] = entity_alpha * vk_world_face_alpha(face->face);
            if (pixel_world && face->pixel_lm_w && face->pixel_lm_h) {
                float aw = vk.world.pixel_lightmap_texture.width ?
                    (float)vk.world.pixel_lightmap_texture.width : 1.0f;
                float ah = vk.world.pixel_lightmap_texture.height ?
                    (float)vk.world.pixel_lightmap_texture.height : 1.0f;
                push.lm_scale[0] = max(face->pixel_lm_w - 1, 0) / aw;
                push.lm_scale[1] = max(face->pixel_lm_h - 1, 0) / ah;
                push.lm_offset[0] = (face->pixel_lm_x + 0.5f) / aw;
                push.lm_offset[1] = (face->pixel_lm_y + 0.5f) / ah;
            } else {
                push.lm_scale[0] = -1.0f;
                push.lm_scale[1] = -1.0f;
                push.lm_offset[0] = 0.0f;
                push.lm_offset[1] = 0.0f;
            }
            if (pixel_world)
                vk_push_pixel_world_constants(cmd, sizeof(push), &push);
            else
                vk_push_constants(cmd, sizeof(vk_world_push_t), &push);
            vk.CmdDrawIndexed(cmd, face->index_count, 1, face->first_index, 0, 0);
            c.facesDrawn++;
            c.facesTris += face->index_count / 3;
            c.trisDrawn += face->index_count / 3;
            vk_count_batch3d();

            if ((pass == VK_WORLD_OPAQUE ||
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
                    vk_push_constants(cmd, sizeof(vk_world_push_t), &push);
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
    vk.CmdBindIndexBuffer(cmd, indices->buffer, 0, VK_INDEX_TYPE_UINT32);
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
    if ((vk_drawsky && !vk_drawsky->integer) ||
        (vk_gl_drawsky && !vk_gl_drawsky->integer))
        return;
    if (!vk.world.sky_visible)
        return;
    if (!vk.render_pass_active || !vk.sky_pipeline ||
        !vk.skybox.vertices.buffer || !vk.skybox.indices.buffer)
        return;

    mat4_t mvp;
    vk_world_push_t push;
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
    vk.CmdBindIndexBuffer(cmd, vk.skybox.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);

    for (uint32_t face = 0; face < 6; face++) {
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
    vk.world.sky_visible = false;
    vk_update_world_view(fd);
    if (!vk_lockpvs || !vk_lockpvs->integer) {
        vk.world.visframe++;
        vk_mark_world_visible_nodes(fd);
    }
    vk_setup_world_frustum(fd);

    clipflags = (!vk_cull_nodes || vk_cull_nodes->integer) &&
        (!vk_world_cull || vk_world_cull->integer) ?
        VK_NODE_CLIPPED : VK_NODE_UNCLIPPED;
    vk_mark_world_node_faces(bsp->nodes, fd, clipflags);
}

#define VK_BACKFACE_EPSILON 0.01f

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
        vec_t dot;

        if (!face->texinfo || !face->plane)
            continue;
        if (face->drawflags & (SURF_SKY | SURF_NODRAW))
            continue;
        if (!translucent && (face->drawflags & SURF_TRANS_MASK))
            continue;
        if (!face->texinfo->image ||
            face->texinfo->image->texnum >= MAX_RIMAGES)
            continue;

        dot = PlaneDiffFast(transformed, face->plane);
        if ((face->drawflags & DSURF_PLANEBACK) ?
            (dot > VK_BACKFACE_EPSILON) : (dot < -VK_BACKFACE_EPSILON)) {
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

    if (!bsp || index < 1 || index >= bsp->nummodels)
        return;
    if (!vk_world_textures || !vk_world_textures->integer)
        return;
    model = &bsp->models[index];
    if (!model->numfaces)
        return;

    vk_entity_axis(ent, axis);
    if (vk_bmodel_culled(model, ent, axis))
        return;

    vk_entity_mvp(mvp, fd, ent, axis);

    vk.world.drawframe++;
    bool translucent = (ent->flags & RF_TRANSLUCENT) || translucent_faces;
    vk_world_pass_t world_pass = translucent_faces ? VK_WORLD_ALPHA :
        (translucent ? VK_WORLD_ENTITY_ALPHA : VK_WORLD_OPAQUE);
    vk_mark_bmodel_faces(model, ent, fd, axis, translucent);
    vk_draw_world_mesh(mvp, true,
                        translucent ? vk.sprite_pipeline : vk.world_pipeline,
                       world_pass,
                       (ent->flags & RF_TRANSLUCENT) ? ent->alpha : 1.0f,
                       fd, ent, axis);
    vk_draw_world_outlines(mvp, true, world_pass, fd, ent);
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
    vk.CmdBindIndexBuffer(cmd, model->mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
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
                                     const vk_alias_push_t *push,
                                     bool count_stats)
{
    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_vertex_buffers(cmd, 0, 2, buffers, offsets);
    vk.CmdBindIndexBuffer(cmd, model->mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(*push), push);
    vk.CmdDrawIndexed(cmd, model->mesh.index_count, 1, 0, 0, 0);
    if (count_stats) {
        c.trisDrawn += model->mesh.index_count / 3;
        vk_count_batch3d();
    }
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
    if (!gl_showtris || !(gl_showtris->integer & SHOWTRIS_MESH))
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
    vk.CmdBindIndexBuffer(cmd, model->alias_line_indices.buffer, 0,
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
    vk_alias_push_t push;

    if (!vk_shadows || !vk_shadows->integer || !vk.alias_shadow_pipeline)
        return;
    if (ent->flags & (RF_WEAPONMODEL | RF_NOSHADOW))
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

    vk_draw_alias_color_pass(vk.command_buffers[vk.current_image],
                              vk.alias_shadow_pipeline, buffers, offsets,
                              model, &push, count_stats);
}

static void vk_draw_alias_model(const entity_t *ent, const refdef_t *fd)
{
    vk_model_t *model = vk_model_for_handle(ent->model);

    bool translucent = ent->flags & RF_TRANSLUCENT;
    bool bloom_only = ent->flags & RF_BLOOM_ONLY;
    bool bloom_shell = (ent->flags & RF_SHELL_MASK) && !(ent->flags & RF_NOBLOOM);
    bool draw_model = !bloom_only || vk.drawing_bloom;

    if (!model || model->type != VK_MODEL_ALIAS ||
        !model->mesh.vertices.buffer || !model->mesh.indices.buffer ||
        !model->vertex_count || !model->alias_batch_count)
        return;

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
            pipeline = (bloom_only || bloom_shell) ? vk.alias_blend_pipeline : VK_NULL_HANDLE;
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
                vk_draw_alias_pass(cmd, vk.alias_blend_pipeline, buffers, offsets,
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
    VkPipeline pipeline = vk.sprite_pipeline;

    if (!translucent &&
        (frame->image->flags & (IF_TRANSPARENT | IF_PALETTED)) ==
            (IF_TRANSPARENT | IF_PALETTED) &&
        vk.sprite_alpha_pipeline) {
        pipeline = vk.sprite_alpha_pipeline;
    }

    vec3_t viewaxis[3], left, right, down, up, xaxis, yaxis, origin;
    float scale = ent->scale ? ent->scale : 1.0f;
    mat4_t model_matrix, mvp;
    vk_world_push_t push;
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
    vk.CmdBindIndexBuffer(cmd, vk.sprite_quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
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

static bool vk_point_occluded(const vec3_t origin, const refdef_t *fd)
{
    bsp_t *bsp = vk.world.cache;
    lightpoint_t point;

    if (!fd || !bsp || !bsp->nodes)
        return false;

    BSP_LightPoint(&point, fd->vieworg, origin, bsp->nodes,
                   vk.world.nolm_mask | SURF_TRANS_MASK);
    vk_trace_bmodel_light_points(fd, bsp, fd->vieworg, origin, &point);

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
    vk_world_push_t push;
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
    vk.CmdBindIndexBuffer(cmd, vk.sprite_quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
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
    vk_world_push_t push;
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

static void vk_draw_glare(const refdef_t *fd)
{
    if (!vk_glare || !vk_glare->integer || !glr.num_glare_sources || !fd ||
        !vk.world.cache || !vk.world.cache->nodes ||
        !vk.glare_pipeline || !vk.particle_texture.descriptor_set ||
        !vk.sprite_quad.vertices.buffer || !vk.sprite_quad.indices.buffer)
        return;

    vec3_t viewaxis[3];
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    AnglesToAxis(fd->viewangles, viewaxis);
    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.glare_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.sprite_quad.vertices.buffer, &offset);
    vk.CmdBindIndexBuffer(cmd, vk.sprite_quad.indices.buffer, 0,
                          VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, vk.particle_texture.descriptor_set);

    for (int i = 0; i < glr.num_glare_sources; i++) {
        glare_source_t *gs = &glr.glare_sources[i];
        vec3_t to_src, view_dir, to_viewer;
        bool visible = true;

        for (int j = 0; j < 4; j++) {
            if (PlaneDiff(gs->origin, &vk.world.frustum[j]) < -2.5f) {
                visible = false;
                break;
            }
        }

        VectorSubtract(gs->origin, fd->vieworg, to_src);
        float dist = VectorNormalize2(to_src, view_dir);
        if (dist < 1.0f)
            visible = false;
        VectorNegate(view_dir, to_viewer);
        float view_angle = DotProduct(to_viewer, gs->normal);
        if (view_angle < 0.01f)
            visible = false;
        if (visible && vk_point_occluded(gs->origin, fd))
            visible = false;

        gs->visible = visible;
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

        float scale = (vk_glare_size ? vk_glare_size->value : 24.0f) * gs->brightness;
        if (dist > 20.0f)
            scale *= 1.0f + dist * 0.004f;
        scale = min(scale, 200.0f);

        float alpha = view_angle * gs->brightness *
            (vk_glare_intensity ? vk_glare_intensity->value : 0.5f) *
            gs->visibility;
        alpha = min(alpha, 1.0f);
        if (alpha < 0.01f || scale <= 0.0f)
            continue;

        vec3_t left, right, down, up, xaxis, yaxis, origin;
        mat4_t model_matrix, mvp;
        vk_world_push_t push;

        VectorScale(viewaxis[1], scale, left);
        VectorScale(viewaxis[1], -scale, right);
        VectorScale(viewaxis[2], -scale, down);
        VectorScale(viewaxis[2], scale, up);
        VectorSubtract(right, left, xaxis);
        VectorSubtract(up, down, yaxis);
        VectorAdd3(gs->origin, left, down, origin);

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
    vk_world_push_t push;
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
    vk.CmdBindIndexBuffer(cmd, vk.sprite_quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, vk.sprite_quad.index_count, 1, 0, 0, 0);
    c.trisDrawn += vk.sprite_quad.index_count / 3;
    vk_count_batch3d();
    vk_draw_fx_outlines(&vk.sprite_quad, &vk.sprite_quad_line_indices,
                        vk.sprite_quad_line_index_count, mvp);
    if (gl_showtris && (gl_showtris->integer & SHOWTRIS_FX)) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.sprite_pipeline);
        vk_bind_vertex_buffers(cmd, 0, 1, &vk.sprite_quad.vertices.buffer, &offset);
        vk.CmdBindIndexBuffer(cmd, vk.sprite_quad.indices.buffer, 0,
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
    vk.CmdBindIndexBuffer(cmd, vk.beam_cylinder.indices.buffer, 0,
                          VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, vk.beam_cylinder.index_count, 1, 0, 0, 0);
    c.trisDrawn += vk.beam_cylinder.index_count / 3;
    vk_count_batch3d();
    vk_draw_fx_outlines(&vk.beam_cylinder, &vk.beam_cylinder_line_indices,
                        vk.beam_cylinder_line_index_count, mvp);
    if (gl_showtris && (gl_showtris->integer & SHOWTRIS_FX))
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.beam_pipeline);
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
        float glow[4];

        memcpy(glow, color, sizeof(glow));
        glow[3] *= 0.35f;

        for (int i = 0; i < num_segments; i++)
            vk_draw_beam_segment(segments[i], segments[i + 1], fd, glow, width * 2.5f);
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
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.beam_pipeline);
    } else {
        if (!vk.sprite_pipeline || !vk.beam_texture.descriptor_set)
            return;
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.sprite_pipeline);
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
    int size = smax * tmax * 3;
    float fracu = point->s - s;
    float fracv = point->t - t;
    float w1 = (1.0f - fracu) * (1.0f - fracv);
    float w2 = fracu * (1.0f - fracv);
    float w3 = fracu * fracv;
    float w4 = (1.0f - fracu) * fracv;

    VectorClear(color);

    if (!lightmap || s < 0 || t < 0 || s + 1 >= smax || t + 1 >= tmax)
        return;

    for (int i = 0; i < surf->numstyles; i++) {
        const byte *b1 = &lightmap[3 * ((t + 0) * smax + (s + 0))];
        const byte *b2 = &lightmap[3 * ((t + 0) * smax + (s + 1))];
        const byte *b3 = &lightmap[3 * ((t + 1) * smax + (s + 1))];
        const byte *b4 = &lightmap[3 * ((t + 1) * smax + (s + 0))];

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

    if (!grid->numleafs ||
        (vk_lightgrid && !vk_lightgrid->integer) ||
        (vk_gl_lightgrid && !vk_gl_lightgrid->integer))
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
    if ((vk_drawentities && !vk_drawentities->integer) ||
        (vk_gl_drawentities && !vk_gl_drawentities->integer))
        return;
    if (fd->num_entities <= 0 || !fd->entities)
        return;

    vk_draw_scope_t old_scope = vk.draw_scope;
    vk.draw_scope = VK_DRAW_ENTITY;

    for (int i = fd->num_entities - 1; i >= 0; i--) {
        const entity_t *ent = &fd->entities[i];

        if (vk_entity_in_pass(ent, pass))
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

static void vk_draw_bloom_world_glowmaps(const refdef_t *fd)
{
    if (!fd || (fd->rdflags & RDF_NOWORLDMODEL) ||
        (vk_drawworld && !vk_drawworld->integer) ||
        !vk.world.mesh.index_count || !vk.world_glow_pipeline)
        return;

    bool old = vk.drawing_bloom;
    mat4_t mvp;

    vk.drawing_bloom = true;
    vk_set_3d_viewport(fd);
    vk_world_mvp(mvp, fd);
    vk_draw_world_mesh(mvp, false, vk.world_pipeline, VK_WORLD_OPAQUE,
                       1.0f, fd, NULL, NULL);
    vk.drawing_bloom = old;
}

static void vk_surface_color(const mface_t *face, float color[4])
{
    float intensity = vk_texture_intensity();

    if (face->drawflags & SURF_TRANS33) {
        Vector4Set(color, 1.0f / intensity, 1.0f / intensity,
                   1.0f / intensity, 0.33f);
        return;
    }
    if (face->drawflags & SURF_TRANS66) {
        Vector4Set(color, 1.0f / intensity, 1.0f / intensity,
                   1.0f / intensity, 0.66f);
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

    if (vk_coloredlightmaps && !vk_coloredlightmaps->integer) {
        float y = LUMINANCE(rgb[0], rgb[1], rgb[2]);
        rgb[0] = rgb[1] = rgb[2] = y;
    }

    return MakeColor(Q_clipf(rgb[0], 0.0f, 255.0f),
                     Q_clipf(rgb[1], 0.0f, 255.0f),
                     Q_clipf(rgb[2], 0.0f, 255.0f), 255);
}

static uint32_t vk_pixel_lightmap_checksum(const uint32_t *pixels, size_t count)
{
    uint32_t hash = 2166136261u;

    for (size_t i = 0; i < count; i++) {
        hash ^= pixels[i];
        hash *= 16777619u;
    }

    return hash;
}

static uint32_t vk_pixel_lightmap_hash_u32(uint32_t hash, uint32_t value)
{
    hash ^= value;
    return hash * 16777619u;
}

static uint32_t vk_pixel_lightmap_hash_float(uint32_t hash, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return vk_pixel_lightmap_hash_u32(hash, bits);
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
    uint32_t invalid = 0;
    uint64_t total_area = 1;
    int max_w = 1;

    for (uint32_t i = 0; i < face_count; i++) {
        faces[i].pixel_lm_x = faces[i].pixel_lm_y = 0;
        faces[i].pixel_lm_w = faces[i].pixel_lm_h = 0;
        const mface_t *face = faces[i].face;
        if (!vk_face_has_valid_lightmap(bsp, face)) {
            invalid++;
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
        Com_Printf("Vulkan pixel lightmap CPU atlas: no valid lightmaps\n");
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
    for (size_t i = 0; i < pixel_count; i++)
        pixels[i] = MakeColor(255, 255, 255, 255);

    for (uint32_t i = 0; i < face_count; i++) {
        const mface_t *face = plan[i].face;
        if (!face)
            continue;
        for (int t = -1; t <= face->lm_height; t++) {
            int src_t = Q_clipf(t, 0, face->lm_height - 1);
            for (int s = -1; s <= face->lm_width; s++) {
                int src_s = Q_clipf(s, 0, face->lm_width - 1);
                pixels[(plan[i].y + t) * atlas_w + plan[i].x + s] =
                    vk_pixel_lightmap_texel(face, fd, src_s, src_t);
            }
        }
    }

    uint32_t checksum = vk_pixel_lightmap_checksum(pixels, pixel_count);

    bool log_atlas = !vk_pixel_lightmaps_atlas_logged;
    if (log_atlas) {
        Com_Printf("Vulkan pixel lightmap CPU atlas (mode %d): %u valid, %u skipped, %dx%d, checksum %08x\n",
                   vk_pixel_lightmap_mode(), valid, invalid, atlas_w, atlas_h, checksum);
        vk_pixel_lightmaps_atlas_logged = true;
    }
    vk_destroy_texture_resource(&vk.world.pixel_lightmap_texture);
    if (vk_upload_texture_data(&vk.world.pixel_lightmap_texture,
                               atlas_w, atlas_h, pixels, false)) {
        if (log_atlas) {
            Com_Printf("Vulkan pixel lightmap atlas texture uploaded: %dx%d\n",
                       atlas_w, atlas_h);
        }
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
    uint32_t lmuv_hash = 2166136261u;
    uint32_t lmuv_count = 0;
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
        float scale_s = image->width ? 1.0f / image->width : 1.0f;
        float scale_t = image->height ? 1.0f / image->height : 1.0f;
        if (face->drawflags & SURF_N64_UV) {
            scale_s *= 0.5f;
            scale_t *= 0.5f;
        }
        vec3_t center;

        VectorClear(center);

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
            if (pixel_lm_debug) {
                float lms = 0.0f;
                float lmt = 0.0f;
                if (vk_face_has_valid_lightmap(bsp, face)) {
                    lms = (DotProduct(src->point, face->lm_axis[0]) +
                           face->lm_offset[0]) / max(face->lm_width, 1);
                    lmt = (DotProduct(src->point, face->lm_axis[1]) +
                           face->lm_offset[1]) / max(face->lm_height, 1);
                }
                lmuv_hash = vk_pixel_lightmap_hash_float(lmuv_hash, lms);
                lmuv_hash = vk_pixel_lightmap_hash_float(lmuv_hash, lmt);
                lmuv_data[v * 2 + 0] = lms;
                lmuv_data[v * 2 + 1] = lmt;
                lmuv_count++;
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

    vk_pixel_lightmap_plan(bsp, draw_faces, draw_face_count, fd);
    if (pixel_lm_debug && !vk_pixel_lightmaps_lmuv_logged)
        Com_Printf("Vulkan pixel lightmap CPU lmuv (mode %d): %u verts, checksum %08x\n",
                   vk_pixel_lightmap_mode(), lmuv_count, lmuv_hash);

    line_indices = vk_build_line_indices(indices, idx, &line_index_count);

    bool ok = vk_upload_mesh(&vk.world.mesh, vertices, v, indices, idx);
    if (ok) {
        vk_destroy_buffer(&vk.world.pixel_lmuv_buffer);
        if (lmuv_data) {
            size_t lmuv_size = sizeof(*lmuv_data) * 2 * v;
            if (vk_upload_buffer(&vk.world.pixel_lmuv_buffer, lmuv_data, lmuv_size,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
                if (!vk_pixel_lightmaps_lmuv_logged) {
                    Com_Printf("Vulkan pixel lightmap lmuv buffer uploaded: %zu bytes\n",
                               lmuv_size);
                    vk_pixel_lightmaps_lmuv_logged = true;
                }
            } else {
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
        if (vk.world.lightstyles[i] != fd->lightstyles[i].white)
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
            vk_pixel_lightmaps_draw_logged = false;
            vk_pixel_lightmaps_scope_logged = false;
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

    if (!vk.world.cache || (!vk_world_lighting_modified() && !styles_modified))
        return;

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
    if (!bsp || !bsp->faces)
        return;

    int n64surfs = 0;

    for (int i = 0; i < bsp->numfaces; i++) {
        mface_t *face = &bsp->faces[i];

        if (!face->texinfo)
            continue;

        face->drawflags |= face->texinfo->c.flags & ~DSURF_PLANEBACK;

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

static void vk_build_glare_list(bsp_t *bsp)
{
    glr.num_glare_sources = 0;

    if (!vk_glare || !vk_glare->integer)
        return;
    if (!bsp || !bsp->faces || (vk_fullbright && vk_fullbright->integer) ||
        (vk_vertexlight && vk_vertexlight->integer))
        return;

    for (int i = 0; i < bsp->numfaces; i++) {
        mface_t *surf = &bsp->faces[i];

        if ((surf->drawflags & SURF_NODRAW) || !surf->texinfo ||
            !surf->texinfo->image || !surf->texinfo->image->texnum2 ||
            !surf->lightmap || !surf->plane || !surf->firstsurfedge ||
            surf->numsurfedges <= 0 || surf->lm_width <= 0 || surf->lm_height <= 0)
            continue;
        if (!vk_face_edges_are_valid(bsp, surf))
            continue;

        int lightmap_size = surf->lm_width * surf->lm_height * 3;
        if (!bsp->lightmap || surf->lightmap < bsp->lightmap || !surf->numstyles)
            continue;
        ptrdiff_t lightmap_offset = surf->lightmap - bsp->lightmap;
        if (lightmap_offset < 0 ||
            (uint64_t)lightmap_offset + (uint64_t)surf->numstyles * lightmap_size >
            (uint64_t)bsp->numlightmapbytes)
            continue;

        int sc = surf->lm_width / 2;
        int tc = surf->lm_height / 2;
        const byte *pixel = surf->lightmap + 3 * (tc * surf->lm_width + sc);
        float r = pixel[0] / 255.0f;
        float g = pixel[1] / 255.0f;
        float b = pixel[2] / 255.0f;
        float brightness = (r + g + b) * (1.0f / 3.0f);

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
        VectorSet(gs->lightcolor, r, g, b);
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
    Com_Printf("Vulkan world: loading %s\n", buffer);
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
    vk_prepare_world_surfaces(bsp);

    if (!vk_build_world_mesh(bsp, NULL))
        Com_WPrintf("Couldn't build Vulkan world mesh: %s\n", Com_GetLastError());
    else
        Com_Printf("Vulkan world mesh: %u indices, %u faces, %u batches\n",
                   vk.world.mesh.index_count, vk.world.face_count, vk.world.batch_count);
    vk_build_glare_list(bsp);
    vk_clear_world_lighting_modified();
}

static void vk_draw_test_triangle(const refdef_t *fd)
{
    bool enabled = (vk_show_test_triangle && vk_show_test_triangle->integer) ||
        (vk_gl_test && vk_gl_test->integer);

    if (!vk.render_pass_active || !vk.color3d_pipeline ||
        !enabled)
        return;
    if (fd->rdflags & RDF_NOWORLDMODEL)
        return;

    mat4_t mvp;
    const float color[4] = { 0.1f, 0.9f, 0.55f, 1.0f };

    vk_projection_matrix(mvp, fd->fov_x, fd->fov_y, fd->rdflags);
    vk_draw_mesh(&vk.test_triangle, mvp, color);
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

    VKR_SetScale(1.0f / vk_auto_scale());
    VKR_DrawFill8(8, 8, 25 * 8, 26 * 10 + 2, 4);

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
    Com_Printf("VK perf: mode=%s imgs=%u fif=%u draws=%i 3d=%i world=%i ent=%i part=%i bloom=%i other=%i 2d=%i chars=%i pics=%i rects=%i pipe=%i desc=%i push=%i vb=%i tris=%i faces=%i ents=%i parts=%i wait=%uus acq=%uus rec=%uus sub=%uus pres=%uus\n",
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
               vk.wait_usec, vk.acquire_usec, vk.record_usec,
               vk.submit_usec, vk.present_usec);
}

bool VKR_Init(bool total)
{
    if (!total)
        return true;

    Com_Printf("------- VKR_Init -------\n");
    Com_Printf("Using video driver: %s\n", vid->name);

    vk_show_test_triangle = Cvar_Get("vk_show_test_triangle", "0", 0);
    vk_gl_test = Cvar_Get("gl_test", "0", 0);
    vk_drawentities = Cvar_Get("vk_drawentities", "1", CVAR_CHEAT);
    vk_gl_drawentities = Cvar_Get("gl_drawentities", "1", CVAR_CHEAT);
    vk_drawsky = Cvar_Get("vk_drawsky", "1", 0);
    vk_drawsky->changed = vk_drawsky_changed;
    vk_gl_drawsky = Cvar_Get("gl_drawsky", "1", 0);
    vk_gl_drawsky->changed = vk_drawsky_changed;
    vk_swapinterval = Cvar_Get("gl_swapinterval", "1", CVAR_ARCHIVE);
    vk_swapinterval->changed = vk_swapinterval_changed;
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
    vk_saturation = Cvar_Get("gl_saturation", "1", CVAR_FILES);
    vk_invert = Cvar_Get("gl_invert", "0", CVAR_FILES);
    vk_gamma_scale_pics = Cvar_Get("gl_gamma_scale_pics", "0", CVAR_FILES);
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
    vk_lightgrid = Cvar_Get("vk_lightgrid", "1", 0);
    vk_gl_lightgrid = Cvar_Get("gl_lightgrid", "1", 0);
    vk_fullbright = Cvar_Get("r_fullbright", "0", CVAR_CHEAT);
    vk_cull_models = Cvar_Get("gl_cull_models", "1", 0);
    vk_shadows = Cvar_Get("gl_shadows", "0", CVAR_ARCHIVE);
    vk_stencilbits = Cvar_Get("gl_stencilbits", "8", CVAR_REFRESH);
    vk_dotshading = Cvar_Get("gl_dotshading", "1", 0);
    vk_draworder = Cvar_Get("gl_draworder", "1", 0);
    vk_showorigins = Cvar_Get("gl_showorigins", "0", CVAR_CHEAT);
    vk_showtearing = Cvar_Get("gl_showtearing", "0", CVAR_CHEAT);
    vk_showbloom = Cvar_Get("gl_showbloom", "0", CVAR_CHEAT);
    vk_waterwarp = Cvar_Get("gl_waterwarp", "0", 0);
    vk_bloom_sigma = Cvar_Get("gl_bloom_sigma", "4", 0);
    vk_bloom_downsample = Cvar_Get("vk_bloom_downsample", "4", 0);
    vk_glare = Cvar_Get("gl_glare", "1", CVAR_ARCHIVE);
    vk_glare_threshold = Cvar_Get("gl_glare_threshold", "0.3", 0);
    vk_glare_size = Cvar_Get("gl_glare_size", "24", 0);
    vk_glare_intensity = Cvar_Get("gl_glare_intensity", "0.5", 0);
    vk_perf_stats = Cvar_Get("vk_perf_stats", "0", 0);
    vk_frames_in_flight = Cvar_Get("vk_frames_in_flight", "2", CVAR_ARCHIVE);
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
    vk_znear = Cvar_Get("gl_znear", "2", CVAR_CHEAT);
    vk_drawworld = Cvar_Get("gl_drawworld", "1", CVAR_CHEAT);
    vk_novis = Cvar_Get("gl_novis", "0", 0);
    vk_lockpvs = Cvar_Get("gl_lockpvs", "0", CVAR_CHEAT);
    vk_lightmap = Cvar_Get("gl_lightmap", "0", CVAR_CHEAT);
    vk_pixel_lightmaps = Cvar_Get("vk_pixel_lightmaps", "0", 0);
    vk_vertexlight = Cvar_Get("gl_vertexlight", "0", 0);
    vk_nobind = Cvar_Get("gl_nobind", "0", CVAR_CHEAT);
    vk_clear = Cvar_Get("gl_clear", "0", 0);
    vk_clearcolor = Cvar_Get("gl_clearcolor", "black", 0);
    vk_clearcolor->generator = Com_Color_g;
    vk_polyblend = Cvar_Get("gl_polyblend", "1", 0);
    vk_damageblend_frac = Cvar_Get("gl_damageblend_frac", "0.2", 0);
    gl_bloom = Cvar_Get("gl_bloom", "0", 0);
    vk_world_textures = Cvar_Get("vk_world_textures", "1", 0);
    vk_world_vis = Cvar_Get("vk_world_vis", "1", 0);
    vk_cull_nodes = Cvar_Get("gl_cull_nodes", "1", 0);
    vk_world_cull = Cvar_Get("vk_world_cull", "1", 0);
#if USE_DEBUG
    vk_debug_distfrac = Cvar_Get("gl_debug_distfrac", "0.004", 0);
#endif

    if (!vid->init())
        return false;

    if (!vk_load_global() ||
        !vk_create_instance() ||
        !vk_create_surface() ||
        !vk_pick_physical_device() ||
        !vk_create_device() ||
        !vk_create_frame_resources()) {
        VKR_Shutdown(true);
        return false;
    }

    if (!vk_create_test_triangle())
        Com_WPrintf("Couldn't create Vulkan test triangle: %s\n", Com_GetLastError());
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
    if (!total)
        return;

    if (vk_swapinterval)
        vk_swapinterval->changed = NULL;
    if (vk_texturemode)
        vk_texturemode->changed = NULL;
    if (vk_texturemode)
        vk_texturemode->generator = NULL;
    if (vk_drawsky)
        vk_drawsky->changed = NULL;
    if (vk_gl_drawsky)
        vk_gl_drawsky->changed = NULL;
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
    vk_destroy_mesh(&vk.test_triangle);
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

    if (vk.rect_pipeline_layout) {
        vk.DestroyPipelineLayout(vk.device, vk.rect_pipeline_layout, NULL);
        vk.rect_pipeline_layout = VK_NULL_HANDLE;
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
    Com_Printf("Vulkan registration: %s\n", map && *map ? map : "<none>");
    r_registration_sequence++;
    memset(vk.flare_fracs, 0, sizeof(vk.flare_fracs));
    memset(vk.flare_times, 0, sizeof(vk.flare_times));
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
    else if (!handle)
        Com_DPrintf("Vulkan renderer skipped unsupported model %s\n", normalized);
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
    vk.sky_rotate = 0.0f;
    vk.sky_autorotate = false;
    VectorSet(vk.sky_axis, 0.0f, 0.0f, 1.0f);

    if (!name || !*name ||
        (vk_drawsky && !vk_drawsky->integer) ||
        (vk_gl_drawsky && !vk_gl_drawsky->integer))
        return;

    if (rotate && VectorNormalize2(axis, vk.sky_axis) >= 0.001f) {
        vk.sky_rotate = rotate;
        vk.sky_autorotate = autorotate;
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
    if (!fd)
        return;

    vk.fd = *fd;
    vk.fd_valid = true;
    if (!vk_dynamic_lights_enabled())
        vk.fd.num_dlights = 0;
    glr.fd = vk.fd;
    fd = &vk.fd;

    vk_rebuild_world_lighting();
    vk_setup_world_frustum(fd);
    vk_update_world_view(fd);
    vk_set_3d_viewport(fd);

    bool drawworld = !(fd->rdflags & RDF_NOWORLDMODEL) &&
        (!vk_drawworld || vk_drawworld->integer);
    bool world_marked = false;

    if (drawworld && vk_world_textures && vk_world_textures->integer &&
        vk_world_vis && vk_world_vis->integer) {
        vk_mark_world_faces(fd);
        world_marked = true;
    } else {
        vk.world.sky_visible = drawworld;
    }

    if (drawworld)
        vk_draw_skybox(fd);

    if (drawworld && vk.world.mesh.index_count) {
        mat4_t mvp;

        vk_world_mvp(mvp, fd);
        if (vk_world_textures && vk_world_textures->integer) {
            if (vk_world_vis && vk_world_vis->integer && !world_marked)
                vk_mark_world_faces(fd);
            vk_draw_world_mesh(mvp, false, vk.world_pipeline, VK_WORLD_OPAQUE,
                               1.0f, fd, NULL, NULL);
            vk_draw_world_outlines(mvp, false, VK_WORLD_OPAQUE, fd, NULL);
        } else {
            const float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            vk_draw_mesh(&vk.world.mesh, mvp, color);
            vk_draw_world_outlines(mvp, false, VK_WORLD_ENTITY_ALPHA, fd, NULL);
        }
    }

    vk_draw_entities(fd, VK_ENTITY_BMODEL);
    vk_draw_entities(fd, VK_ENTITY_OPAQUE);
    vk_draw_entities(fd, VK_ENTITY_ALPHA_BACK);
    vk_draw_entities(fd, VK_ENTITY_BMODEL_ALPHA);
    if (drawworld && vk.world.mesh.index_count &&
        vk_world_textures && vk_world_textures->integer) {
        mat4_t mvp;

        vk_world_mvp(mvp, fd);
        if (vk_world_vis && vk_world_vis->integer)
            vk_mark_world_faces(fd);
        vk_draw_world_mesh(mvp, false, vk.sprite_pipeline, VK_WORLD_ALPHA,
                           1.0f, fd, NULL, NULL);
        vk_draw_world_outlines(mvp, false, VK_WORLD_ALPHA, fd, NULL);
    }
    vk_draw_entities(fd, VK_ENTITY_BEAM);
    vk_draw_particles(fd);
    vk_draw_glare(fd);
    vk_draw_entities(fd, VK_ENTITY_ALPHA_FRONT);
    vk_draw_test_triangle(fd);
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
    byte a = Q_clip(alpha, 0.0f, 1.0f) * 255;
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

void VKR_DrawChar(int x, int y, int flags, int ch, qhandle_t font)
{
    int old_2d = c.batchesDrawn2D;

    if ((ch & 127) == 32)
        return;

    if (vk_fontshadow && vk_fontshadow->integer > 0)
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
        if (vk_fontshadow && vk_fontshadow->integer > 1) {
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
        const byte *src_pixel = src + y * src_rowbytes;
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
    VkClearValue clear[] = {
        {
            .color = color,
        },
        {
            .depthStencil = { .depth = 1.0f, .stencil = 0 },
        },
    };
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {
            .offset = { 0, 0 },
            .extent = { width, height },
        },
        .clearValueCount = q_countof(clear),
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

static void vk_finish_postprocess_scene(void)
{
    if (!vk.frame_bloom && !vk.frame_waterwarp)
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
            1.0f / (float)bloom_w,
            1.0f / (float)bloom_h,
            0.0f,
            1.0f,
        };
        float sigma = vk_bloom_sigma ? Cvar_ClampValue(vk_bloom_sigma, 1.0f, 25.0f) : 4.0f;
        sigma *= max((float)vk.fd.height, 1.0f) / 1080.0f;
        sigma = max(sigma, 1.0f) / 4.0f;
        vec4_t blur_x = {
            sigma / (float)bloom_w,
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
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        vk_begin_render_pass(vk.bloom_render_pass, vk.bloom_source_framebuffer, black);
        if (vk.fd_valid) {
            vk_draw_bloom_world_glowmaps(&vk.fd);
            vk_draw_bloom_source_entities(&vk.fd);
            vk_draw_bloom_beams(&vk.fd);
            vk_draw_bloom_only_entities(&vk.fd);
        }
        vk.CmdEndRenderPass(cmd);
        vk.render_pass_active = false;

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

        int iterations = gl_bloom ? Cvar_ClampInteger(gl_bloom, 1, 8) : 1;
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

    vk.frame_bloom = false;
    vk.frame_waterwarp = false;
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

    start = vk_time_usec();
    result = vk.AcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX,
                                     image_available, VK_NULL_HANDLE,
                                     &vk.current_image);
    vk.acquire_usec = vk_time_usec() - start;
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        vk_recreate_swapchain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        Com_EPrintf("vkAcquireNextImageKHR failed: Vulkan error %d\n", result);
        return;
    }

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

    vk_transition_image(cmd, vk.current_image,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    vk.frame_bloom = vk_bloom_enabled_for_frame();
    vk.frame_waterwarp = vk_waterwarp_enabled_for_frame();
    bool postprocess = vk.frame_bloom || vk.frame_waterwarp;
    if (postprocess) {
        vk_transition_scene(cmd,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }

    VkClearValue clear[] = {
        {
            .color = vk_frame_clear_color(),
        },
        {
            .depthStencil = { .depth = 1.0f, .stencil = 0 },
        },
    };
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk.render_pass,
        .framebuffer = postprocess ? vk.scene_framebuffer : vk.framebuffers[vk.current_image],
        .renderArea = {
            .offset = { 0, 0 },
            .extent = vk.swapchain_extent,
        },
        .clearValueCount = q_countof(clear),
        .pClearValues = clear,
    };

    vk.CmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vk.render_pass_active = true;


    vk.frame_active = true;
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
            1.0f / (float)bloom_w,
            1.0f / (float)bloom_h,
            0.0f,
            1.0f,
        };
        float sigma = vk_bloom_sigma ? Cvar_ClampValue(vk_bloom_sigma, 1.0f, 25.0f) : 4.0f;
        sigma *= max((float)vk.fd.height, 1.0f) / 1080.0f;
        sigma = max(sigma, 1.0f) / 4.0f;
        vec4_t blur_x = {
            sigma / (float)bloom_w,
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
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        vk_begin_render_pass(vk.bloom_render_pass, vk.bloom_source_framebuffer, black);
        if (vk.fd_valid) {
            vk_draw_bloom_world_glowmaps(&vk.fd);
            vk_draw_bloom_source_entities(&vk.fd);
            vk_draw_bloom_beams(&vk.fd);
            vk_draw_bloom_only_entities(&vk.fd);
        }
        vk.CmdEndRenderPass(cmd);
        vk.render_pass_active = false;

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

        int iterations = gl_bloom ? Cvar_ClampInteger(gl_bloom, 1, 8) : 1;
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
        vk_recreate_swapchain();
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

    vk_recreate_swapchain();
}

bool VKR_VideoSync(void)
{
    VkFence frame_fence = vk.frame_fence[vk.frame_index];

    if (!frame_fence)
        return true;

    return vk.WaitForFences(vk.device, 1, &frame_fence, VK_TRUE, 0) == VK_SUCCESS;
}

r_opengl_config_t VKR_GetGLConfig(void)
{
    return (r_opengl_config_t){ 0 };
}

#endif
