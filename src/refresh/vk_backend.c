/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "vk_local.h"

#if USE_VULKAN

#include "vk_spirv.h"

vk_state_t vk;
cvar_t *vk_drawentities;
cvar_t *vk_drawsky;
cvar_t *vk_swapinterval;
cvar_t *vk_present_mode;
bool vk_present_mode_warned;
static cvar_t *vk_finish;
cvar_t *vk_texturemode;
cvar_t *vk_anisotropy;
cvar_t *vk_round_down;
cvar_t *vk_picmip;
cvar_t *vk_downsample_skins;
cvar_t *vk_bilerp_chars;
cvar_t *vk_bilerp_pics;
cvar_t *vk_bilerp_skies;
static cvar_t *vk_cubemaps;
cvar_t *vk_saturation;
cvar_t *vk_invert;
static cvar_t *vk_gamma;
cvar_t *vk_gamma_scale_pics;
static cvar_t *vk_upscale_pcx;
#if USE_MD5
cvar_t *vk_md5_load;
cvar_t *vk_md5_use;
cvar_t *vk_md5_distance;
#endif
cvar_t *vk_partscale;
cvar_t *vk_partstyle;
cvar_t *vk_partshape;
cvar_t *vk_beamstyle;
cvar_t *vk_flarespeed;
cvar_t *vk_lightgrid;
cvar_t *vk_fullbright;
cvar_t *vk_cull_models;
cvar_t *vk_shadows;
static cvar_t *vk_stencilbits;
cvar_t *vk_celshading;
cvar_t *vk_dotshading;
cvar_t *vk_draworder;
cvar_t *vk_showorigins;
static cvar_t *vk_showtearing;
cvar_t *vk_showbloom;
cvar_t *vk_waterwarp;
cvar_t *vk_bloom_sigma;
cvar_t *vk_bloom_lamps;
cvar_t *vk_bloom_downsample;
static cvar_t *vk_perf_stats;
cvar_t *vk_frames_in_flight;
cvar_t *vk_device;
cvar_t *vk_devicelist;
cvar_t *vk_raytracing;
cvar_t *vk_rt_emissive;
cvar_t *vk_rt_ao;
cvar_t *vk_rt_skylight;
cvar_t *vk_rt_environment;
cvar_t *vk_rt_atmosphere;
cvar_t *vk_rt_underwater_absorption;
cvar_t *vk_rt_light_scattering;
cvar_t *vk_rt_fog_turbulence;
cvar_t *vk_rt_dust;
cvar_t *vk_rt_dust_lighting;
cvar_t *vk_rt_emissive_motes;
cvar_t *vk_rt_afterglow;
cvar_t *vk_rt_sunlight;
cvar_t *vk_rt_emissive_halo;
cvar_t *vk_rt_emissive_flicker;
cvar_t *vk_rt_reflections;
cvar_t *vk_rt_reflection_glints;
cvar_t *vk_rt_reflection_dispersion;
cvar_t *vk_rt_specular;
cvar_t *vk_rt_liquids;
cvar_t *vk_rt_bounce;
cvar_t *vk_rt_caustics;
cvar_t *vk_rt_shadow_fringe;
cvar_t *vk_rt_light_ripples;
cvar_t *vk_rt_rail_impact;
cvar_t *vk_rt_light_coronas;
cvar_t *vk_rt_lava_embers;
cvar_t *vk_rt_shockwaves;
cvar_t *vk_rt_impact_marks;
cvar_t *vk_rt_splash_ripples;
cvar_t *vk_rt_water_wakes;
cvar_t *vk_rt_teleport_vortex;
cvar_t *vk_rt_item_respawn;
cvar_t *vk_rt_electric_filaments;
cvar_t *vk_rt_landing_dust;
cvar_t *vk_rt_energy_collapse;
cvar_t *vk_rt_debug;
#if USE_DEBUG
static cvar_t *vk_showstats;
#endif
cvar_t *vk_modulate;
cvar_t *vk_modulate_entities;
cvar_t *vk_doublelight_entities;
cvar_t *vk_fontshadow;
cvar_t *vk_modulate_world;
cvar_t *vk_coloredlightmaps;
cvar_t *vk_dynamic;
cvar_t *vk_dlight_falloff;
cvar_t *vk_brightness;
cvar_t *vk_fog;
cvar_t *vk_intensity;
static cvar_t *vk_shaders;
static cvar_t *vk_znear;
static cvar_t *vk_drawworld;
cvar_t *vk_novis;
cvar_t *vk_lockpvs;
cvar_t *vk_lightmap;
cvar_t *vk_pixel_lightmaps;
cvar_t *vk_vertexlight;
cvar_t *vk_nobind;
static cvar_t *vk_clear;
static cvar_t *vk_clearcolor;
static cvar_t *vk_polyblend;
static cvar_t *vk_damageblend_frac;
cvar_t *vk_cull_nodes;
#if USE_DEBUG
static cvar_t *vk_debug_distfrac;
static cvar_t *vk_debug_linewidth;
#endif
byte vk_gammatable[256];
bool vk_pixel_lightmaps_warned;

