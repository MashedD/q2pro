/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan world loading: lightmaps, the world mesh, ray-traced surface lights and lava ember anchors, and map load.

#include "vk_local.h"

#if USE_VULKAN

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

void vk_destroy_pixel_lightmap_staging(void)
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
    const vk_world_face_t *draw;
    vec3_t normal;
    float area;
    uint32_t index;
    int anchor_count;
} vk_lava_face_candidate_t;

static int vk_lava_face_candidate_compare(const void *a, const void *b)
{
    const vk_lava_face_candidate_t *ca = a;
    const vk_lava_face_candidate_t *cb = b;

    if (ca->area < cb->area)
        return 1;
    if (ca->area > cb->area)
        return -1;
    if (ca->index < cb->index)
        return -1;
    return ca->index != cb->index;
}

static uint32_t vk_lava_ember_hash(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float vk_lava_ember_random(uint32_t value)
{
    return (vk_lava_ember_hash(value) & 0x00ffffffu) / 16777216.0f;
}

static bool vk_add_lava_ember_anchor(const vk_lava_face_candidate_t *candidate,
                                     const vk_vertex_t *vertices,
                                     uint32_t sequence)
{
    if (!candidate || !candidate->draw || candidate->draw->edge_count < 3 ||
        vk.world.lava_ember_anchor_count >= VK_MAX_LAVA_EMBER_ANCHORS)
        return false;

    const vk_world_face_t *draw = candidate->draw;
    const vec_t *base = vertices[draw->first_vertex].position;
    uint32_t hash = vk_lava_ember_hash(candidate->index * 0x9e3779b9u +
                                       sequence * 0x85ebca6bu + 1u);
    float target = vk_lava_ember_random(hash) * candidate->area;
    uint32_t triangle = 1;

    for (uint32_t i = 1; i + 1 < draw->edge_count; i++) {
        vec3_t edge1, edge2, cross;
        VectorSubtract(vertices[draw->first_vertex + i].position, base, edge1);
        VectorSubtract(vertices[draw->first_vertex + i + 1].position, base, edge2);
        CrossProduct(edge1, edge2, cross);
        float area = VectorLength(cross) * 0.5f;
        triangle = i;
        if (target <= area)
            break;
        target -= area;
    }

    const vec_t *b = vertices[draw->first_vertex + triangle].position;
    const vec_t *c = vertices[draw->first_vertex + triangle + 1].position;
    float root = sqrtf(vk_lava_ember_random(hash ^ 0x68bc21ebu));
    float split = vk_lava_ember_random(hash ^ 0x02e5be93u);
    float weights[3] = { 1.0f - root, root * (1.0f - split), root * split };
    vk_lava_ember_anchor_t *anchor =
        &vk.world.lava_ember_anchors[vk.world.lava_ember_anchor_count++];
    for (int axis = 0; axis < 3; axis++) {
        anchor->origin[axis] = base[axis] * weights[0] + b[axis] * weights[1] +
            c[axis] * weights[2] + candidate->normal[axis] * 4.0f;
        anchor->normal[axis] = candidate->normal[axis];
    }
    anchor->face = draw->face;
    anchor->hash = hash;
    return true;
}

static void vk_build_lava_ember_anchors(const vk_world_face_t *faces,
                                        uint32_t face_count,
                                        const vk_vertex_t *vertices)
{
    vk.world.lava_ember_anchor_count = 0;
    if (!faces || !face_count || !vertices)
        return;

    vk_lava_face_candidate_t *candidates =
        Z_Malloc(sizeof(*candidates) * face_count);
    uint32_t candidate_count = 0;

    for (uint32_t i = 0; i < face_count; i++) {
        const vk_world_face_t *draw = &faces[i];
        if (!draw->face || vk_world_liquid_kind(draw->face) < 3)
            continue;

        vec3_t normal;
        VectorCopy(draw->face->plane->normal, normal);
        if (draw->face->drawflags & DSURF_PLANEBACK)
            VectorNegate(normal, normal);
        if (VectorNormalize(normal) <= 0.0f || normal[2] < 0.45f)
            continue;

        float area = vk_polygon_area(vertices, draw);
        if (area < 16.0f)
            continue;

        vk_lava_face_candidate_t *candidate = &candidates[candidate_count++];
        candidate->draw = draw;
        VectorCopy(normal, candidate->normal);
        candidate->area = area;
        candidate->index = i;
        candidate->anchor_count = Q_clip((int)ceilf(area / 2048.0f), 1, 8);
    }

    qsort(candidates, candidate_count, sizeof(*candidates),
          vk_lava_face_candidate_compare);
    for (int layer = 0; layer < 8 &&
         vk.world.lava_ember_anchor_count < VK_MAX_LAVA_EMBER_ANCHORS; layer++) {
        for (uint32_t i = 0; i < candidate_count &&
             vk.world.lava_ember_anchor_count < VK_MAX_LAVA_EMBER_ANCHORS; i++) {
            if (candidates[i].anchor_count > layer)
                vk_add_lava_ember_anchor(&candidates[i], vertices, layer);
        }
    }

    Com_DPrintf("Vulkan RT lava embers: %u anchors\n",
                vk.world.lava_ember_anchor_count);
    Z_Free(candidates);
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
    if (ok)
        vk_build_lava_ember_anchors(draw_faces, draw_face_count, vertices);
    else
        vk.world.lava_ember_anchor_count = 0;
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

void vk_rebuild_world_lighting(void)
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

void vk_load_world(const char *name)
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
        bsp->refcount--;
        return;
    }

    vk_free_world();
    vk.world.cache = bsp;
    vk.world.size = vk_world_size_for_bsp(bsp);

    vk_register_world_images(bsp);
#if USE_VULKAN_RAYTRACING
    // Reflection and roughness per texture, only used by ray tracing.
    if (vk.raytracing_active)
        vk_load_rt_materials();
#endif
    vk_prepare_world_surfaces(bsp);

    if (!vk_build_world_mesh(bsp, NULL))
        Com_WPrintf("Couldn't build Vulkan world mesh: %s\n", Com_GetLastError());
    vk_clear_world_lighting_modified();
}

#endif // USE_VULKAN
