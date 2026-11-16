// Vulkan snow cover: a lit, colored mesh built by the client (weather.c)
// and drawn after the opaque world with its own pipelines; vk_snow.vert grows
// it. Only the blocks in view are drawn (snow.c).
//
// Explosions blow snow away (R_AddSnowCrater): a host-visible buffer holds
// the time and strength of the last crater at each vertex, as a second
// vertex stream, so a crater needs no upload of the mesh.
//
// Dynamic lights are added by the fragment shaders, up to SNOW_MAX_LIGHTS
// per draw, as push constants; with ray tracing, they cast shadows.

#include "vk_local.h"
#include "snow.h"

#if USE_VULKAN

typedef char vk_snow_push_lights_check[
    sizeof(((vk_snow_push_t *)0)->dlight_origins) ==
    sizeof(((snow_lights_t *)0)->origins) ? 1 : -1];

static snow_blocks_t   snow_blocks;
static r_cover_kind_t  snow_kind;

static struct {
    VkBuffer        buffer;
    VkDeviceMemory  memory;
    float           *mapped;        // time, strength per vertex
    int             num_vertices;
    float           last_time;      // of the last crater
} snow_craters;

static void free_craters(void)
{
    if (snow_craters.mapped)
        vk.UnmapMemory(vk.device, snow_craters.memory);
    if (snow_craters.buffer)
        vk.DestroyBuffer(vk.device, snow_craters.buffer, NULL);
    if (snow_craters.memory)
        vk.FreeMemory(vk.device, snow_craters.memory, NULL);
    memset(&snow_craters, 0, sizeof(snow_craters));
}

static bool create_craters(int num_vertices)
{
    VkDeviceSize size = sizeof(float) * 2 * num_vertices;
    void *mapped;

    if (!vk_create_buffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &snow_craters.buffer, &snow_craters.memory))
        return false;
    VkResult result = vk.MapMemory(vk.device, snow_craters.memory, 0, size, 0, &mapped);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkMapMemory", result);

    snow_craters.mapped = mapped;
    snow_craters.num_vertices = num_vertices;
    snow_craters.last_time = -SNOW_REFILL;
    R_InitSnowCraters(snow_craters.mapped, num_vertices);
    return true;
}

void VKR_AddSnowCrater(const int *vertices, const float *strength, int count,
                       float time)
{
    if (!snow_craters.mapped)
        return;

    R_AddSnowCraters(snow_craters.mapped, snow_craters.num_vertices,
                     vertices, strength, count, time);
    snow_craters.last_time = time;
}

void VKR_SetSnowCover(r_cover_kind_t kind,
                      const r_snow_vertex_t *vertices, int num_vertices,
                      const uint32_t *indices, int num_indices)
{
    vk_vertex_t *verts;

    if (vk.device && vk.DeviceWaitIdle && vk.snow_cover.vertices.buffer)
        vk.DeviceWaitIdle(vk.device);
    vk_free_snow_cover();

    if (num_vertices <= 0 || num_indices <= 0 || !vertices || !indices)
        return;
    snow_kind = kind;

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

    uint32_t *sorted = R_SortSnowBlocks(&snow_blocks, vk.world.cache, vertices,
                                        indices, num_indices);
    if (!vk_upload_mesh(&vk.snow_cover, verts, num_vertices, sorted, num_indices) ||
        !create_craters(num_vertices)) {
        Com_WPrintf("Couldn't upload the snow cover\n");
        vk_free_snow_cover();
    }
    Z_Free(sorted);
    Z_Free(verts);
}

void vk_draw_snow_cover(const refdef_t *fd)
{
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    vk_snow_push_t push;
    mat4_t mvp;

    if (!vk.snow_cover.index_count || !vk.snow_cover.vertices.buffer ||
        !vk.snow_pipeline || !vk.snow_full_pipeline || !vk.puddle_pipeline ||
        !snow_craters.buffer ||
        fd->snow_amount <= 0 ||
        (fd->rdflags & RDF_NOWORLDMODEL))
        return;

    vk_world_mvp(mvp, fd);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    Vector4Set(push.color, 1.0f, 1.0f, 1.0f, fd->snow_amount);
    Vector4Set(push.craters, fd->time, SNOW_REFILL, 0.0f, 0.0f);
    vk_fog_params(fd, push.fog);
    VectorCopy(fd->vieworg, push.view);
    push.view[3] = fd->rain_amount;

    VkBuffer buffers[] = { vk.snow_cover.vertices.buffer, snow_craters.buffer };
    VkDeviceSize offsets[] = { 0, 0 };

    // once all of it has grown and no crater is filling in, no pixels are
    // left out
    bool full = fd->snow_amount >= 1 &&
        (fd->time < snow_craters.last_time ||
         fd->time - snow_craters.last_time > SNOW_REFILL);
    VkPipeline pipeline = full ? vk.snow_full_pipeline : vk.snow_pipeline;
    if (snow_kind == R_COVER_PUDDLES)
        pipeline = vk.puddle_pipeline;
#if USE_VULKAN_RAYTRACING
    // with ray tracing, dynamic lights cast shadows on the snow
    else if (vk.raytracing_active && vk.rt_descriptor_set && vk.rt_tlas.handle &&
        vk.snow_rt_pipelines[full]) {
        pipeline = vk.snow_rt_pipelines[full];
        vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 vk.rect_pipeline_layout, 1, 1,
                                 &vk.rt_descriptor_set, 0, NULL);
    }
#endif
    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_vertex_buffers(cmd, 0, 2, buffers, offsets);
    vk_bind_index_buffer(cmd, vk.snow_cover.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

    // draw the blocks in view, joining neighbouring ranges that the same
    // dynamic lights reach into one draw
    bool lights = vk_dynamic_lights_enabled() && fd->dlights;
    snow_lights_t run_lights, blk_lights;
    uint32_t first = 0, count = 0;
    memset(&run_lights, 0, sizeof(run_lights));
    for (int i = 0; i <= snow_blocks.num_blocks; i++) {
        bool visible = false;
        if (i < snow_blocks.num_blocks) {
            const snow_block_t *blk = &snow_blocks.blocks[i];
            visible = R_SnowBlockVisible(&snow_blocks, i, vk.world.cache,
                                         vk.world.frustum, vk.world.visframe);
            if (visible) {
                if (lights)
                    R_SnowBlockLights(&snow_blocks, i, fd->dlights,
                                      fd->num_dlights, &blk_lights);
                else
                    memset(&blk_lights, 0, sizeof(blk_lights));
            }
            if (visible && count && blk->first_index == first + count &&
                !memcmp(&blk_lights, &run_lights, sizeof(run_lights))) {
                count += blk->num_indices;
                continue;
            }
        }
        if (count) {
            memcpy(push.dlight_origins, run_lights.origins, sizeof(push.dlight_origins));
            memcpy(push.dlight_colors, run_lights.colors, sizeof(push.dlight_colors));
            vk_push_constants(cmd, sizeof(push), &push);
            vk.CmdDrawIndexed(cmd, count, 1, first, 0, 0);
            c.trisDrawn += count / 3;
            vk_count_batch3d();
        }
        count = 0;
        if (visible) {
            first = snow_blocks.blocks[i].first_index;
            count = snow_blocks.blocks[i].num_indices;
            run_lights = blk_lights;
        }
    }
}

void vk_free_snow_cover(void)
{
    vk_destroy_mesh(&vk.snow_cover);
    free_craters();
    R_FreeSnowBlocks(&snow_blocks);
}

#endif // USE_VULKAN
