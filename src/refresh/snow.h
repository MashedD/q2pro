// snow.h -- snow cover helpers shared by the OpenGL and Vulkan renderers

#pragma once

#define SNOW_BLOCK      256     // size of a culling block, in units
#define SNOW_REFILL     60      // seconds for a crater to fill in again

typedef struct {
    int         bx, by;
    vec3_t      bounds[2];
    uint32_t    first_index;
    uint32_t    num_indices;
    int         first_leaf;     // in snow_blocks_t.leafs, -1 for none known
    int         num_leafs;
} snow_block_t;

typedef struct {
    snow_block_t    *blocks;
    int             num_blocks;
    int             *leafs;     // world leaf numbers per block
    int             num_leafs;
} snow_blocks_t;

uint32_t *R_SortSnowBlocks(snow_blocks_t *sb, const bsp_t *bsp,
                           const r_snow_vertex_t *vertices,
                           const uint32_t *indices, int num_indices);
void R_FreeSnowBlocks(snow_blocks_t *sb);
bool R_SnowBlockVisible(const snow_blocks_t *sb, int block, const bsp_t *bsp,
                        const cplane_t *frustum, int visframe);

void R_InitSnowCraters(float *craters, int num_vertices);
void R_AddSnowCraters(float *craters, int num_vertices, const int *vertices,
                      const float *strength, int count, float time);
