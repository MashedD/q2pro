// OpenGL snow cover: the mesh built by the client (weather.c), drawn after
// the opaque world. Only the blocks in view are drawn (snow.c).
//
// Unlike Vulkan, vertex positions for growth and craters are worked out
// here and uploaded again, up to four times a second while the snow is
// growing or a crater is filling in, and not at all once it has settled.
// With GLSL, GLS_SNOW makes the fragment shader leave out what hasn't grown
// yet along a noisy line, like vk_snow.frag; the legacy renderer just keeps
// those points under the floor. Dynamic lights are added by the shader
// too (GLS_SNOW_LIGHTS), up to SNOW_MAX_LIGHTS per draw.

#include "gl.h"
#include "snow.h"

#define SNOW_UPLOAD_INTERVAL    0.25f

typedef struct {
    vec3_t  pos;
    color_t color;
    float   tc[2];      // world X/Y, for the noise of the edge
    float   front[2];   // how far the snow at this vertex has come; 0 unused
} snow_glvertex_t;

static const glVaDesc_t snow_arrays[VERT_ATTR_COUNT] = {
    [VERT_ATTR_POS]   = { 3, false, sizeof(snow_glvertex_t), offsetof(snow_glvertex_t, pos) },
    [VERT_ATTR_TC]    = { 2, false, sizeof(snow_glvertex_t), offsetof(snow_glvertex_t, tc) },
    [VERT_ATTR_LMTC]  = { 2, false, sizeof(snow_glvertex_t), offsetof(snow_glvertex_t, front) },
    [VERT_ATTR_COLOR] = { 4, true,  sizeof(snow_glvertex_t), offsetof(snow_glvertex_t, color) },
};

static struct {
    r_snow_vertex_t *vertices;      // as sent by the client
    int             num_vertices;
    snow_glvertex_t *upload;
    float           *craters;       // time, strength per vertex
    snow_blocks_t   blocks;
    GLuint          buffers[2];     // vertices, indices
    float           last_amount;    // at the last upload
    float           last_upload;
    float           last_crater;
    bool            dirty;
} snow;

void GL_FreeSnowCover(void)
{
    if (snow.buffers[0])
        GL_DeleteBuffers(2, snow.buffers);
    Z_Free(snow.vertices);
    Z_Free(snow.upload);
    Z_Free(snow.craters);
    R_FreeSnowBlocks(&snow.blocks);
    memset(&snow, 0, sizeof(snow));
}

void GLR_SetSnowCover(const r_snow_vertex_t *vertices, int num_vertices,
                      const uint32_t *indices, int num_indices)
{
    GL_FreeSnowCover();

    // the mesh needs 32-bit indices, which OpenGL ES 1 and 2 may not have
    if (num_vertices <= 0 || num_indices <= 0 || !vertices || !indices ||
        !qglGenBuffers || !gl_static.world.cache ||
        !(gl_config.caps & QGL_CAP_ELEMENT_INDEX_UINT))
        return;

    snow.vertices = R_Malloc(sizeof(*snow.vertices) * num_vertices);
    memcpy(snow.vertices, vertices, sizeof(*snow.vertices) * num_vertices);
    snow.num_vertices = num_vertices;
    snow.upload = R_Malloc(sizeof(*snow.upload) * num_vertices);
    snow.craters = R_Malloc(sizeof(float) * 2 * num_vertices);
    R_InitSnowCraters(snow.craters, num_vertices);
    snow.last_crater = -SNOW_REFILL;

    uint32_t *sorted = R_SortSnowBlocks(&snow.blocks, gl_static.world.cache,
                                        vertices, indices, num_indices);

    qglGenBuffers(2, snow.buffers);
    GL_BindBuffer(GL_ARRAY_BUFFER, snow.buffers[0]);
    qglBufferData(GL_ARRAY_BUFFER, sizeof(*snow.upload) * num_vertices, NULL, GL_DYNAMIC_DRAW);
    GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, snow.buffers[1]);
    qglBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(*sorted) * num_indices, sorted, GL_STATIC_DRAW);
    Z_Free(sorted);

    snow.dirty = true;
}

void GLR_AddSnowCrater(const int *vertices, const float *strength, int count,
                       float time)
{
    if (!snow.craters)
        return;

    R_AddSnowCraters(snow.craters, snow.num_vertices, vertices, strength, count, time);
    snow.last_crater = time;
    snow.dirty = true;
}

