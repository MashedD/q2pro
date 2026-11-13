/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan textures: uploads, samplers, descriptors, cubemaps and built-in textures.

#include "vk_local.h"

#if USE_VULKAN

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

void vk_upload_image(image_t *image, byte *pic)
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

void vk_unload_image(image_t *image)
{
    if (image->flags & IF_CUBEMAP) {
        vk_unload_cubemap(image);
        return;
    }

    vk_destroy_texture(image);
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

const vk_texture_t *vk_texture_for_index(unsigned index, bool allow_nobind)
{
    if (allow_nobind && vk_nobind && vk_nobind->integer &&
        vk.textures[0].descriptor_set) {
        return &vk.textures[0];
    }

    if (index >= MAX_RIMAGES || !vk.textures[index].descriptor_set)
        return NULL;

    return &vk.textures[index];
}

void vk_texture_barrier(VkCommandBuffer cmd, VkImage image,
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

void vk_update_texture_descriptor_with_sampler(vk_texture_t *texture,
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

void vk_update_texture_descriptor(vk_texture_t *texture)
{
    vk_update_texture_descriptor_with_sampler(texture, vk.sampler);
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

    if (image->type == IT_WALL || image->type == IT_SKIN)
        return vk.sampler;

    return vk.postprocess_sampler ? vk.postprocess_sampler : vk.sampler;
}

VkSampler vk_sampler_for_pic_flags(imageflags_t flags)
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

bool vk_upload_texture_data(vk_texture_t *texture, uint32_t width,
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

vk_cubemap_t *vk_find_cubemap(const image_t *image)
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

void vk_unload_cubemap(image_t *image)
{
    vk_cubemap_t *cubemap = vk_find_cubemap(image);
    if (cubemap)
        vk_clear_cubemap(cubemap);
    image->texnum = image->texnum2 = 0;
}

bool vk_upload_cubemap(image_t *image, const byte *pic)
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

bool vk_upload_texture(image_t *image, byte *pic)
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

void vk_destroy_texture_resource(vk_texture_t *texture)
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

void vk_destroy_texture(image_t *image)
{
    unsigned index = image->texnum;
    unsigned index2 = image->texnum2;

    if (index && index < MAX_RIMAGES)
        vk_destroy_texture_resource(&vk.textures[index]);
    if (index2 && index2 < MAX_RIMAGES && index2 != index)
        vk_destroy_texture_resource(&vk.textures[index2]);
    image->texnum = image->texnum2 = 0;
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

bool vk_create_sampler(VkSampler *sampler)
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

bool vk_create_sky_sampler(VkSampler *sampler)
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

bool vk_create_postprocess_sampler(VkSampler *sampler)
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

bool vk_create_nearest_sampler(VkSampler *sampler,
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

void vk_texturemode_changed(cvar_t *self)
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

void vk_texturemode_g(genctx_t *ctx)
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

void vk_sampler_selection_changed(cvar_t *self)
{
    (void)self;

    if (!vk.device)
        return;

    vk_update_texture_descriptors();
}

bool vk_create_particle_texture(void)
{
    int shape = vk_partshape ? Cvar_ClampInteger(vk_partshape, 0, 2) : 0;
    // Keep the legacy hard-edged shape at its native 16x16 size. Smooth
    // shapes are generated at a higher resolution, so their alpha texels
    // don't show when large particles magnify them.
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

bool vk_create_beam_texture(void)
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

bool vk_create_shockwave_texture(void)
{
    enum { SIZE = 128 };
    uint32_t *pixels = Z_Malloc(sizeof(*pixels) * SIZE * SIZE);

    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            float fx = (x + 0.5f - SIZE * 0.5f) / (SIZE * 0.5f);
            float fy = (y + 0.5f - SIZE * 0.5f) / (SIZE * 0.5f);
            float radius = sqrtf(fx * fx + fy * fy);
            float delta = (radius - 0.68f) / 0.075f;
            float band = expf(-0.5f * delta * delta);
            float edge = Q_clipf((0.98f - radius) / 0.08f, 0.0f, 1.0f);
            byte alpha = 255.0f * Q_clipf(band * edge * 0.90f, 0.0f, 1.0f);
            pixels[y * SIZE + x] = MakeColor(255, 255, 255, alpha);
        }
    }

    bool uploaded = vk_upload_texture_data(&vk.shockwave_texture,
                                            SIZE, SIZE, pixels, false);
    Z_Free(pixels);
    if (!uploaded)
        return false;

    vk_update_texture_descriptor_with_sampler(&vk.shockwave_texture,
                                               vk.postprocess_sampler);
    return true;
}

bool vk_create_impact_texture(void)
{
    enum { SIZE = 128 };
    uint32_t *pixels = Z_Malloc(sizeof(*pixels) * SIZE * SIZE);

    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            float fx = (x + 0.5f - SIZE * 0.5f) / (SIZE * 0.5f);
            float fy = (y + 0.5f - SIZE * 0.5f) / (SIZE * 0.5f);
            float radius = sqrtf(fx * fx + fy * fy);
            float core_delta = radius / 0.13f;
            float ring_delta = (radius - 0.63f) / 0.075f;
            float core = expf(-0.5f * core_delta * core_delta);
            float ring = expf(-0.5f * ring_delta * ring_delta);
            float edge = Q_clipf((0.98f - radius) / 0.08f, 0.0f, 1.0f);
            float coverage = max(core * 0.92f, ring * edge * 0.88f);
            byte alpha = 255.0f * Q_clipf(coverage, 0.0f, 1.0f);
            pixels[y * SIZE + x] = MakeColor(255, 255, 255, alpha);
        }
    }

    bool uploaded = vk_upload_texture_data(&vk.impact_texture,
                                            SIZE, SIZE, pixels, false);
    Z_Free(pixels);
    if (!uploaded)
        return false;

    vk_update_texture_descriptor_with_sampler(&vk.impact_texture,
                                               vk.postprocess_sampler);
    return true;
}

bool vk_create_default_texture(void)
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

bool vk_create_shell_texture(void)
{
    uint32_t pixel = U32_WHITE;

    if (!vk_upload_texture_data(&vk.textures[1], 1, 1, &pixel, false))
        return false;

    R_SHELLTEXTURE->texnum = 1;
    return true;
}

#endif // USE_VULKAN
