/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan world drawing: faces, fog, dynamic lights, sky, visibility, brush models and light sampling.

#include "vk_local.h"

#if USE_VULKAN

static float vk_world_face_alpha(const mface_t *face)
{
    if (face->drawflags & SURF_TRANS33)
        return 0.33f;
    if (face->drawflags & SURF_TRANS66)
        return 0.66f;
    return 1.0f;
}

#define VK_BACKFACE_EPSILON 0.01f

static bool vk_world_face_backfacing(const mface_t *face, const vec3_t vieworg)
{
    vec_t dot = PlaneDiffFast(vieworg, face->plane);

    return (face->drawflags & DSURF_PLANEBACK) ?
        dot > VK_BACKFACE_EPSILON : dot < -VK_BACKFACE_EPSILON;
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

static bool vk_world_lamp_bloom_enabled(void)
{
    return vk_bloom_lamps && vk_bloom_lamps->integer;
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

    // Mark glowing faces without a glow map with a small fraction in the
    // mode, so the world shaders let them feed bloom (gl_bloom_lamps), as the
    // OpenGL renderer does: 0.125 for the lit color (1), 0.375 for the
    // stronger glow from their bright texels (2).
    if (vk_world_lamp_bloom_enabled() &&
        !(face->drawflags & (SURF_TRANS_MASK | SURF_WARP | SURF_NODRAW)) &&
        (face->texinfo->c.flags & SURF_LIGHT) &&
        !face->texinfo->image->texnum2)
        scroll[2] += vk_bloom_lamps->integer >= 2 ? 0.375f : 0.125f;

    if (fullbright || (face->drawflags & vk.world.nolm_mask))
        return;

    color[0] = Cvar_ClampValue(vk_modulate, 0.0f, 1e6f) *
        Cvar_ClampValue(vk_modulate_world, 0.0f, 1e6f);
    color[1] = color[0];
    color[2] = color[0];
    scroll[3] = Cvar_ClampValue(vk_brightness, -1.0f, 1.0f);
}

bool vk_dynamic_lights_enabled(void)
{
    return (!vk_dynamic || vk_dynamic->integer == 1) &&
           (!vk_vertexlight || !vk_vertexlight->integer);
}

static float vk_rt_atmosphere_strength(const refdef_t *fd)
{
    if (!fd || !vk.raytracing_active || !vk_rt_atmosphere ||
        (vk_fog && !vk_fog->integer) ||
        (fd->rdflags & (RDF_UNDERWATER | RDF_NOWORLDMODEL)) ||
        !vk.world.cache)
        return 0.0f;

    return Cvar_ClampValue(vk_rt_atmosphere, 0.0f, 1.0f);
}

static float vk_rt_underwater_absorption_strength(const refdef_t *fd)
{
    if (!fd || !vk.raytracing_active || !vk_rt_underwater_absorption ||
        (vk_fog && !vk_fog->integer) ||
        !(fd->rdflags & RDF_UNDERWATER) ||
        (fd->rdflags & RDF_NOWORLDMODEL) || vk.view_liquid_kind != 1 ||
        !vk.world.cache)
        return 0.0f;

    return Cvar_ClampValue(vk_rt_underwater_absorption, 0.0f, 1.0f);
}

void vk_fog_params(const refdef_t *fd, float fog[4])
{
    Vector4Clear(fog);

    if (!fd || (vk_fog && !vk_fog->integer))
        return;

    const vec3_t atmosphere_color = { 0.28f, 0.32f, 0.38f };
    const vec3_t water_color = { 0.06f, 0.18f, 0.22f };
    float base_density = max(fd->fog.density, 0.0f) / 64.0f;
    float atmosphere_density = 0.00065f * vk_rt_atmosphere_strength(fd);
    float water_density = 0.0023f *
        vk_rt_underwater_absorption_strength(fd);
    float density_sum = base_density + atmosphere_density + water_density;
    if (density_sum <= 0.0f)
        return;

    for (int i = 0; i < 3; i++)
        fog[i] = (fd->fog.color[i] * base_density +
                  atmosphere_color[i] * atmosphere_density +
                  water_color[i] * water_density) / density_sum;
    fog[3] = sqrtf(base_density * base_density +
                   atmosphere_density * atmosphere_density +
                   water_density * water_density);
}

void vk_height_fog_params(const refdef_t *fd, float start[4],
                                 float end[4], float view[4],
                                 float params[4])
{
    Vector4Clear(start);
    Vector4Clear(end);
    Vector4Clear(view);
    Vector4Clear(params);

    if (!fd || (vk_fog && !vk_fog->integer) ||
        fd->heightfog.density <= 0.0f || fd->heightfog.falloff <= 0.0f)
        return;

    VectorCopy(fd->heightfog.start.color, start);
    start[3] = fd->heightfog.start.dist;
    VectorCopy(fd->heightfog.end.color, end);
    end[3] = fd->heightfog.end.dist;
    VectorCopy(fd->vieworg, view);
    params[0] = fd->heightfog.density;
    params[1] = fd->heightfog.falloff;
    params[3] = 1.0f;
}

static void vk_sky_fog_params(const refdef_t *fd, float fog[4])
{
    Vector4Clear(fog);

    if (!fd || (vk_fog && !vk_fog->integer))
        return;

    const vec3_t atmosphere_color = { 0.28f, 0.32f, 0.38f };
    float base_factor = Q_clipf(fd->fog.sky_factor, 0.0f, 1.0f);
    float atmosphere_factor = 0.30f * vk_rt_atmosphere_strength(fd);
    if (base_factor <= 0.0f && atmosphere_factor <= 0.0f)
        return;

    if (base_factor > 0.0f && atmosphere_factor > 0.0f) {
        float atmosphere_mix = atmosphere_factor /
            (base_factor + atmosphere_factor);
        for (int i = 0; i < 3; i++)
            fog[i] = fd->fog.color[i] +
                (atmosphere_color[i] - fd->fog.color[i]) * atmosphere_mix;
    } else if (base_factor > 0.0f) {
        VectorCopy(fd->fog.color, fog);
    } else {
        VectorCopy(atmosphere_color, fog);
    }
    fog[3] = -(1.0f - (1.0f - base_factor) *
                         (1.0f - atmosphere_factor));
}

float vk_texture_intensity(void)
{
    return Cvar_ClampValue(vk_intensity, 1.0f, 5.0f);
}

static float vk_world_face_desaturation(const mface_t *face)
{
    if (!face || (face->drawflags & SURF_WARP))
        return 0.0f;

    return 1.0f - Cvar_ClampValue(vk_saturation, 0.0f, 1.0f);
}

static float vk_world_face_intensity(const mface_t *face)
{
    // Match the OpenGL surface state: translucent textures are not boosted by
    // the global intensity setting.
    return (face->drawflags & SURF_TRANS_MASK) ? 1.0f : vk_texture_intensity();
}

float vk_glowmap_intensity(void)
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

static bool vk_afterglow_rt_active(void)
{
#if USE_VULKAN_RAYTRACING
    return vk.raytracing_active;
#else
    return false;
#endif
}

void vk_update_afterglow(const refdef_t *fd)
{
    float duration = vk_rt_afterglow ?
        Cvar_ClampValue(vk_rt_afterglow, 0.0f, 1.0f) : 0.0f;
    if (!fd || !vk_afterglow_rt_active() || duration <= 0.0f ||
        !vk_dynamic_lights_enabled()) {
        memset(vk.afterglow_lights, 0, sizeof(vk.afterglow_lights));
        vk.afterglow_time = fd ? fd->time : 0.0f;
        return;
    }

    if (fd->time < vk.afterglow_time) {
        memset(vk.afterglow_lights, 0, sizeof(vk.afterglow_lights));
        vk.afterglow_frame = 0;
    }
    vk.afterglow_time = fd->time;
    uint32_t frame = ++vk.afterglow_frame;
    if (!frame) {
        memset(vk.afterglow_lights, 0, sizeof(vk.afterglow_lights));
        frame = ++vk.afterglow_frame;
    }

    for (int i = 0; i < fd->num_dlights && fd->dlights; i++) {
        const dlight_t *light = &fd->dlights[i];
        float peak = max(light->color[0],
                         max(light->color[1], light->color[2]));
        if (light->intensity < 160.0f || peak <= 0.10f)
            continue;

        int best = -1;
        float best_distance = 128.0f;
        float light_color_length = sqrtf(DotProduct(light->color,
                                                     light->color));
        for (int j = 0; j < VK_RT_AFTERGLOW_LIGHTS; j++) {
            vk_rt_afterglow_light_t *cached = &vk.afterglow_lights[j];
            if (!cached->valid || cached->seen_frame == frame)
                continue;
            float age = fd->time - cached->last_seen;
            if (age < 0.0f || age > duration)
                continue;
            float cached_color_length = sqrtf(DotProduct(cached->light.color,
                                                          cached->light.color));
            float similarity = DotProduct(light->color, cached->light.color) /
                max(light_color_length * cached_color_length, 0.0001f);
            float distance = Distance(light->origin, cached->light.origin);
            if (similarity >= 0.75f && distance <= best_distance) {
                best = j;
                best_distance = distance;
            }
        }

        if (best < 0) {
            float oldest = 1e30f;
            for (int j = 0; j < VK_RT_AFTERGLOW_LIGHTS; j++) {
                vk_rt_afterglow_light_t *cached = &vk.afterglow_lights[j];
                if (!cached->valid ||
                    fd->time - cached->last_seen > duration) {
                    best = j;
                    break;
                }
                if (cached->seen_frame != frame && cached->last_seen < oldest) {
                    oldest = cached->last_seen;
                    best = j;
                }
            }
        }
        if (best < 0)
            continue;

        vk_rt_afterglow_light_t *cached = &vk.afterglow_lights[best];
        cached->light = *light;
        cached->last_seen = fd->time;
        cached->seen_frame = frame;
        cached->valid = true;
    }

    for (int i = 0; i < VK_RT_AFTERGLOW_LIGHTS; i++) {
        vk_rt_afterglow_light_t *cached = &vk.afterglow_lights[i];
        if (cached->valid &&
            fd->time - cached->last_seen > duration)
            cached->valid = false;
    }
}

static int vk_world_insert_dynamic_light(const vk_world_face_t *face,
                                         const dlight_t *light,
                                         bool afterglow,
                                         const entity_t *ent,
                                         const vec3_t axis[3], int count,
                                         float scores[VK_WORLD_MAX_DLIGHTS],
                                         float origins[VK_WORLD_MAX_DLIGHTS][4],
                                         float colors[VK_WORLD_MAX_DLIGHTS][4])
{
    vec3_t light_origin;
    float plane_dist = vk_world_face_light_plane_dist(face->face, light,
                                                       ent, axis);
    vk_world_face_light_origin(light, ent, axis, light_origin);
    float f = vk_world_dynamic_light_fraction(light, face->face, light_origin,
                                               plane_dist);
    if (f <= 0.0f)
        return count;

    int slot = min(count, VK_WORLD_MAX_DLIGHTS - 1);
    while (slot > 0 && f > scores[slot - 1]) {
        if (origins)
            memcpy(origins[slot], origins[slot - 1], sizeof(origins[slot]));
        if (colors)
            memcpy(colors[slot], colors[slot - 1], sizeof(colors[slot]));
        scores[slot] = scores[slot - 1];
        slot--;
    }
    if (slot >= VK_WORLD_MAX_DLIGHTS ||
        (count >= VK_WORLD_MAX_DLIGHTS && f <= scores[slot]))
        return count;

    scores[slot] = f;
    if (origins) {
        VectorCopy(light_origin, origins[slot]);
        origins[slot][3] = max(light->intensity - DLIGHT_CUTOFF *
            ((vk_dlight_falloff && vk_dlight_falloff->integer) ? 0.8f : 1.0f),
            0.0f);
    }
    if (colors) {
        VectorCopy(light->color, colors[slot]);
        colors[slot][3] = afterglow ? -light->intensity : light->intensity;
    }
    return count < VK_WORLD_MAX_DLIGHTS ? count + 1 : count;
}

static int vk_world_dynamic_lights(const vk_world_face_t *face,
                                   const refdef_t *fd, const entity_t *ent,
                                   const vec3_t axis[3],
                                   float origins[VK_WORLD_MAX_DLIGHTS][4],
                                   float colors[VK_WORLD_MAX_DLIGHTS][4])
{
    float scores[VK_WORLD_MAX_DLIGHTS] = { 0.0f };
    int count = 0;

    if (origins)
        memset(origins, 0, sizeof(float) * VK_WORLD_MAX_DLIGHTS * 4);
    if (colors)
        memset(colors, 0, sizeof(float) * VK_WORLD_MAX_DLIGHTS * 4);

    if (!face || !face->face || !face->face->plane ||
        !fd || !vk_dynamic_lights_enabled() ||
        (face->face->drawflags & vk.world.nolm_mask))
        return 0;

    for (int i = 0; i < fd->num_dlights && fd->dlights; i++)
        count = vk_world_insert_dynamic_light(face, &fd->dlights[i], false,
            ent, axis, count, scores, origins, colors);

    float duration = vk_rt_afterglow ?
        Cvar_ClampValue(vk_rt_afterglow, 0.0f, 1.0f) : 0.0f;
    if (vk_afterglow_rt_active() && duration > 0.0f) {
        for (int i = 0; i < VK_RT_AFTERGLOW_LIGHTS; i++) {
            const vk_rt_afterglow_light_t *cached = &vk.afterglow_lights[i];
            if (!cached->valid || cached->seen_frame == vk.afterglow_frame)
                continue;
            float age = fd->time - cached->last_seen;
            if (age < 0.0f || age >= duration)
                continue;
            float fade = 1.0f - age / duration;
            fade *= fade;
            dlight_t light = cached->light;
            light.intensity = DLIGHT_CUTOFF +
                max(light.intensity - DLIGHT_CUTOFF, 0.0f) * fade;
            count = vk_world_insert_dynamic_light(face, &light, true, ent,
                axis, count, scores, origins, colors);
        }
    }

    return count;
}

static int vk_world_lights(const vk_world_face_t *face, const refdef_t *fd,
                           const entity_t *ent, const vec3_t axis[3],
                           float origins[VK_WORLD_MAX_DLIGHTS][4],
                           float colors[VK_WORLD_MAX_DLIGHTS][4])
{
    return vk_world_dynamic_lights(face, fd, ent, axis, origins, colors);
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

void vk_draw_world_outlines(const mat4_t mvp, bool marked_only,
                                   vk_world_pass_t pass, const refdef_t *fd,
                                   const entity_t *ent)
{
    if (!gl_showtris || !(gl_showtris->integer & SHOWTRIS_WORLD) ||
        !vk.render_pass_active || !vk.line3d_pipeline ||
        !vk.world.mesh.vertices.buffer || !vk.world.line_indices.buffer ||
        !vk.world.line_index_count || !vk.world.batch_count ||
        !vk.world.batches || !vk.world.faces)
        return;

    bool use_marked = marked_only || vk.world.face_count;
    vk_color3d_push_t push;
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    Vector4Set(push.color, 1.0f, 0.0f, 0.0f, 1.0f);

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.line3d_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.world.mesh.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.world.line_indices.buffer, 0,
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

    if (r_lava_glowmaps && face->texinfo && strstr(face->texinfo->name, "lava") &&
        (!r_lava_glowmaps->integer ||
         (r_lava_glowmaps->integer == 1 &&
          (!face->lightmap || (face->drawflags & vk.world.nolm_mask)))))
        return false;

    return true;
}

// Lamp flicker for vk_rt_emissive_flicker. Only applied while ray tracing
// is active, but needs no ray tracing support itself.
static bool vk_world_emissive_flicker_eligible(const mface_t *face)
{
    return face && face->texinfo &&
        (face->texinfo->c.flags & SURF_LIGHT) &&
        !(face->drawflags & (SURF_TRANS_MASK | SURF_WARP | SURF_SKY));
}

static float vk_world_emissive_flicker_factor(const mface_t *face,
                                              float time)
{
    if (!vk_world_emissive_flicker_eligible(face))
        return 0.0f;

    float strength = vk_rt_emissive_flicker ?
        Cvar_ClampValue(vk_rt_emissive_flicker, 0.0f, 1.0f) : 0.0f;
    uint32_t hash = FS_HashPath(face->texinfo->name, UINT32_MAX);
    float seed0 = (hash & 0xffffu) * (1.0f / 65535.0f);
    float seed1 = ((hash >> 16) & 0xffffu) * (1.0f / 65535.0f);
    float phase0 = seed0 * (2.0f * M_PIf);
    float phase1 = seed1 * (2.0f * M_PIf);
    float wave = 1.0f + 0.10f * sinf(time * (1.35f + seed0 * 0.55f) + phase0) +
        0.05f * sinf(time * (3.70f + seed1 * 1.10f) + phase1);
    float dip_wave = 0.5f + 0.5f * sinf(time * (0.38f + seed1 * 0.14f) +
                                        phase0 * 0.37f + phase1);
    float dip = Q_clipf((dip_wave - 0.84f) / 0.16f, 0.0f, 1.0f);
    dip = dip * dip * (3.0f - 2.0f * dip);
    float target = Q_clipf(wave - dip * 0.12f, 0.72f, 1.16f);
    return 1.0f + (target - 1.0f) * strength;
}

#if USE_VULKAN_RAYTRACING
static void vk_world_material(const mface_t *face, float *reflect,
                              float *roughness)
{
    *reflect = 1.0f;
    *roughness = 0.5f;
    if (!vk.world.rt_material_file_loaded)
        return;
    const vk_rt_material_rule_t *rule = face && face->texinfo ?
        vk_rt_material_rule(face->texinfo->name) : NULL;
    if (rule) {
        *reflect = rule->reflect;
        *roughness = rule->roughness;
    } else {
        // Keep SSR useful on maps whose floor textures are not present in
        // the optional allowlist.  The shader's normal mask rejects walls;
        // liquids and other translucent faces are excluded separately.
        *reflect = 0.514f;
        *roughness = 0.70f;
    }
}

static float vk_world_pack_material(float reflect, float roughness,
                                    float skylight, float environment,
                                    float sunlight, float emissive_halo)
{
    // The secondary MRT alpha remains available alongside bloom RGB. Four
    // bits per property are sufficient for broad Quake II material classes
    // and keep reflection eligibility and roughness independent.
    unsigned reflect_q = (unsigned)(min(max(reflect, 0.0f), 1.0f) * 15.0f + 0.5f);
    unsigned roughness_q = (unsigned)(min(max(roughness, 0.0f), 1.0f) * 15.0f + 0.5f);
    unsigned skylight_q = (unsigned)(min(max(skylight, 0.0f), 1.0f) *
                                     15.0f + 0.5f);
    unsigned environment_q = (unsigned)(min(max(environment, 0.0f), 1.0f) *
                                        15.0f + 0.5f);
    unsigned sunlight_q = (unsigned)(min(max(sunlight, 0.0f), 1.0f) *
                                     15.0f + 0.5f);
    unsigned halo_q = (unsigned)(min(max(emissive_halo, 0.0f), 1.0f) *
                                 15.0f + 0.5f);
    return (float)((halo_q << 20) | (sunlight_q << 16) |
                   (environment_q << 12) |
                   (skylight_q << 8) |
                   (reflect_q << 4) | roughness_q);
}

static float vk_world_pack_rt_controls(float specular, float bounce,
                                       float caustics, float shadow_fringe,
                                       float light_ripples)
{
    // A float represents every 24-bit integer exactly. Six-bit general
    // controls and four-bit effect controls retain useful menu precision
    // while leaving the 256-byte world push constants unchanged.
    unsigned specular_q = (unsigned)(min(max(specular, 0.0f), 1.0f) *
                                     63.0f + 0.5f);
    unsigned bounce_q = (unsigned)(min(max(bounce, 0.0f), 1.0f) *
                                   63.0f + 0.5f);
    unsigned caustics_q = (unsigned)(min(max(caustics, 0.0f), 1.0f) *
                                     15.0f + 0.5f);
    unsigned fringe_q = (unsigned)(min(max(shadow_fringe, 0.0f), 1.0f) *
                                   15.0f + 0.5f);
    unsigned ripple_q = (unsigned)(min(max(light_ripples, 0.0f), 1.0f) *
                                   15.0f + 0.5f);
    return (float)(specular_q | (bounce_q << 6) | (caustics_q << 12) |
                   (fringe_q << 16) | (ripple_q << 20));
}

static float vk_world_pack_atmosphere_controls(float emissive,
                                               float scattering,
                                               float turbulence,
                                               float flicker_factor)
{
    unsigned scattering_q = (unsigned)(min(max(scattering, 0.0f), 1.0f) *
                                       15.0f + 0.5f);
    unsigned turbulence_q = (unsigned)(min(max(turbulence, 0.0f), 1.0f) *
                                       15.0f + 0.5f);
    // Zero marks an ineligible surface. Eligible factors use fixed-point
    // 1/128 units, making a disabled factor of exactly 1.0 lossless.
    unsigned flicker_q = flicker_factor > 0.0f ?
        (unsigned)(min(max(flicker_factor, 0.0f), 1.9921875f) *
                   128.0f + 0.5f) : 0;
    return min(max(emissive, 0.0f), 2.0f) +
        (float)((scattering_q << 2) | (turbulence_q << 6) |
                (flicker_q << 10));
}

int vk_world_liquid_kind(const mface_t *face)
{
    if (!face || !(face->drawflags & SURF_WARP) || !face->texinfo)
        return 0;

    const char *name = face->texinfo->name;
    if (Q_strcasestr(name, "lava"))
        return r_lava_glowmaps && !r_lava_glowmaps->integer ? 4 : 3;
    if (Q_strcasestr(name, "slime"))
        return 2;
    return 1;
}
#endif

#if USE_VULKAN_RAYTRACING
int vk_view_liquid_kind(const refdef_t *fd)
{
    if (!fd || !(fd->rdflags & RDF_UNDERWATER) ||
        !vk.world.cache || !vk.world.cache->nodes)
        return 0;

    const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes, fd->vieworg);
    int contents = leaf ? leaf->contents[0] : 0;
    if (contents & (CONTENTS_LAVA | CONTENTS_SLIME))
        return 0;
    return contents & CONTENTS_WATER ? 1 : 0;
}
#endif

static void vk_world_rt_params(float *params, bool pixel_world,
                               bool world_entity, const mface_t *face)
{
    params[0] = 0.0f;
    params[1] = 0.0f;
    params[2] = 0.0f;
    params[3] = 0.0f;
#if USE_VULKAN_RAYTRACING
    float specular = vk_rt_specular ?
        Cvar_ClampValue(vk_rt_specular, 0.0f, 1.0f) : 0.0f;
    float bounce = vk_rt_bounce ?
        Cvar_ClampValue(vk_rt_bounce, 0.0f, 1.0f) : 0.0f;
    float caustics = world_entity && vk.view_liquid_kind && vk_rt_caustics ?
        Cvar_ClampValue(vk_rt_caustics, 0.0f, 1.0f) : 0.0f;
    float shadow_fringe = world_entity && vk_rt_shadow_fringe ?
        Cvar_ClampValue(vk_rt_shadow_fringe, 0.0f, 1.0f) : 0.0f;
    float light_ripples = world_entity && vk_rt_light_ripples ?
        Cvar_ClampValue(vk_rt_light_ripples, 0.0f, 1.0f) : 0.0f;
    float skylight = world_entity && vk_rt_skylight ?
        Cvar_ClampValue(vk_rt_skylight, 0.0f, 1.0f) : 0.0f;
    float environment = world_entity && vk_rt_environment ?
        Cvar_ClampValue(vk_rt_environment, 0.0f, 1.0f) : 0.0f;
    float sunlight = world_entity && vk_rt_sunlight ?
        Cvar_ClampValue(vk_rt_sunlight, 0.0f, 1.0f) : 0.0f;
    float scattering = world_entity && vk.fd_valid &&
        !(vk.fd.rdflags & RDF_UNDERWATER) && vk_rt_light_scattering ?
        Cvar_ClampValue(vk_rt_light_scattering, 0.0f, 1.0f) : 0.0f;
    float turbulence = world_entity && vk.fd_valid &&
        !(vk.fd.rdflags & RDF_UNDERWATER) && vk_rt_fog_turbulence ?
        Cvar_ClampValue(vk_rt_fog_turbulence, 0.0f, 1.0f) : 0.0f;
    bool emissive_face = world_entity &&
        vk_world_emissive_flicker_eligible(face);
    float emissive_halo = emissive_face && vk_rt_emissive_halo ?
        Cvar_ClampValue(vk_rt_emissive_halo, 0.0f, 1.0f) : 0.0f;
    float emissive_flicker = emissive_face && vk.fd_valid ?
        vk_world_emissive_flicker_factor(face, vk.fd.time) : 0.0f;
    params[3] = vk_world_pack_rt_controls(specular, bounce, caustics,
                                          shadow_fringe, light_ripples);
    int requested_debug = vk_rt_debug ? vk_rt_debug->integer : 0;
    float material_reflect, material_roughness;
    vk_world_material(face, &material_reflect, &material_roughness);
    if (face && (face->drawflags & (SURF_TRANS_MASK | SURF_WARP))) {
        material_reflect = 0.0f;
        sunlight = 0.0f;
    }
    float packed_material = vk_world_pack_material(material_reflect,
                                                   material_roughness,
                                                   skylight, environment,
                                                   sunlight, emissive_halo);
    if (pixel_world) {
        if (requested_debug >= 1 && requested_debug <= 3) {
            params[0] = -(float)requested_debug;
        } else {
            float emissive = vk_rt_emissive ?
                Cvar_ClampValue(vk_rt_emissive, 0.0f, 2.0f) : 0.0f;
            params[0] = vk_world_pack_atmosphere_controls(emissive,
                                                          scattering,
                                                          turbulence,
                                                          emissive_flicker);
            params[1] = vk_rt_ao ?
                Cvar_ClampValue(vk_rt_ao, 0.0f, 0.5f) : 0.0f;
            if (requested_debug >= 8 && requested_debug <= 19)
                params[1] = -(float)requested_debug;
            params[2] = packed_material;
        }
    } else if (vk.raytracing_active && world_entity) {
        // This position aliases rt_enabled in vk_world_lit_push_t.
        params[0] = requested_debug >= 1 && requested_debug <= 3 ?
            -(float)requested_debug :
            vk_world_pack_atmosphere_controls(1.0f, scattering,
                                              turbulence,
                                              emissive_flicker);
        if (requested_debug >= 8 && requested_debug <= 19)
            params[1] = -(float)requested_debug;
        params[2] = packed_material;
    }

    int liquid_kind = vk.raytracing_active && world_entity ?
        vk_world_liquid_kind(face) : 0;
    if (liquid_kind) {
        // Warp faces never enter SSR. Reuse their material channel for the
        // liquid class and retain the ordinary specular control in its
        // fractional part so strength zero reproduces the previous shader.
        params[2] = -((float)liquid_kind + specular * 0.99f);
        params[3] = vk_rt_liquids ?
            Cvar_ClampValue(vk_rt_liquids, 0.0f, 1.0f) : 0.65f;
    }
#else
    (void)pixel_world;
    (void)world_entity;
    (void)face;
#endif
}

void vk_draw_world_mesh(const mat4_t mvp, bool marked_only,
                               VkPipeline pipeline, vk_world_pass_t pass,
                               float entity_alpha, const refdef_t *fd,
                               const entity_t *ent, const vec3_t axis[3])
{
    const vk_mesh_t *mesh = &vk.world.mesh;
    bool use_marked = marked_only || vk.world.face_count;
    bool special_light_mode = (vk_lightmap && vk_lightmap->integer) ||
        (vk_fullbright && vk_fullbright->integer) ||
        (vk_vertexlight && vk_vertexlight->integer);
    bool pixel_requested = vk_pixel_lightmap_mode() >= 2 &&
        pass == VK_WORLD_OPAQUE && !vk.drawing_bloom && !ent &&
        !special_light_mode;
    bool pixel_ready = vk.pixel_world_pipeline && vk.pixel_world_alpha_pipeline &&
        vk.pixel_world_fast_pipeline &&
        vk.pixel_world_glow_pipeline && vk.pixel_world_glow_alpha_pipeline &&
        vk.pixel_world_glow_fast_pipeline &&
        vk.pixel_world_pipeline_layout && vk.world.pixel_lmuv_buffer.buffer &&
        vk.world.pixel_lightmap_texture.descriptor_set;
    bool pixel_world = pixel_requested && pixel_ready;
#if USE_VULKAN_RAYTRACING
    bool ray_pixel = pixel_world && vk.raytracing_active &&
        vk.raytracing_pixel_ready &&
        vk.world.surface_lights_ready &&
        vk.rt_descriptor_set && vk.pixel_world_rt_pipeline &&
        vk.pixel_world_rt_alpha_pipeline && vk.pixel_world_rt_glow_pipeline &&
        vk.pixel_world_rt_glow_alpha_pipeline;
#else
    bool ray_pixel = false;
#endif
    bool world_batching = pass == VK_WORLD_OPAQUE && !ent && !vk.drawing_bloom;

    if (!vk.render_pass_active || !pipeline ||
        !mesh->vertices.buffer || !mesh->indices.buffer || !mesh->index_count ||
        !vk.world.batch_count || !vk.world.batches || !vk.world.faces)
        return;

    vk_draw_scope_t old_scope = vk.draw_scope;
    vk.draw_scope = VK_DRAW_WORLD;

    if (pixel_requested && !pixel_ready) {
        if (!vk_pixel_lightmaps_warned) {
            Com_WPrintf("Vulkan pixel lightmaps mode 2 needs vk_pixel_lightmaps 2 before video init; using mode 1\n");
            vk_pixel_lightmaps_warned = true;
        }
        Cvar_Set("vk_pixel_lightmaps", "1");
    }
    vk_world_pixel_push_t push = { 0 };
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    push.color[0] = 1.0f;
    push.color[1] = 1.0f;
    push.color[2] = 1.0f;
    push.color[3] = entity_alpha;
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    if (fd) {
        VectorCopy(fd->vieworg, push.dlight);
        push.dlight[3] = fd->time;
    }
    vk_fog_params(fd, push.fog);
    push.intensity = vk_texture_intensity();
    push.desaturation = 0.0f;
    push.lm_scale[0] = -1.0f;
    push.lm_scale[1] = -1.0f;
    vk_world_rt_params(push.rt_params, pixel_world, !ent, NULL);

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;
    VkBuffer pixel_buffers[] = {
        mesh->vertices.buffer,
        vk.world.pixel_lmuv_buffer.buffer,
        vk.world.rt_light_indices.buffer,
    };
    VkDeviceSize pixel_offsets[] = { 0, 0, 0 };
    uint32_t *batch_index_mapped = vk.world.batch_index_mapped;
    uint32_t batch_index_base = vk.current_image * vk.world.batch_index_capacity;
    uint32_t batch_index_cursor = 0;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                       ray_pixel ? vk.pixel_world_rt_pipeline :
                       (pixel_world ? vk.pixel_world_pipeline : pipeline));
#if USE_VULKAN_RAYTRACING
    if (vk.raytracing_active && vk.rt_descriptor_set) {
        if (ray_pixel)
            vk_bind_pixel_world_descriptor(cmd, 3, vk.rt_descriptor_set);
        else if (!pixel_world)
            vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     vk.rect_pipeline_layout, 1, 1,
                                     &vk.rt_descriptor_set, 0, NULL);
    }