// The same as vk_snow.vert.
static void update_vertices(float amount, float time)
{
    for (int i = 0; i < snow.num_vertices; i++) {
        const r_snow_vertex_t *in = &snow.vertices[i];
        const float *crater = &snow.craters[i * 2];
        snow_glvertex_t *out = &snow.upload[i];

        float front = (amount - in->start) / max(in->length, 0.001f);
        float refill = Q_clipf((time - crater[0]) / SNOW_REFILL, 0, 1);
        front -= 3 * crater[1] * (1 - refill);

        float g = Q_clipf(front, 0, 1);
        g = g * g * (3 - 2 * g);

        float h = in->height, z;
        if (h <= 0)
            z = (h + 2) * g - 2;
        else if (gl_static.use_shaders || front > 0)
            z = max(h * g, min(h, 0.75f));
        else
            z = -2;     // no shader to leave it out

        VectorSet(out->pos, in->origin[0], in->origin[1], in->origin[2] + z);
        out->color = in->color;
        out->tc[0] = in->origin[0];
        out->tc[1] = in->origin[1];
        out->front[0] = front;
        out->front[1] = 0;
    }

    // a new buffer, instead of overwriting the one the GPU may still be
    // drawing from, which can make the driver wait for it
    GL_BindBuffer(GL_ARRAY_BUFFER, snow.buffers[0]);
    qglBufferData(GL_ARRAY_BUFFER, sizeof(*snow.upload) * snow.num_vertices, NULL, GL_DYNAMIC_DRAW);
    qglBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(*snow.upload) * snow.num_vertices, snow.upload);
}

void GL_DrawSnowCover(void)
{
    float amount = glr.fd.snow_amount;
    float time = glr.fd.time;

    if (!snow.num_vertices || amount <= 0 || (glr.fd.rdflags & RDF_NOWORLDMODEL) ||
        !gl_drawworld->integer)
        return;

    bool refilling = time >= snow.last_crater && time - snow.last_crater < SNOW_REFILL;
    bool growing = amount < 1;

    if (snow.dirty || ((growing || refilling) && amount != snow.last_amount &&
        (time - snow.last_upload >= SNOW_UPLOAD_INTERVAL || time < snow.last_upload)) ||
        (refilling && time - snow.last_upload >= SNOW_UPLOAD_INTERVAL)) {
        update_vertices(amount, time);
        snow.last_amount = amount;
        snow.last_upload = time;
        snow.dirty = false;
    }

    // the client doesn't keep a consistent winding
    glStateBits_t bits = GLS_CULL_DISABLE | GLS_SHADE_SMOOTH | glr.fog_bits;
    glArrayBits_t arrays = GLA_VERTEX | GLA_TC | GLA_COLOR;
    if (gl_static.use_shaders && (growing || refilling)) {
        bits |= GLS_SNOW;
        arrays |= GLA_LMTC;
    }
    if (glr.framebuffer_bound && gl_bloom->integer)
        bits |= GLS_BLOOM_GENERATE;

    // the mesh is in world space, whatever entity was drawn last; the sky
    // matrices hold the dynamic lights, and are put back afterwards
    mat4_t saved_sky[2];
    memcpy(saved_sky, gls.u_block.m_sky, sizeof(saved_sky));
    memcpy(gls.u_block.m_model, gl_identity, sizeof(gls.u_block.m_model));
    bool lights = gl_static.use_shaders && glr.fd.num_dlights && glr.fd.dlights;

    GL_LoadMatrix(glr.viewmatrix);
    GL_BindTexture(TMU_TEXTURE, TEXNUM_WHITE);
    GL_BindArrays(VA_NONE);
    GL_BindBuffer(GL_ARRAY_BUFFER, snow.buffers[0]);
    gl_backend->array_pointers(snow_arrays, NULL);
    GL_ArrayBits(arrays);
    GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, snow.buffers[1]);

    // draw the blocks in view, joining neighbouring ranges that the same
    // dynamic lights reach into one draw
    snow_lights_t run_lights, blk_lights;
    uint32_t first = 0, count = 0;
    memset(&run_lights, 0, sizeof(run_lights));
    for (int i = 0; i <= snow.blocks.num_blocks; i++) {
        bool visible = false;
        if (i < snow.blocks.num_blocks) {
            const snow_block_t *blk = &snow.blocks.blocks[i];
            visible = R_SnowBlockVisible(&snow.blocks, i, gl_static.world.cache,
                                         glr.frustumPlanes, (int)glr.visframe);
            if (visible) {
                if (lights)
                    R_SnowBlockLights(&snow.blocks, i, glr.fd.dlights,
                                      glr.fd.num_dlights, &blk_lights);
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
            memset(gls.u_block.m_sky, 0, sizeof(gls.u_block.m_sky));
            for (int j = 0; j < run_lights.num_lights; j++) {
                Vector4Copy(run_lights.origins[j], &gls.u_block.m_sky[0][j * 4]);
                Vector4Copy(run_lights.colors[j], &gls.u_block.m_sky[1][j * 4]);
            }
            gls.u_block_dirty = true;
            GL_StateBits(bits | (run_lights.num_lights ? GLS_SNOW_LIGHTS : 0));
            GL_LoadUniforms();
            qglDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT,
                            VBO_OFS(first * sizeof(uint32_t)));
            c.trisDrawn += count / 3;
        }
        count = 0;
        if (visible) {
            first = snow.blocks.blocks[i].first_index;
            count = snow.blocks.blocks[i].num_indices;
            run_lights = blk_lights;
        }
    }

    memcpy(gls.u_block.m_sky, saved_sky, sizeof(saved_sky));
    gls.u_block_dirty = true;
}
