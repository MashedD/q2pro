/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan snow cover: a lit, colored mesh built by the client (weather.c)
// and drawn after the opaque world with the color3d pipeline. Triangles are
// sorted into blocks of SNOW_BLOCK units, and only blocks in view are drawn:
// inside the view frustum, with a BSP leaf that the world marked visible
// from the viewer's PVS this frame.

#include "vk_local.h"

#if USE_VULKAN

#define SNOW_BLOCK      256

typedef struct {
    int     bx, by;
    vec3_t  bounds[2];
    uint32_t first_index;
    uint32_t num_indices;
    int     first_leaf;     // in snow_leafs, -1 for none known
    int     num_leafs;
} snow_block_t;

static snow_block_t *snow_blocks;
static int          num_snow_blocks;
static int          *snow_leafs;    // world leaf numbers per block
static int          num_snow_leafs;

static int snow_block_coord(float v)
{
    return (int)floorf(v / SNOW_BLOCK);
}

static int compare_tri_blocks(const void *p1, const void *p2)
{
    const int *a = p1, *b = p2;
    return a[0] - b[0];
}

// Reorders the triangles by block and records each block's range and bounds.
static uint32_t *sort_into_blocks(const r_snow_vertex_t *vertices,
                                  const uint32_t *indices, int num_indices)
{
    int num_tris = num_indices / 3;
    int (*keys)[2] = R_Malloc(sizeof(*keys) * num_tris);

    snow_blocks = R_Malloc(sizeof(*snow_blocks) * max(num_tris, 1));
    num_snow_blocks = 0;
    for (int i = 0; i < num_tris; i++) {
        const float *a = vertices[indices[i * 3 + 0]].origin;
        const float *b = vertices[indices[i * 3 + 1]].origin;
        const float *c = vertices[indices[i * 3 + 2]].origin;
        int bx = snow_block_coord((a[0] + b[0] + c[0]) / 3);
        int by = snow_block_coord((a[1] + b[1] + c[1]) / 3);
        int k;

        for (k = 0; k < num_snow_blocks; k++)
            if (snow_blocks[k].bx == bx && snow_blocks[k].by == by)
                break;
        if (k == num_snow_blocks) {
            snow_block_t *blk = &snow_blocks[num_snow_blocks++];
            blk->bx = bx;
            blk->by = by;
            ClearBounds(blk->bounds[0], blk->bounds[1]);
            blk->num_indices = 0;
        }
        keys[i][0] = k;
        keys[i][1] = i;
    }
    qsort(keys, num_tris, sizeof(*keys), compare_tri_blocks);

    uint32_t *sorted = R_Malloc(sizeof(*sorted) * num_indices);
    for (int i = 0; i < num_tris; i++) {
        snow_block_t *blk = &snow_blocks[keys[i][0]];
        const uint32_t *t = &indices[keys[i][1] * 3];

        if (!blk->num_indices)
            blk->first_index = i * 3;
        blk->num_indices += 3;
        for (int j = 0; j < 3; j++) {
            const r_snow_vertex_t *v = &vertices[t[j]];
            vec3_t lo, hi;
            VectorCopy(v->origin, lo);
            VectorCopy(v->origin, hi);
            lo[2] -= 2;
            hi[2] += max(v->height, 1);
            AddPointToBounds(lo, blk->bounds[0], blk->bounds[1]);
            AddPointToBounds(hi, blk->bounds[0], blk->bounds[1]);
            sorted[i * 3 + j] = t[j];
        }
    }

    Z_Free(keys);

    // the leaves the snow of each block lies in
    const bsp_t *bsp = vk.world.cache;
    if (bsp && bsp->nodes && bsp->leafs && bsp->numleafs > 0) {
        int *marked = R_Malloc(sizeof(int) * bsp->numleafs);
        memset(marked, 0xff, sizeof(int) * bsp->numleafs);
        int max_leafs = 1024;
        snow_leafs = R_Malloc(sizeof(int) * max_leafs);
        num_snow_leafs = 0;

        for (int b = 0; b < num_snow_blocks; b++) {
            snow_block_t *blk = &snow_blocks[b];
            blk->first_leaf = num_snow_leafs;
            for (uint32_t i = 0; i < blk->num_indices; i++) {
                vec3_t point;
                VectorCopy(vertices[sorted[blk->first_index + i]].origin, point);
                point[2] += 4;
                const mleaf_t *leaf = BSP_PointLeaf(bsp->nodes, point);
                int n = leaf - bsp->leafs;
                if (n < 0 || n >= bsp->numleafs || marked[n] == b)
                    continue;
                marked[n] = b;
                if (num_snow_leafs == max_leafs) {
                    max_leafs *= 2;
                    snow_leafs = Z_Realloc(snow_leafs, sizeof(int) * max_leafs);
                }
                snow_leafs[num_snow_leafs++] = n;
            }
            blk->num_leafs = num_snow_leafs - blk->first_leaf;
        }
        Z_Free(marked);
    } else {
        for (int b = 0; b < num_snow_blocks; b++)
            snow_blocks[b].first_leaf = -1;
    }

    return sorted;
}