static void vk_finish_screenshot(int status);
#if USE_VULKAN_RAYTRACING
#endif

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

static const image_upload_t vk_image_upload = {
    .load = vk_upload_image,
    .unload = vk_unload_image,
    .glowmaps = true,
};

// VERSION_NUMBERS is "major,minor,patch,0" (see meson.build); Vulkan takes the
// first three.

bool vk_fail_result(const char *what, VkResult result)
{
    Com_SetLastError(va("%s failed: Vulkan error %d", what, result));
    return false;
}

uint32_t vk_find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags properties)
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

bool vk_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
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

bool vk_depth_format_has_stencil(VkFormat format)
{
    return format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

const char *vk_format_name(VkFormat format)
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

bool vk_shadow_stencil_requested(void)
{
    return vk_stencilbits && Cvar_ClampInteger(vk_stencilbits, 0, 8) > 0;
}

bool vk_shadow_stencil_enabled(void)
{
    return vk_shadow_stencil_requested() &&
        vk_depth_format_has_stencil(vk.depth_format);
}

VkFormat vk_choose_depth_format(bool stencil)
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

VkSampleCountFlagBits vk_choose_sample_count(void)
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

bool vk_begin_immediate(VkCommandBuffer *cmd)
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

bool vk_end_immediate(VkCommandBuffer cmd)
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

void vk_destroy_buffer(vk_buffer_t *buffer)
{
    if (!vk.device)
        return;

    if (buffer->buffer)
        vk.DestroyBuffer(vk.device, buffer->buffer, NULL);
    if (buffer->memory)
        vk.FreeMemory(vk.device, buffer->memory, NULL);

    memset(buffer, 0, sizeof(*buffer));
}

void vk_unmap_world_batch_indices(void)
{
    if (vk.device && vk.world.batch_index_mapped) {
        vk.UnmapMemory(vk.device, vk.world.batch_indices.memory);
        vk.world.batch_index_mapped = NULL;
    }
}

bool vk_upload_buffer(vk_buffer_t *dst, const void *data,
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

void vk_destroy_mesh(vk_mesh_t *mesh)
{
    vk_destroy_buffer(&mesh->vertices);
    vk_destroy_buffer(&mesh->indices);
    mesh->vertex_count = 0;
    mesh->index_count = 0;
}

void vk_free_world(void)
{
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
    vk.world.lava_ember_anchor_count = 0;
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

bool vk_upload_mesh(vk_mesh_t *mesh, const vk_vertex_t *vertices,
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

uint32_t *vk_build_line_indices(const uint32_t *indices,
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

bool vk_create_color_target(vk_texture_t *texture, uint32_t width,
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

uint32_t vk_mip_level_count(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;

    while (width > 1 || height > 1) {
        width = max(width >> 1, 1);
        height = max(height >> 1, 1);
        levels++;
    }

    return levels;
}

int vk_upscale_level(uint32_t width, uint32_t height,
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

void vk_mip_map(byte *out, const byte *in, uint32_t width, uint32_t height)
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
    } else if (vk.swapchain_layouts[image_index] == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
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

void vk_transition_color_target(VkCommandBuffer cmd, vk_texture_t *texture,
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

void vk_transition_scene(VkCommandBuffer cmd, VkImageLayout new_layout,
                                VkAccessFlags dst_access,
                                VkPipelineStageFlags dst_stage)
{
    vk_transition_color_target(cmd, &vk.scene_texture, &vk.scene_layout,
                               new_layout, dst_access, dst_stage);
}

void vk_transition_depth(VkCommandBuffer cmd, VkImageLayout new_layout,
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

VkClearColorValue vk_frame_clear_color(void)
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

void vk_bind_texture_descriptor(VkCommandBuffer cmd, VkDescriptorSet set)
{
    if (vk.bound_texture_descriptor == set)
        return;

    vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             vk.rect_pipeline_layout, 0, 1, &set, 0, NULL);
    vk.bound_texture_descriptor = set;
    c.texSwitches++;
}

void vk_bind_pixel_world_descriptor(VkCommandBuffer cmd,
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

void vk_bind_pipeline(VkCommandBuffer cmd, VkPipelineBindPoint bind_point,
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

void vk_reset_bind_cache(void)
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

// Microsecond clock for the performance statistics. 32-bit MinGW doesn't
// provide clock_gettime, so Windows uses the performance counter.
uint64_t vk_time_usec(void)
{
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;

    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart / frequency.QuadPart) * 1000000 +
           (uint64_t)(counter.QuadPart % frequency.QuadPart) * 1000000 /
           frequency.QuadPart;
#else
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

void vk_push_constants(VkCommandBuffer cmd, uint32_t size,
                              const void *data)
{
    vk.CmdPushConstants(cmd, vk.rect_pipeline_layout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, size, data);
    c.uniformUploads++;
}

void vk_push_pixel_world_constants(VkCommandBuffer cmd, uint32_t size,
                                          const void *data)
{
    vk.CmdPushConstants(cmd, vk.pixel_world_pipeline_layout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, size, data);
    c.uniformUploads++;
}

void vk_bind_vertex_buffers(VkCommandBuffer cmd, uint32_t first,
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

void vk_bind_index_buffer(VkCommandBuffer cmd, VkBuffer buffer,
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

void vk_count_batch3d(void)
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

int vk_2d_width(void)
{
    int width = r_config.width > 0 ? r_config.width : (int)vk.swapchain_extent.width;

    if (vk.scale > 0.0f && vk.scale != 1.0f)
        width = Q_rint(width * vk.scale);

    return max(width, 1);
}

int vk_2d_height(void)
{
    int height = r_config.height > 0 ? r_config.height : (int)vk.swapchain_extent.height;

    if (vk.scale > 0.0f && vk.scale != 1.0f)
        height = Q_rint(height * vk.scale);

    return max(height, 1);
}

void vk_set_3d_viewport(const refdef_t *fd)
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

void vk_clear_rect(int x, int y, int w, int h, uint32_t color)
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

void vk_draw_texture_resource(int x, int y, int w, int h,
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

bool vk_alias_model_has_glowmap(const vk_model_t *model)
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

static float vk_projection_zfar(int rdflags)
{
    if ((rdflags & RDF_NOWORLDMODEL) || vk.world.size <= 0.0f)
        return 2048.0f;

    return vk.world.size * 2.0f;
}

void vk_projection_matrix(mat4_t m, float fov_x, float fov_y, int rdflags)
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

void vk_view_matrix(mat4_t matrix, const refdef_t *fd)
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

// Left-handed view weapons are mirrored, which reverses their winding.
bool vk_entity_mirrored(const entity_t *ent)
{
    extern cvar_t *cl_gun;
    extern cvar_t *info_hand;

    if (!(ent->flags & RF_WEAPONMODEL))
        return false;

    return (info_hand && cl_gun && info_hand->integer == 1 && cl_gun->integer == 1) ||
           (cl_gun && cl_gun->integer == 3);
}

static void vk_entity_projection_matrix(mat4_t matrix, const refdef_t *fd,
                                        const entity_t *ent)
{
    extern cvar_t *cl_adjustfov;
    extern cvar_t *cl_gunfov;
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

        if (vk_entity_mirrored(ent))
            reflect_x = -1.0f;
    }

    vk_projection_matrix(matrix, fov_x, fov_y, fd->rdflags);
    matrix[0] *= reflect_x;
}

void vk_matrix_multiply(mat4_t out, const mat4_t a, const mat4_t b)
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

void vk_entity_model_matrix(mat4_t model, const entity_t *ent,
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

void vk_world_mvp(mat4_t out, const refdef_t *fd)
{
    mat4_t proj, view;

    vk_projection_matrix(proj, fd->fov_x, fd->fov_y, fd->rdflags);
    vk_view_matrix(view, fd);
    vk_matrix_multiply(out, proj, view);
}

void vk_entity_axis(const entity_t *ent, vec3_t axis[3])
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

void vk_entity_mvp(mat4_t out, const refdef_t *fd,
                          const entity_t *ent, const vec3_t axis[3])
{
    mat4_t proj, view, model, view_model;

    vk_entity_model_matrix(model, ent, axis);

    vk_entity_projection_matrix(proj, fd, ent);
    vk_view_matrix(view, fd);
    vk_matrix_multiply(view_model, view, model);
    vk_matrix_multiply(out, proj, view_model);
}

void vk_model_mvp(mat4_t out, const refdef_t *fd, const mat4_t model)
{
    mat4_t proj, view, view_model;

    vk_projection_matrix(proj, fd->fov_x, fd->fov_y, fd->rdflags);
    vk_view_matrix(view, fd);
    vk_matrix_multiply(view_model, view, model);
    vk_matrix_multiply(out, proj, view_model);
}

vk_alias_lerp_t vk_alias_lerp_for_entity(const vk_model_t *model,
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

bool vk_alias_model_culled(const vk_model_t *model, const entity_t *ent,
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
    VkDeviceSize size = sizeof(vk_vertex_t) * VK_MAX_DEBUG_LINE_VERTICES;

    vk_destroy_buffer(&vk.debug_lines);
    if (!vk_create_buffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &vk.debug_lines.buffer, &vk.debug_lines.memory))
        return false;

    // vk_draw_debug_lines() maps the buffer by this size.
    vk.debug_lines.size = size;
    return true;
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
    vk.debug_text_vertices.size = sizeof(vk_vertex_t) * VK_MAX_DEBUG_TEXT_VERTICES;

    if (!vk_create_buffer(sizeof(uint32_t) * VK_MAX_DEBUG_TEXT_INDICES,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &vk.debug_text_indices.buffer,
                          &vk.debug_text_indices.memory)) {
        vk_destroy_buffer(&vk.debug_text_vertices);
        return false;
    }
    vk.debug_text_indices.size = sizeof(uint32_t) * VK_MAX_DEBUG_TEXT_INDICES;

    return true;
}
#endif

static void vk_partshape_changed(cvar_t *self)
{
    if (vk.device && !vk_create_particle_texture()) {
        Com_WPrintf("Couldn't recreate Vulkan particle texture: %s\n",
                    Com_GetLastError());
    }
}

void vk_draw_null_model(const entity_t *ent, const refdef_t *fd)
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

// vk_perf_stats 2: prints frames that took much longer than the frames
// before them, with where the time went, to find what causes stutter. A
// frame whose own times are small spent its time outside the renderer:
// in the client, the game, the network or the system.
static void vk_log_perf_spike(void)
{
    static float avg_frame;
    static int frames;
    float frame = vk.fd.frametime * 1000000.0f;

    // start over after loading, or anything else that stops the game
    if (frame <= 0 || frame > 1000000.0f) {
        frames = 0;
        return;
    }
    if (!frames++)
        avg_frame = frame;

    if (frames > 60 && frame > 2 * avg_frame && frame > avg_frame + 4000) {
        // what the renderer's own steps don't account for was lost
        // elsewhere: in the client, the game, the network or the system
        float renderer = vk.wait_usec + vk.acquire_usec + vk.record_usec +
                         vk.submit_usec + vk.present_usec;
        Com_Printf("VK spike %.1fs: %.1f ms (usual %.1f), outside %.1f, pres %.1f, "
                   "wait %.1f, rec %.1f, gpu %.1f, acq %.1f, sub %.1f, "
                   "dl %i, part %i, ent %i, tris %i\n",
                   vk.fd.time, frame / 1000, avg_frame / 1000,
                   max(frame - renderer, 0.0f) / 1000, vk.present_usec / 1000.0f,
                   vk.wait_usec / 1000.0f, vk.record_usec / 1000.0f,
                   vk.gpu_frame_usec / 1000.0f, vk.acquire_usec / 1000.0f,
                   vk.submit_usec / 1000.0f, vk.fd.num_dlights,
                   vk.fd.num_particles, vk.fd.num_entities, c.trisDrawn);
    }

    // the usual frame, over about the last hundred; a spike moves it little
    avg_frame += (frame - avg_frame) * 0.01f;
}

static void vk_log_perf_stats(void)
{
    if (!vk_perf_stats || !vk_perf_stats->integer || !vk.fd_valid)
        return;
    if (vk_perf_stats->integer == 2) {
        vk_log_perf_spike();
        return;
    }
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

    // Reject non-Vulkan window backends before they create native resources.
    // In particular, the GLX-only X11 backend must not be initialized while
    // probing video drivers for Vulkan before falling back to SDL.
    if (!vid->get_vk_instance_extensions || !vid->create_vk_surface) {
        Com_SetLastError("Video driver doesn't support Vulkan");
        return false;
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
    vk_bloom_lamps = Cvar_Get("gl_bloom_lamps", "1", CVAR_ARCHIVE);
    vk_bloom_downsample = Cvar_Get("vk_bloom_downsample", "4", 0);
    vk_perf_stats = Cvar_Get("vk_perf_stats", "0", 0);
    vk_frames_in_flight = Cvar_Get("vk_frames_in_flight", "2", CVAR_ARCHIVE);
    vk_device = Cvar_Get("vk_device", "", CVAR_ARCHIVE | CVAR_REFRESH);
    vk_devicelist = Cvar_Get("vk_devicelist", "\"automatic\" \"\"", CVAR_ROM);
    vk_raytracing = Cvar_Get("vk_raytracing", "0", CVAR_ARCHIVE | CVAR_REFRESH);
    vk_rt_emissive = Cvar_Get("vk_rt_emissive", "0.35", CVAR_ARCHIVE);
    vk_rt_ao = Cvar_Get("vk_rt_ao", "0.24", CVAR_ARCHIVE);
    vk_rt_skylight = Cvar_Get("vk_rt_skylight", "0", CVAR_ARCHIVE);
    vk_rt_environment = Cvar_Get("vk_rt_environment", "0.65", CVAR_ARCHIVE);
    vk_rt_atmosphere = Cvar_Get("vk_rt_atmosphere", "0.45", CVAR_ARCHIVE);
    vk_rt_underwater_absorption = Cvar_Get("vk_rt_underwater_absorption",
                                           "0.65", CVAR_ARCHIVE);
    vk_rt_light_scattering = Cvar_Get("vk_rt_light_scattering", "0.5",
                                      CVAR_ARCHIVE);
    vk_rt_fog_turbulence = Cvar_Get("vk_rt_fog_turbulence", "0.4",
                                    CVAR_ARCHIVE);
    vk_rt_dust = Cvar_Get("vk_rt_dust", "0.35", CVAR_ARCHIVE);
    vk_rt_dust_lighting = Cvar_Get("vk_rt_dust_lighting", "0.65",
                                   CVAR_ARCHIVE);
    vk_rt_emissive_motes = Cvar_Get("vk_rt_emissive_motes", "0.45",
                                    CVAR_ARCHIVE);
    vk_rt_afterglow = Cvar_Get("vk_rt_afterglow", "0.35", CVAR_ARCHIVE);
    vk_rt_sunlight = Cvar_Get("vk_rt_sunlight", "0.65", CVAR_ARCHIVE);
    vk_rt_emissive_halo = Cvar_Get("vk_rt_emissive_halo", "0.65",
                                   CVAR_ARCHIVE);
    vk_rt_emissive_flicker = Cvar_Get("vk_rt_emissive_flicker", "0.55",
                                      CVAR_ARCHIVE);
    vk_rt_reflections = Cvar_Get("vk_rt_reflections", "0.35", CVAR_ARCHIVE);
    vk_rt_reflection_glints = Cvar_Get("vk_rt_reflection_glints", "0.55",
                                       CVAR_ARCHIVE);
    vk_rt_reflection_dispersion = Cvar_Get("vk_rt_reflection_dispersion",
                                           "0.6", CVAR_ARCHIVE);
    vk_rt_specular = Cvar_Get("vk_rt_specular", "0.45", CVAR_ARCHIVE);
    vk_rt_liquids = Cvar_Get("vk_rt_liquids", "0.65", CVAR_ARCHIVE);
    vk_rt_bounce = Cvar_Get("vk_rt_bounce", "0.65", CVAR_ARCHIVE);
    vk_rt_caustics = Cvar_Get("vk_rt_caustics", "0.65", CVAR_ARCHIVE);
    vk_rt_shadow_fringe = Cvar_Get("vk_rt_shadow_fringe", "0.6", CVAR_ARCHIVE);
    vk_rt_light_ripples = Cvar_Get("vk_rt_light_ripples", "0.6", CVAR_ARCHIVE);
    vk_rt_rail_impact = Cvar_Get("vk_rt_rail_impact", "0", CVAR_ARCHIVE);
    vk_rt_light_coronas = Cvar_Get("vk_rt_light_coronas", "0.6", CVAR_ARCHIVE);
    vk_rt_lava_embers = Cvar_Get("vk_rt_lava_embers", "0.65", CVAR_ARCHIVE);
    vk_rt_shockwaves = Cvar_Get("vk_rt_shockwaves", "0.7", CVAR_ARCHIVE);
    vk_rt_impact_marks = Cvar_Get("vk_rt_impact_marks", "0", CVAR_ARCHIVE);
    vk_rt_splash_ripples = Cvar_Get("vk_rt_splash_ripples", "0.75",
                                    CVAR_ARCHIVE);
    vk_rt_water_wakes = Cvar_Get("vk_rt_water_wakes", "0.75",
                                 CVAR_ARCHIVE);
    vk_rt_teleport_vortex = Cvar_Get("vk_rt_teleport_vortex", "0.8",
                                     CVAR_ARCHIVE);
    vk_rt_item_respawn = Cvar_Get("vk_rt_item_respawn", "0.8",
                                  CVAR_ARCHIVE);
    vk_rt_electric_filaments = Cvar_Get("vk_rt_electric_filaments", "0.75",
                                        CVAR_ARCHIVE);
    vk_rt_landing_dust = Cvar_Get("vk_rt_landing_dust", "0.7", CVAR_ARCHIVE);
    vk_rt_energy_collapse = Cvar_Get("vk_rt_energy_collapse", "0.8",
                                     CVAR_ARCHIVE);
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
    r_lava_glowmaps = Cvar_Get("r_lava_glowmaps", "1", CVAR_ARCHIVE);
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
    gl_bloom = Cvar_Get("gl_bloom", "0", CVAR_ARCHIVE);
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
    if (!vk_create_shockwave_texture())
        Com_WPrintf("Couldn't create Vulkan shockwave texture: %s\n",
                    Com_GetLastError());
    if (!vk_create_impact_texture())
        Com_WPrintf("Couldn't create Vulkan impact texture: %s\n",
                    Com_GetLastError());

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
        vk_free_snow_cover();
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

    // A screenshot waiting for the next frame won't get one.
    if (IMG_PendingScreenshot())
        vk_finish_screenshot(Q_ERR(ECANCELED));

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
    vk_destroy_texture_resource(&vk.shockwave_texture);
    vk_destroy_texture_resource(&vk.impact_texture);

    vk_destroy_swapchain();

    if (vk.device && vk.DeviceWaitIdle)
        vk.DeviceWaitIdle(vk.device);

    if (vk.timestamp_query_pool) {
        vk.DestroyQueryPool(vk.device, vk.timestamp_query_pool, NULL);
        vk.timestamp_query_pool = VK_NULL_HANDLE;
    }

    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++) {
        if (vk.frame_fence[i]) {
            vk.DestroyFence(vk.device, vk.frame_fence[i], NULL);
            vk.frame_fence[i] = VK_NULL_HANDLE;
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
    vk_free_snow_cover();
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
    vk_free_snow_cover();
    vk_load_world(map);
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
    if (vk.view_liquid_kind == 1)
        vk.view_water_top = vk_view_water_top(&vk.fd);
    vk_update_water_fade(&vk.fd);
#else
    vk.view_liquid_kind = 0;
    vk.water_fade = 0.0f;
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
    vk_draw_snow_cover(fd);
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

// Screenshots are taken from the swapchain image of the next frame, copied
// just before it is presented. Between frames the last image already belongs
// to the presentation engine and must not be read
// (UNASSIGNED-non-acquired-swapchain-image-used).
int VKR_ReadPixels(screenshot_t *s)
{
    int offsets[3];

    if (!vk.swapchain || !vk.swapchain_transfer_src ||
        !vk.swapchain_images || !vk.swapchain_layouts)
        return Q_ERR(ENOTSUP);
    if (vk.frame_active)
        return Q_ERR(EBUSY);
    if (!vk_screenshot_format_offsets(offsets))
        return Q_ERR(ENOTSUP);

    return Q_ERR(EAGAIN);
}

static void vk_finish_screenshot(int status)
{
    if (vk.screenshot_mapped)
        vk.UnmapMemory(vk.device, vk.screenshot_readback.memory);
    vk.screenshot_mapped = NULL;
    vk_destroy_buffer(&vk.screenshot_readback);
    vk.screenshot_recorded = false;
    IMG_FinishPendingScreenshot(status);
}

// Records a copy of the current swapchain image into a readback buffer when
// a screenshot is waiting. Called right before the transition to present.
static void vk_record_screenshot(VkCommandBuffer cmd)
{
    int offsets[3];

    if (!IMG_PendingScreenshot() || vk.screenshot_recorded)
        return;
    if (!vk.swapchain_transfer_src || !vk_screenshot_format_offsets(offsets)) {
        vk_finish_screenshot(Q_ERR(ENOTSUP));
        return;
    }

    uint32_t width = vk.swapchain_extent.width;
    uint32_t height = vk.swapchain_extent.height;
    if (!width || !height || width > INT_MAX / 4 ||
        height > INT_MAX / (width * 4)) {
        vk_finish_screenshot(Q_ERR(EOVERFLOW));
        return;
    }

    VkDeviceSize size = (VkDeviceSize)width * height * 4;
    if (!vk_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &vk.screenshot_readback.buffer,
                          &vk.screenshot_readback.memory)) {
        vk_finish_screenshot(Q_ERR_FAILURE);
        return;
    }
    vk.screenshot_readback.size = size;
    vk.screenshot_width = width;
    vk.screenshot_height = height;

    vk_transition_image(cmd, vk.current_image,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copy = {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .imageExtent = { width, height, 1 },
    };
    vk.CmdCopyImageToBuffer(cmd, vk.swapchain_images[vk.current_image],
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            vk.screenshot_readback.buffer, 1, &copy);

    VkBufferMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = vk.screenshot_readback.buffer,
        .size = VK_WHOLE_SIZE,
    };
    vk.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL,
                          1, &barrier, 0, NULL);

    vk.screenshot_recorded = true;
}

// Waits for the frame with the copy and hands the pixels to the screenshot
// code, which writes the file.
static void vk_complete_screenshot(VkFence fence)
{
    int offsets[3];
    void *mapped = NULL;

    if (!vk.screenshot_recorded)
        return;

    VkResult result = vk.WaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkWaitForFences", result);
        vk_finish_screenshot(Q_ERR_FAILURE);
        return;
    }

    result = vk.MapMemory(vk.device, vk.screenshot_readback.memory, 0,
                          vk.screenshot_readback.size, 0, &mapped);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkMapMemory", result);
        vk_finish_screenshot(Q_ERR_FAILURE);
        return;
    }
    vk.screenshot_mapped = mapped;

    screenshot_t *s = IMG_PendingScreenshot();
    if (!s || !vk_screenshot_format_offsets(offsets)) {
        vk_finish_screenshot(Q_ERR(ENOTSUP));
        return;
    }

    int width = vk.screenshot_width;
    int height = vk.screenshot_height;
    int src_rowbytes = width * 4;
    int dst_rowbytes = width * 3;

    s->bpp = 3;
    s->rowbytes = dst_rowbytes;
    s->pixels = R_Malloc(dst_rowbytes * height);
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

    vk_finish_screenshot(Q_ERR_SUCCESS);
}

void vk_begin_render_pass_sized(VkRenderPass render_pass,
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

void vk_begin_render_pass(VkRenderPass render_pass, VkFramebuffer framebuffer,
                                 VkClearColorValue color)
{
    vk_begin_render_pass_sized(render_pass, framebuffer, color,
                               vk.swapchain_extent.width,
                               vk.swapchain_extent.height);
}

void VKR_BeginFrame(void)
{
    memset(&c, 0, sizeof(c));

    // Lets the client skip effects that only ray tracing draws. Ray tracing
    // can be switched off at run time when something fails, so this is kept
    // current every frame.
    if (vk.raytracing_active)
        r_config.flags |= QVF_RAYTRACING;
    else
        r_config.flags &= ~QVF_RAYTRACING;
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
        bool enable_mrt = vk_mrt_requested() && vk_bloom_supported();
        gl_bloom->modified = false;
        if (enable_mrt != vk.mrt_bloom &&
            !vk_recreate_swapchain("bloom configuration change"))
            return;
    }
    if (vk_rt_reflections && vk_rt_reflections->modified) {
        bool enable_mrt = vk_mrt_requested() && vk_bloom_supported();
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
    }
    // The render pass has the bloom source as its second color attachment
    // whenever bloom is set up, on frames without bloom too (menus, loading),
    // and expects it in attachment layout.
    if (vk.mrt_bloom) {
        vk_transition_color_target(cmd, &vk.bloom_source_texture,
                                   &vk.bloom_source_layout,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
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

    // Frames without a 3D view still have to show the scene target that
    // VKR_BeginFrame() chose; VKR_RenderFrame() has already done this
    // otherwise and cleared the flags.
    vk_finish_postprocess_scene();

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

    vk_record_screenshot(cmd);

    vk_transition_image(cmd, vk.current_image,
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        0,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    uint64_t start = vk_time_usec();
    VkResult result = vk.EndCommandBuffer(cmd);
    vk.record_usec = vk_time_usec() - vk.frame_start_usec;
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkEndCommandBuffer failed: Vulkan error %d\n", result);
        if (vk.screenshot_recorded)
            vk_finish_screenshot(Q_ERR_FAILURE);
        vk.frame_active = false;
        return;
    }

    VkSemaphore image_available = vk.image_available[vk.frame_index];
    VkSemaphore render_finished = vk.render_finished[vk.current_image];
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
        if (vk.screenshot_recorded)
            vk_finish_screenshot(Q_ERR_FAILURE);
        vk.frame_active = false;
        return;
    }

    start = vk_time_usec();
    result = vk.QueueSubmit(vk.graphics_queue, 1, &submit_info, frame_fence);
    vk.submit_usec = vk_time_usec() - start;
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkQueueSubmit failed: Vulkan error %d\n", result);
        if (vk.screenshot_recorded)
            vk_finish_screenshot(Q_ERR_FAILURE);
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

    vk_complete_screenshot(frame_fence);

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
