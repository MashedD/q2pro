/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan snow cover: a lit, colored mesh built by the client (weather.c)
// and drawn after the opaque world with the color3d pipeline.

#include "vk_local.h"

#if USE_VULKAN

void VKR_SetSnowCover(const vec3_t *positions, const color_t *colors,
                      int num_vertices, const uint32_t *indices, int num_indices)
{
    vk_vertex_t *verts;

    if (vk.device && vk.DeviceWaitIdle && vk.snow_cover.vertices.buffer)
        vk.DeviceWaitIdle(vk.device);
    vk_destroy_mesh(&vk.snow_cover);

    if (num_vertices <= 0 || num_indices <= 0 || !positions || !colors || !indices)
        return;

    verts = R_Malloc(sizeof(*verts) * num_vertices);
    for (int i = 0; i < num_vertices; i++) {
        vk_vertex_t *v = &verts[i];

        VectorCopy(positions[i], v->position);
        v->color[0] = colors[i].u8[0] / 255.0f;
        v->color[1] = colors[i].u8[1] / 255.0f;
        v->color[2] = colors[i].u8[2] / 255.0f;
        v->color[3] = 1.0f;
        v->uv[0] = v->uv[1] = 0.0f;
        VectorClear(v->normal);
    }

    if (!vk_upload_mesh(&vk.snow_cover, verts, num_vertices, indices, num_indices))
        Com_WPrintf("Couldn't upload the snow cover\n");
    Z_Free(verts);
}

void vk_draw_snow_cover(const refdef_t *fd)
{
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    vk_color3d_push_t push;
    mat4_t mvp;

    if (!vk.snow_cover.index_count || !vk.snow_cover.vertices.buffer ||
        !vk.color3d_pipeline || (fd->rdflags & RDF_NOWORLDMODEL))
        return;

    vk_world_mvp(mvp, fd);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    Vector4Set(push.color, 1.0f, 1.0f, 1.0f, 1.0f);

    VkBuffer buffers[] = { vk.snow_cover.vertices.buffer };
    VkDeviceSize offsets[] = { 0 };

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.color3d_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, buffers, offsets);
    vk_bind_index_buffer(cmd, vk.snow_cover.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, vk.snow_cover.index_count, 1, 0, 0, 0);
    c.trisDrawn += vk.snow_cover.index_count / 3;
    vk_count_batch3d();
}

void vk_free_snow_cover(void)
{
    vk_destroy_mesh(&vk.snow_cover);
}

#endif // USE_VULKAN