void VKR_SetSnowCover(const r_snow_vertex_t *vertices, int num_vertices,
                      const uint32_t *indices, int num_indices)
{
    vk_vertex_t *verts;

    if (vk.device && vk.DeviceWaitIdle && vk.snow_cover.vertices.buffer)
        vk.DeviceWaitIdle(vk.device);
    vk_free_snow_cover();

    if (num_vertices <= 0 || num_indices <= 0 || !vertices || !indices)
        return;

    // growth goes into the normal, which vk_snow.vert reads as location 2
    verts = R_Malloc(sizeof(*verts) * num_vertices);
    for (int i = 0; i < num_vertices; i++) {
        const r_snow_vertex_t *in = &vertices[i];
        vk_vertex_t *v = &verts[i];

        VectorCopy(in->origin, v->position);
        v->color[0] = in->color.u8[0] / 255.0f;
        v->color[1] = in->color.u8[1] / 255.0f;
        v->color[2] = in->color.u8[2] / 255.0f;
        v->color[3] = 1.0f;
        v->uv[0] = v->uv[1] = 0.0f;
        VectorSet(v->normal, in->height, in->start, in->length);
    }

    uint32_t *sorted = sort_into_blocks(vertices, indices, num_indices);
    if (!vk_upload_mesh(&vk.snow_cover, verts, num_vertices, sorted, num_indices)) {
        Com_WPrintf("Couldn't upload the snow cover\n");
        vk_free_snow_cover();
    }
    Z_Free(sorted);
    Z_Free(verts);
}

void vk_draw_snow_cover(const refdef_t *fd)
{
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    vk_color3d_push_t push;
    mat4_t mvp;

    if (!vk.snow_cover.index_count || !vk.snow_cover.vertices.buffer ||
        !vk.snow_pipeline || !vk.snow_full_pipeline || fd->snow_amount <= 0 ||
        (fd->rdflags & RDF_NOWORLDMODEL))
        return;

    vk_world_mvp(mvp, fd);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    Vector4Set(push.color, 1.0f, 1.0f, 1.0f, fd->snow_amount);

    VkBuffer buffers[] = { vk.snow_cover.vertices.buffer };
    VkDeviceSize offsets[] = { 0 };

    // once all of it has grown, no pixels are left out
    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                     fd->snow_amount >= 1 ? vk.snow_full_pipeline : vk.snow_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, buffers, offsets);
    vk_bind_index_buffer(cmd, vk.snow_cover.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);

    // draw the blocks in view, joining neighbouring ranges into one draw
    uint32_t first = 0, count = 0;
    for (int i = 0; i <= num_snow_blocks; i++) {
        bool visible = false;
        if (i < num_snow_blocks) {
            const snow_block_t *blk = &snow_blocks[i];
            visible = true;
            for (int j = 0; j < 4 && visible; j++)
                if (BoxOnPlaneSide(blk->bounds[0], blk->bounds[1],
                                   &vk.world.frustum[j]) == BOX_BEHIND)
                    visible = false;
            if (visible && blk->first_leaf >= 0 && vk.world.cache) {
                visible = false;
                for (int j = 0; j < blk->num_leafs && !visible; j++) {
                    const mleaf_t *leaf = &vk.world.cache->leafs[snow_leafs[blk->first_leaf + j]];
                    visible = leaf->visframe == vk.world.visframe;
                }
            }
            if (visible && count && blk->first_index == first + count) {
                count += blk->num_indices;
                continue;
            }
        }
        if (count) {
            vk.CmdDrawIndexed(cmd, count, 1, first, 0, 0);
            c.trisDrawn += count / 3;
            vk_count_batch3d();
        }
        count = 0;
        if (visible) {
            first = snow_blocks[i].first_index;
            count = snow_blocks[i].num_indices;
        }
    }
}

void vk_free_snow_cover(void)
{
    vk_destroy_mesh(&vk.snow_cover);
    Z_Freep(&snow_blocks);
    num_snow_blocks = 0;
    Z_Freep(&snow_leafs);
    num_snow_leafs = 0;
}

#endif // USE_VULKAN
