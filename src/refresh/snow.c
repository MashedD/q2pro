// snow.c -- snow cover helpers shared by the OpenGL and Vulkan renderers.
//
// Both renderers draw the cover built by the client (weather.c) in blocks of
// SNOW_BLOCK units, and only blocks in view: inside the view frustum, with a
// BSP leaf that the world pass marked visible from the viewer's PVS. Both
// keep the time and strength of the last explosion crater at each vertex.

#include "gl.h"
#include "snow.h"

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
uint32_t *R_SortSnowBlocks(snow_blocks_t *sb, const bsp_t *bsp,
                           const r_snow_vertex_t *vertices,
                           const uint32_t *indices, int num_indices)
{
    int num_tris = num_indices / 3;
    int (*keys)[2] = R_Malloc(sizeof(*keys) * num_tris);

    sb->blocks = R_Malloc(sizeof(*sb->blocks) * max(num_tris, 1));
    sb->num_blocks = 0;
    for (int i = 0; i < num_tris; i++) {
        const float *a = vertices[indices[i * 3 + 0]].origin;
        const float *b = vertices[indices[i * 3 + 1]].origin;
        const float *c = vertices[indices[i * 3 + 2]].origin;
        int bx = snow_block_coord((a[0] + b[0] + c[0]) / 3);
        int by = snow_block_coord((a[1] + b[1] + c[1]) / 3);
        int k;

        for (k = 0; k < sb->num_blocks; k++)
            if (sb->blocks[k].bx == bx && sb->blocks[k].by == by)
                break;
        if (k == sb->num_blocks) {
            snow_block_t *blk = &sb->blocks[sb->num_blocks++];
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
        snow_block_t *blk = &sb->blocks[keys[i][0]];
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
    if (bsp && bsp->nodes && bsp->leafs && bsp->numleafs > 0) {
        int *marked = R_Malloc(sizeof(int) * bsp->numleafs);
        memset(marked, 0xff, sizeof(int) * bsp->numleafs);
        int max_leafs = 1024;
        sb->leafs = R_Malloc(sizeof(int) * max_leafs);
        sb->num_leafs = 0;

        for (int b = 0; b < sb->num_blocks; b++) {
            snow_block_t *blk = &sb->blocks[b];
            blk->first_leaf = sb->num_leafs;
            for (uint32_t i = 0; i < blk->num_indices; i++) {
                vec3_t point;
                VectorCopy(vertices[sorted[blk->first_index + i]].origin, point);
                point[2] += 4;
                const mleaf_t *leaf = BSP_PointLeaf(bsp->nodes, point);
                int n = leaf - bsp->leafs;
                if (n < 0 || n >= bsp->numleafs || marked[n] == b)
                    continue;
                marked[n] = b;
                if (sb->num_leafs == max_leafs) {
                    max_leafs *= 2;
                    sb->leafs = Z_Realloc(sb->leafs, sizeof(int) * max_leafs);
                }
                sb->leafs[sb->num_leafs++] = n;
            }
            blk->num_leafs = sb->num_leafs - blk->first_leaf;
        }
        Z_Free(marked);
    } else {
        for (int b = 0; b < sb->num_blocks; b++)
            sb->blocks[b].first_leaf = -1;
    }

    return sorted;
}

void R_FreeSnowBlocks(snow_blocks_t *sb)
{
    Z_Free(sb->blocks);
    Z_Free(sb->leafs);
    memset(sb, 0, sizeof(*sb));
}

bool R_SnowBlockVisible(const snow_blocks_t *sb, int block, const bsp_t *bsp,
                        const cplane_t *frustum, int visframe)
{
    const snow_block_t *blk = &sb->blocks[block];

    for (int i = 0; i < 4; i++)
        if (BoxOnPlaneSide(blk->bounds[0], blk->bounds[1], &frustum[i]) == BOX_BEHIND)
            return false;

    if (blk->first_leaf < 0 || !bsp)
        return true;
    for (int i = 0; i < blk->num_leafs; i++)
        if (bsp->leafs[sb->leafs[blk->first_leaf + i]].visframe == visframe)
            return true;
    return false;
}

// Craters hold two floats per vertex: the time of the last crater there and
// its strength. The renderers set a vertex back on its growth schedule by
// three times the strength still left, which fades over SNOW_REFILL seconds.
void R_InitSnowCraters(float *craters, int num_vertices)
{
    for (int i = 0; i < num_vertices; i++) {
        craters[i * 2 + 0] = -SNOW_REFILL;
        craters[i * 2 + 1] = 0;
    }
}

void R_AddSnowCraters(float *craters, int num_vertices, const int *vertices,
                      const float *strength, int count, float time)
{
    for (int i = 0; i < count; i++) {
        int v = vertices[i];
        if (v < 0 || v >= num_vertices)
            continue;
        // keep what is left of an earlier crater here
        float *c = &craters[v * 2];
        float refill = Q_clipf((time - c[0]) / SNOW_REFILL, 0, 1);
        c[1] = max(Q_clipf(strength[i], 0, 1), c[1] * (1 - refill));
        c[0] = time;
    }
}