#endif
    if (pixel_world)
        vk_bind_vertex_buffers(cmd, 0, ray_pixel ? 3 : 2,
                               pixel_buffers, pixel_offsets);
    else
        vk_bind_vertex_buffers(cmd, 0, 1, &mesh->vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    if (pixel_world) {
        vk_bind_pixel_world_descriptor(cmd, 1,
                                       vk.world.pixel_lightmap_texture.descriptor_set);
        vk_push_pixel_world_constants(cmd, sizeof(push), &push);
    } else {
        vk_push_constants(cmd, sizeof(vk_world_lit_push_t), &push);
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
        bool group_glow = false;
        VkPipeline group_pipeline = ray_pixel ? vk.pixel_world_rt_pipeline :
            (pixel_world ? vk.pixel_world_pipeline : pipeline);
        const vk_texture_t *group_glow_texture = NULL;

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
                    vk_bind_index_buffer(cmd, vk.world.batch_indices.buffer, bind_offset, VK_INDEX_TYPE_UINT32); \
                } \
                vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, group_pipeline); \
                if (pixel_world) { \
                    vk_bind_pixel_world_descriptor(cmd, 0, texture->descriptor_set); \
                    vk_bind_pixel_world_descriptor(cmd, 1, \
                        vk.world.pixel_lightmap_texture.descriptor_set); \
                    if (group_glow && group_glow_texture) \
                        vk_bind_pixel_world_descriptor(cmd, 2, \
                            group_glow_texture->descriptor_set); \
                    vk_push_pixel_world_constants(cmd, sizeof(push), &push); \
                } else { \
                    vk_bind_texture_descriptor(cmd, texture->descriptor_set); \
                    vk_push_constants(cmd, sizeof(vk_world_lit_push_t), &push); \
                } \
                if (draw_group) \
                    vk.CmdDrawIndexed(cmd, group_count, 1, 0, 0, 0); \
                vk_bind_index_buffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32); \
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

            // Skipped faces do not change any GPU state. Keep an active group
            // open across BSP visibility gaps so all compatible visible faces
            // in this texture batch remain in one indexed draw.
            if (!face->face || !face->face->texinfo || !face->face->plane)
                continue;
            if (use_marked && face->face->drawframe != vk.world.drawframe)
                continue;
            if (!ent && fd &&
                vk_world_face_backfacing(face->face, fd->vieworg)) {
                c.facesCulled++;
                continue;
            }
            if (!vk_world_face_in_pass(face->face, pass))
                continue;

            const image_t *image = vk_world_face_image(face->face, fd, ent);
            if (!image || image->texnum >= MAX_RIMAGES)
                continue;

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
                if (vk.raytracing_active &&
                    vk_world_emissive_flicker_eligible(face->face))
                    push.intensity *= vk_world_emissive_flicker_factor(
                        face->face, fd ? fd->time : 0.0f);
                push.color[0] = 1.0f;
                push.color[1] = 1.0f;
                push.color[2] = 1.0f;
                push.color[3] = entity_alpha * vk_world_face_alpha(face->face);
                Vector4Clear(push.dlight);
                vk_world_face_scroll(face->face, fd ? fd->time : 0.0f, push.scroll);
                vk_push_constants(cmd, sizeof(vk_world_lit_push_t), &push);
                vk.CmdDrawIndexed(cmd, face->index_count, 1, face->first_index, 0, 0);
                c.trisDrawn += face->index_count / 3;
                vk_count_batch3d();
                continue;
            }

            // Lamp bloom (gl_bloom_lamps) is a per-face push constant, and
            // one texture can be used both with and without SURF_LIGHT, so
            // such faces are drawn on their own rather than inheriting the
            // first face's setting in a batch.
            bool static_lamp = vk_world_lamp_bloom_enabled() &&
                (face->face->texinfo->c.flags & SURF_LIGHT) &&
                !image->texnum2 &&
                !(face->face->drawflags &
                  (SURF_TRANS_MASK | SURF_WARP | SURF_NODRAW));
            bool can_group = world_batching && !static_lamp &&
                batch_index_mapped && vk.world.batch_indices.buffer &&
                vk.world.batch_index_data &&
                image->texnum == batch->texture_index &&
                !(face->face->drawflags & (SURF_ALPHATEST | SURF_FLOWING |
                                           SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y |
                                           SURF_WARP | SURF_TRANS33 | SURF_TRANS66)) &&
                !(face->face->drawflags & vk.world.nolm_mask) &&
                (!pixel_world || (face->pixel_lm_w && face->pixel_lm_h)) &&
                (pixel_world ||
                 !vk_world_face_glowmap_enabled(face->face, image));

            if (can_group) {
                can_group = !vk_world_lights(face, fd, ent, axis, NULL, NULL);
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
                    if (fd)
                        VectorCopy(fd->vieworg, push.dlight);
                    push.dlight[3] = fd ? fd->time : 0.0f;
                    push.intensity = vk_texture_intensity();
                    push.desaturation = vk_world_face_desaturation(face->face);
                    group_glow = pixel_world &&
                        vk_world_face_glowmap_enabled(face->face, image);
                    group_glow_texture = group_glow ?
                        vk_texture_for_index(image->texnum2, false) : NULL;
                    group_glow = group_glow_texture != NULL;
                    if (ray_pixel)
                        group_pipeline = group_glow ?
                            vk.pixel_world_rt_glow_pipeline :
                            vk.pixel_world_rt_pipeline;
                    else
                        group_pipeline = group_glow ?
                            vk.pixel_world_glow_fast_pipeline :
                            (pixel_world ? vk.pixel_world_fast_pipeline : pipeline);
                    push.lm_scale[0] = pixel_world ? 1.0f : -1.0f;
                    push.lm_scale[1] = pixel_world ? 1.0f : -1.0f;
                    vk_world_rt_params(push.rt_params, pixel_world, !ent, face->face);
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

            bool combined_glow = pixel_world &&
                vk_world_face_glowmap_enabled(face->face, image);
            const vk_texture_t *combined_glow_texture = NULL;
            if (combined_glow) {
                combined_glow_texture =
                    vk_texture_for_index(image->texnum2, false);
                combined_glow = combined_glow_texture != NULL;
            }

            if (pixel_world) {
                if (combined_glow) {
                    if (ray_pixel)
                        face_pipeline = (face->face->drawflags & SURF_ALPHATEST) ?
                            vk.pixel_world_rt_glow_alpha_pipeline :
                            vk.pixel_world_rt_glow_pipeline;
                    else
                        face_pipeline = (face->face->drawflags & SURF_ALPHATEST) ?
                            vk.pixel_world_glow_alpha_pipeline :
                            vk.pixel_world_glow_pipeline;
                    vk_bind_pixel_world_descriptor(cmd, 2,
                                                   combined_glow_texture->descriptor_set);
                } else {
                    if (ray_pixel)
                        face_pipeline = (face->face->drawflags & SURF_ALPHATEST) ?
                            vk.pixel_world_rt_alpha_pipeline :
                            vk.pixel_world_rt_pipeline;
                    else
                        face_pipeline = (face->face->drawflags & SURF_ALPHATEST) ?
                            vk.pixel_world_alpha_pipeline : vk.pixel_world_pipeline;
                }
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
            Vector4Clear(push.dlight);
            if (fd)
                VectorCopy(fd->vieworg, push.dlight);
            vk_world_lights(face, fd, ent, axis,
                            push.dlight_origins, push.dlight_colors);
            push.dlight[3] = fd ? fd->time : 0.0f;
            push.intensity = vk_world_face_intensity(face->face);
            push.desaturation = vk_world_face_desaturation(face->face);
            push.color[3] = entity_alpha * vk_world_face_alpha(face->face);
            if (pixel_world && face->pixel_lm_w && face->pixel_lm_h) {
                push.lm_scale[0] = 1.0f;
                push.lm_scale[1] = 1.0f;
            } else {
                // A negative scale tells the pixel shaders that the face has
                // no lightmap (liquids, for example) and is drawn fully lit.
                push.lm_scale[0] = -1.0f;
                push.lm_scale[1] = -1.0f;
            }
            vk_world_rt_params(push.rt_params, pixel_world, !ent, face->face);
            if (pixel_world)
                vk_push_pixel_world_constants(cmd, sizeof(push), &push);
            else
                vk_push_constants(cmd, sizeof(vk_world_lit_push_t), &push);
            vk.CmdDrawIndexed(cmd, face->index_count, 1, face->first_index, 0, 0);
            c.facesDrawn++;
            c.facesTris += face->index_count / 3;
            c.trisDrawn += face->index_count / 3;
            vk_count_batch3d();

            if (!combined_glow &&
                (pass == VK_WORLD_OPAQUE ||
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
                    if (vk.raytracing_active &&
                        vk_world_emissive_flicker_eligible(face->face))
                        push.intensity *= vk_world_emissive_flicker_factor(
                            face->face, fd ? fd->time : 0.0f);
                    push.color[0] = 1.0f;
                    push.color[1] = 1.0f;
                    push.color[2] = 1.0f;
                    push.color[3] = entity_alpha * vk_world_face_alpha(face->face);
                    vk_push_constants(cmd, sizeof(vk_world_lit_push_t), &push);
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

void vk_draw_fx_outlines(const vk_mesh_t *mesh, const vk_buffer_t *indices,
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
    vk_bind_index_buffer(cmd, indices->buffer, 0, VK_INDEX_TYPE_UINT32);
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

void vk_draw_skybox(const refdef_t *fd)
{
    if (vk_drawsky && !vk_drawsky->integer)
        return;
    if (!vk.world.sky_visible)
        return;
    if (!vk.render_pass_active || !vk.sky_pipeline ||
        !vk.skybox.vertices.buffer || !vk.skybox.indices.buffer)
        return;

    mat4_t mvp;
    vk_world_push_t push = { 0 };
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
    vk_bind_index_buffer(cmd, vk.skybox.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);

    for (uint32_t face = 0; face < 6; face++) {
        if (vk.sky_cubemap >= 0 && vk.sky_cubemap < VK_MAX_CUBEMAPS) {
            const vk_cubemap_t *cubemap = &vk.cubemaps[vk.sky_cubemap];
            const vk_texture_t *texture = &cubemap->faces[face];
            if (cubemap->image && texture->descriptor_set) {
                vk_bind_texture_descriptor(cmd, texture->descriptor_set);
                vk.CmdDrawIndexed(cmd, 6, 1, face * 6, 0, 0);
                c.trisDrawn += 2;
                vk_count_batch3d();
            }
            continue;
        }

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

void vk_setup_world_frustum(const refdef_t *fd)
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

void vk_update_world_view(const refdef_t *fd)
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

void vk_mark_world_faces(const refdef_t *fd)
{
    const bsp_t *bsp = vk.world.cache;
    int clipflags;

    if (!bsp || !bsp->nodes)
        return;

    vk.world.drawframe++;
    // The skybox is also the background for translucent portals. A sky face
    // behind one can be outside the visible-leaf walk, so keep the map sky
    // available and let opaque geometry mask it.
    vk.world.sky_visible = vk.world.has_sky;
    vk_update_world_view(fd);
    if (!vk_lockpvs || !vk_lockpvs->integer) {
        vk.world.visframe++;
        vk_mark_world_visible_nodes(fd);
    }
    vk_setup_world_frustum(fd);

    clipflags = (!vk_cull_nodes || vk_cull_nodes->integer) ?
        VK_NODE_CLIPPED : VK_NODE_UNCLIPPED;
    vk_mark_world_node_faces(bsp->nodes, fd, clipflags);
}

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

        if (!face->texinfo || !face->plane)
            continue;
        if (face->drawflags & (SURF_SKY | SURF_NODRAW))
            continue;
        if (!translucent && (face->drawflags & SURF_TRANS_MASK))
            continue;
        if (!face->texinfo->image ||
            face->texinfo->image->texnum >= MAX_RIMAGES)
            continue;

        if (vk_world_face_backfacing(face, transformed)) {
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

void vk_draw_bmodel(const entity_t *ent, const refdef_t *fd,
                           bool translucent_faces)
{
    bsp_t *bsp = vk.world.cache;
    int index = ~ent->model;
    mmodel_t *model;
    vec3_t axis[3];
    mat4_t mvp;
    unsigned world_drawframe;

    if (!bsp || index < 1 || index >= bsp->nummodels)
        return;
    model = &bsp->models[index];
    if (!model->numfaces)
        return;

    vk_entity_axis(ent, axis);
    if (vk_bmodel_culled(model, ent, axis))
        return;

    vk_entity_mvp(mvp, fd, ent, axis);

    // Brush models share the BSP face array with the static world, but their
    // visibility marks are consumed immediately.  Preserve the static-world
    // generation so its PVS/frustum result remains valid for the alpha pass.
    world_drawframe = vk.world.drawframe;
    vk.world.drawframe++;
    bool translucent = (ent->flags & RF_TRANSLUCENT) || translucent_faces;
    vk_world_pass_t world_pass = translucent_faces ? VK_WORLD_ALPHA :
        (translucent ? VK_WORLD_ENTITY_ALPHA : VK_WORLD_OPAQUE);
    vk_mark_bmodel_faces(model, ent, fd, axis, translucent);
    vk_draw_world_mesh(mvp, true,
                        translucent ? vk.world_blend_pipeline : vk.world_pipeline,
                       world_pass,
                       (ent->flags & RF_TRANSLUCENT) ? ent->alpha : 1.0f,
                       fd, ent, axis);
    vk_draw_world_outlines(mvp, true, world_pass, fd, ent);
    for (int i = 0; i < model->numfaces; i++) {
        mface_t *face = model->firstface + i;
        if (face->drawframe == vk.world.drawframe)
            face->drawframe = 0;
    }
    vk.world.drawframe = world_drawframe;
}

// A light style index is a byte, so it is always below MAX_LIGHTSTYLES.
_Static_assert(MAX_LIGHTSTYLES >= 256, "light style indices are bytes");
float vk_lightstyle_value(const refdef_t *fd, byte style)
{
    if (!fd || !fd->lightstyles)
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
    int s1, t1;
    int size = smax * tmax * 3;
    float fracu = point->s - s;
    float fracv = point->t - t;
    float w1 = (1.0f - fracu) * (1.0f - fracv);
    float w2 = fracu * (1.0f - fracv);
    float w3 = fracu * fracv;
    float w4 = (1.0f - fracu) * fracv;

    VectorClear(color);

    if (!lightmap || smax < 1 || tmax < 1 || s < 0 || t < 0 ||
        s >= smax || t >= tmax)
        return;

    s1 = min(s + 1, smax - 1);
    t1 = min(t + 1, tmax - 1);

    for (int i = 0; i < surf->numstyles; i++) {
        const byte *b1 = &lightmap[3 * ((t + 0) * smax + (s + 0))];
        const byte *b2 = &lightmap[3 * ((t + 0) * smax + s1)];
        const byte *b3 = &lightmap[3 * (t1 * smax + s1)];
        const byte *b4 = &lightmap[3 * (t1 * smax + (s + 0))];

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

    if (!grid->numleafs || (vk_lightgrid && !vk_lightgrid->integer))
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

bool vk_static_light_point(const vec3_t origin, const refdef_t *fd,
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

#endif // USE_VULKAN
