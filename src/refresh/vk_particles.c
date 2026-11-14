/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan particles and the ray-traced particle effects (dust, embers, shockwaves, splashes, wakes, impacts and more).

#include "vk_local.h"

#if USE_VULKAN

#define VK_PARTICLE_SIZE    (1.0f + M_SQRT1_2f)
#define VK_PARTICLE_SCALE   (1.0f / (2.0f * VK_PARTICLE_SIZE))

typedef struct {
    vec3_t origin;
    uint32_t hash;
} vk_dust_mote_t;

typedef struct {
    dlight_t light;
    float score;
} vk_dust_light_t;

#define VK_MAX_DUST_LIGHTS 3
#define VK_MAX_EMISSIVE_MOTE_SOURCES 8
#define VK_MAX_EMISSIVE_MOTES 24
#define VK_MAX_LIGHT_CORONAS 6

static uint32_t vk_dust_hash(int x, int y, int z)
{
    uint32_t hash = (uint32_t)x * 0x8da6b343u;
    hash ^= (uint32_t)y * 0xd8163841u;
    hash ^= (uint32_t)z * 0xcb1ab31fu;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    return hash ^ (hash >> 16);
}

static float vk_dust_random(uint32_t hash)
{
    return (hash & 0xffffu) * (1.0f / 65535.0f);
}

static int vk_insert_dust_light(const refdef_t *fd, const dlight_t *light,
                                vk_dust_light_t lights[VK_MAX_DUST_LIGHTS],
                                int count)
{
    if (!fd || !light || !vk.world.cache || !vk.world.cache->nodes)
        return count;

    float peak = max(light->color[0],
                     max(light->color[1], light->color[2]));
    float reach = light->intensity - DLIGHT_CUTOFF;
    float camera_distance = Distance(light->origin, fd->vieworg);
    if (peak <= 0.10f || reach <= 0.0f || camera_distance >= reach + 400.0f)
        return count;

    const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes, light->origin);
    if (!leaf || leaf->visframe != vk.world.visframe ||
        (leaf->contents[0] & (CONTENTS_SOLID | MASK_WATER)) ||
        (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
        return count;

    // This is the strongest contribution the light can make anywhere inside
    // the camera's bounded dust volume.
    float score = reach - max(camera_distance - 400.0f, 0.0f);
    if (score <= 0.0f)
        return count;

    int slot = min(count, VK_MAX_DUST_LIGHTS - 1);
    while (slot > 0 && score > lights[slot - 1].score) {
        lights[slot] = lights[slot - 1];
        slot--;
    }
    if (count >= VK_MAX_DUST_LIGHTS && score <= lights[slot].score)
        return count;

    lights[slot].light = *light;
    lights[slot].score = score;
    return count < VK_MAX_DUST_LIGHTS ? count + 1 : count;
}

static int vk_select_dust_lights(const refdef_t *fd,
                                 vk_dust_light_t lights[VK_MAX_DUST_LIGHTS])
{
    memset(lights, 0, sizeof(*lights) * VK_MAX_DUST_LIGHTS);
    if (!fd || !vk_rt_dust_lighting ||
        Cvar_ClampValue(vk_rt_dust_lighting, 0.0f, 1.0f) <= 0.0f ||
        !vk_dynamic_lights_enabled())
        return 0;

    int count = 0;
    for (int i = 0; fd->dlights && i < fd->num_dlights; i++)
        count = vk_insert_dust_light(fd, &fd->dlights[i], lights, count);

    float duration = vk_rt_afterglow ?
        Cvar_ClampValue(vk_rt_afterglow, 0.0f, 1.0f) : 0.0f;
    if (duration <= 0.0f)
        return count;

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
        count = vk_insert_dust_light(fd, &light, lights, count);
    }
    return count;
}

static void vk_light_dust_mote(const vec3_t origin,
                               const vk_dust_light_t *lights,
                               int light_count, float strength,
                               float color[4])
{
    vec3_t response = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < light_count; i++) {
        const dlight_t *light = &lights[i].light;
        float reach = max(light->intensity - DLIGHT_CUTOFF, 1.0f);
        float fraction = Q_clipf((reach - Distance(light->origin, origin)) /
                                 reach, 0.0f, 1.0f);
        fraction = fraction * fraction * (3.0f - 2.0f * fraction);
        fraction *= min(reach / 255.0f, 1.0f);
        VectorMA(response, fraction, light->color, response);
    }

    float energy = Q_clipf(max(response[0],
                         max(response[1], response[2])), 0.0f, 1.0f);
    float brighten = 1.0f + energy * strength * 0.25f;
    for (int i = 0; i < 3; i++)
        color[i] = Q_clipf(color[i] * brighten +
                           response[i] * strength * 0.55f, 0.0f, 1.0f);
    color[3] = min(color[3] * (1.0f + energy * strength * 0.45f), 0.34f);
}

// The ray-traced sprites (dust, coronas, motes, embers) were tuned on quads
// spanning scale + scale * VK_PARTICLE_SCALE; this keeps that size now that
// the quad is centered and sized by its half-width.
#define VK_RT_SPRITE_HALF(scale) ((scale) * (1.0f + VK_PARTICLE_SCALE) * 0.5f)

// Appends a camera-facing quad of half-width half_size around origin, with
// the round particle texture stretched across it.
static void vk_append_particle_quad(uint32_t *vertex_count,
                                    const vec3_t origin, float half_size,
                                    const float color[4],
                                    const vec3_t viewaxis[3])
{
    vec3_t left, right, down, up, corners[4];

    VectorScale(viewaxis[1], half_size, left);
    VectorScale(viewaxis[1], -half_size, right);
    VectorScale(viewaxis[2], -half_size, down);
    VectorScale(viewaxis[2], half_size, up);
    VectorAdd3(origin, left, down, corners[0]);
    VectorAdd3(origin, right, down, corners[1]);
    VectorAdd3(origin, left, up, corners[2]);
    VectorAdd3(origin, right, up, corners[3]);

    static const int order[6] = { 0, 2, 1, 1, 2, 3 };
    static const float uv[4][2] = {
        { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f },
    };
    for (int j = 0; j < 6; j++) {
        int idx = order[j];
        vk_vertex_t *v = &vk.particle_batch[(*vertex_count)++];
        VectorCopy(corners[idx], v->position);
        v->uv[0] = uv[idx][0];
        v->uv[1] = uv[idx][1];
        Vector4Copy(color, v->color);
    }
}

#if USE_VULKAN_RAYTRACING
static bool vk_append_effect_segment(uint32_t *vertex_count,
                                     const vec3_t start, const vec3_t end,
                                     const vec3_t vieworg, float width,
                                     const float color[4])
{
    vec3_t direction, midpoint, to_view, side, fallback, corners[4];
    VectorSubtract(end, start, direction);
    if (VectorNormalize(direction) <= 0.1f)
        return false;

    VectorAvg(start, end, midpoint);
    VectorSubtract(vieworg, midpoint, to_view);
    CrossProduct(direction, to_view, side);
    if (VectorNormalize(side) <= 0.1f)
        MakeNormalVectors(direction, side, fallback);
    VectorScale(side, width, side);

    VectorSubtract(start, side, corners[0]);
    VectorAdd(start, side, corners[1]);
    VectorSubtract(end, side, corners[2]);
    VectorAdd(end, side, corners[3]);

    static const int order[6] = { 0, 2, 1, 1, 2, 3 };
    static const float uv[4][2] = {
        { 0.0f, 0.0f }, { 0.0f, 1.0f },
        { 1.0f, 0.0f }, { 1.0f, 1.0f },
    };
    for (int i = 0; i < 6; i++) {
        int index = order[i];
        vk_vertex_t *vertex = &vk.particle_batch[(*vertex_count)++];
        VectorCopy(corners[index], vertex->position);
        vertex->uv[0] = uv[index][0];
        vertex->uv[1] = uv[index][1];
        Vector4Copy(color, vertex->color);
    }
    return true;
}

static void vk_append_effect_quad(uint32_t *vertex_count,
                                  const vec3_t origin, float radius,
                                  const float color[4],
                                  const vec3_t viewaxis[3])
{
    vec3_t left, up, corners[4];
    VectorScale(viewaxis[1], radius, left);
    VectorScale(viewaxis[2], radius, up);
    VectorAdd3(origin, left, up, corners[0]);
    VectorSubtract(origin, left, corners[1]);
    VectorAdd(corners[1], up, corners[1]);
    VectorAdd(origin, left, corners[2]);
    VectorSubtract(corners[2], up, corners[2]);
    VectorSubtract(origin, left, corners[3]);
    VectorSubtract(corners[3], up, corners[3]);

    static const int order[6] = { 0, 1, 2, 2, 1, 3 };
    static const float uv[4][2] = {
        { 0.0f, 0.0f }, { 1.0f, 0.0f },
        { 0.0f, 1.0f }, { 1.0f, 1.0f },
    };
    for (int i = 0; i < 6; i++) {
        int index = order[i];
        vk_vertex_t *vertex = &vk.particle_batch[(*vertex_count)++];
        VectorCopy(corners[index], vertex->position);
        vertex->uv[0] = uv[index][0];
        vertex->uv[1] = uv[index][1];
        Vector4Copy(color, vertex->color);
    }
}

static void vk_append_oriented_effect_quad(uint32_t *vertex_count,
                                           const vec3_t origin,
                                           const vec3_t normal, float radius,
                                           const float color[4])
{
    vec3_t right, up, center, corners[4];
    MakeNormalVectors(normal, right, up);
    VectorMA(origin, 0.75f, normal, center);
    VectorScale(right, radius, right);
    VectorScale(up, radius, up);
    VectorAdd3(center, right, up, corners[0]);
    VectorSubtract(center, right, corners[1]);
    VectorAdd(corners[1], up, corners[1]);
    VectorAdd(center, right, corners[2]);
    VectorSubtract(corners[2], up, corners[2]);
    VectorSubtract(center, right, corners[3]);
    VectorSubtract(corners[3], up, corners[3]);

    static const int order[6] = { 0, 1, 2, 2, 1, 3 };
    static const float uv[4][2] = {
        { 0.0f, 0.0f }, { 1.0f, 0.0f },
        { 0.0f, 1.0f }, { 1.0f, 1.0f },
    };
    for (int i = 0; i < 6; i++) {
        int index = order[i];
        vk_vertex_t *vertex = &vk.particle_batch[(*vertex_count)++];
        VectorCopy(corners[index], vertex->position);
        vertex->uv[0] = uv[index][0];
        vertex->uv[1] = uv[index][1];
        Vector4Copy(color, vertex->color);
    }
}

static void vk_append_oriented_effect_ellipse(uint32_t *vertex_count,
                                              const vec3_t origin,
                                              const vec3_t normal,
                                              const vec3_t direction,
                                              float major_radius,
                                              float minor_radius,
                                              const float color[4])
{
    vec3_t tangent, bitangent, center, corners[4], fallback;
    VectorMA(direction, -DotProduct(direction, normal), normal, tangent);
    if (VectorNormalize(tangent) <= 0.1f)
        MakeNormalVectors(normal, tangent, fallback);
    CrossProduct(normal, tangent, bitangent);
    if (VectorNormalize(bitangent) <= 0.1f)
        return;

    VectorMA(origin, 0.75f, normal, center);
    VectorScale(tangent, major_radius, tangent);
    VectorScale(bitangent, minor_radius, bitangent);
    VectorAdd3(center, tangent, bitangent, corners[0]);
    VectorSubtract(center, tangent, corners[1]);
    VectorAdd(corners[1], bitangent, corners[1]);
    VectorAdd(center, tangent, corners[2]);
    VectorSubtract(corners[2], bitangent, corners[2]);
    VectorSubtract(center, tangent, corners[3]);
    VectorSubtract(corners[3], bitangent, corners[3]);

    static const int order[6] = { 0, 1, 2, 2, 1, 3 };
    static const float uv[4][2] = {
        { 0.0f, 0.0f }, { 1.0f, 0.0f },
        { 0.0f, 1.0f }, { 1.0f, 1.0f },
    };
    for (int i = 0; i < 6; i++) {
        int index = order[i];
        vk_vertex_t *vertex = &vk.particle_batch[(*vertex_count)++];
        VectorCopy(corners[index], vertex->position);
        vertex->uv[0] = uv[index][0];
        vertex->uv[1] = uv[index][1];
        Vector4Copy(color, vertex->color);
    }
}
#endif

static uint32_t vk_append_dust_motes(const refdef_t *fd,
                                     const vec3_t viewaxis[3],
                                     uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_dust ||
        (fd->rdflags & (RDF_UNDERWATER | RDF_NOWORLDMODEL)) ||
        vk.view_liquid_kind || !vk.world.cache || !vk.world.cache->nodes)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_dust, 0.0f, 1.0f);
    uint32_t available = (VK_MAX_PARTICLE_VERTICES - vertex_count) / 6;
    uint32_t wanted = min((uint32_t)VK_MAX_DUST_MOTES, available);
    if (!wanted)
        return vertex_count;

    enum { CELL_SIZE = 160, CELL_RADIUS = 4, Z_RADIUS = 2 };
    vk_dust_mote_t motes[VK_MAX_DUST_MOTES];
    vk_dust_light_t lights[VK_MAX_DUST_LIGHTS];
    int light_count = vk_select_dust_lights(fd, lights);
    float light_strength = vk_rt_dust_lighting ?
        Cvar_ClampValue(vk_rt_dust_lighting, 0.0f, 1.0f) : 0.0f;
    uint32_t mote_count = 0;
    int center[3] = {
        (int)floorf(fd->vieworg[0] / CELL_SIZE),
        (int)floorf(fd->vieworg[1] / CELL_SIZE),
        (int)floorf(fd->vieworg[2] / CELL_SIZE),
    };

    for (int z = -Z_RADIUS; z <= Z_RADIUS; z++) {
        for (int y = -CELL_RADIUS; y <= CELL_RADIUS; y++) {
            for (int x = -CELL_RADIUS; x <= CELL_RADIUS; x++) {
                int cell[3] = { center[0] + x, center[1] + y, center[2] + z };
                uint32_t hash = vk_dust_hash(cell[0], cell[1], cell[2]);
                vec3_t origin, delta;

                // Density is decided independently per world cell. New cells
                // therefore enter at the faded edge instead of replacing a
                // fully visible mote when the camera crosses a cell boundary.
                if (vk_dust_random(vk_dust_hash(cell[2], cell[0], cell[1])) >=
                    strength * 0.60f)
                    continue;

                origin[0] = (cell[0] + vk_dust_random(hash)) * CELL_SIZE;
                origin[1] = (cell[1] + vk_dust_random(hash >> 8)) * CELL_SIZE;
                origin[2] = (cell[2] + vk_dust_random(hash >> 16)) * CELL_SIZE;
                origin[0] += sinf(fd->time * (0.11f + vk_dust_random(hash) * 0.08f) +
                                  vk_dust_random(hash >> 4) * (2.0f * M_PIf)) * 10.0f;
                origin[1] += cosf(fd->time * (0.09f + vk_dust_random(hash >> 7) * 0.07f) +
                                  vk_dust_random(hash >> 12) * (2.0f * M_PIf)) * 8.0f;
                origin[2] += sinf(fd->time * (0.08f + vk_dust_random(hash >> 13) * 0.06f) +
                                  vk_dust_random(hash >> 2) * (2.0f * M_PIf)) * 12.0f;

                VectorSubtract(origin, fd->vieworg, delta);
                float distance = VectorLength(delta);
                if (distance < 32.0f || distance >= 400.0f)
                    continue;

                const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes, origin);
                if (!leaf || leaf->visframe != vk.world.visframe ||
                    (leaf->contents[0] & (CONTENTS_SOLID | MASK_WATER)) ||
                    (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
                    continue;

                VectorCopy(origin, motes[mote_count].origin);
                motes[mote_count++].hash = hash;
                if (mote_count == wanted)
                    goto motes_ready;
            }
        }
    }

motes_ready:
    for (uint32_t i = 0; i < mote_count; i++) {
        vec3_t delta;
        VectorSubtract(motes[i].origin, fd->vieworg, delta);
        float distance = VectorLength(delta);
        float fade = Q_clipf((400.0f - distance) / 100.0f, 0.0f, 1.0f);
        float tint = vk_dust_random(motes[i].hash >> 5);
        float color[4] = {
            0.72f + tint * 0.12f,
            0.70f + tint * 0.10f,
            0.64f + tint * 0.08f,
            (0.16f + 0.10f * strength) * fade,
        };
        if (light_count)
            vk_light_dust_mote(motes[i].origin, lights, light_count,
                               light_strength, color);
        float scale = 1.8f + distance * 0.003f +
            vk_dust_random(motes[i].hash >> 11) * 0.8f;
        vk_append_particle_quad(&vertex_count, motes[i].origin, VK_RT_SPRITE_HALF(scale),
                                color, viewaxis);
    }
    return vertex_count;
}

#if USE_VULKAN_RAYTRACING
typedef struct {
    const entity_t *entity;
    vec3_t normal;
    float phase;
    float distance;
    float facing;
    uint32_t seed;
} vk_rail_ionization_candidate_t;

static float vk_rail_ionization_envelope(float phase)
{
    float birth = Q_clipf(phase / 0.10f, 0.0f, 1.0f);
    birth = birth * birth * (3.0f - 2.0f * birth);
    float fade = Q_clipf((1.0f - phase) / 0.35f, 0.0f, 1.0f);
    fade = fade * fade * (3.0f - 2.0f * fade);
    return birth * fade;
}

static uint32_t vk_select_rail_ionizations(
    const refdef_t *fd, vk_rail_ionization_candidate_t candidates[],
    uint32_t capacity)
{
    if (!fd || !capacity || !vk.raytracing_active || !vk_rt_rail_impact ||
        Cvar_ClampValue(vk_rt_rail_impact, 0.0f, 1.0f) <= 0.0f ||
        !fd->entities || fd->num_entities <= 0 ||
        (fd->rdflags & RDF_NOWORLDMODEL) ||
        !vk.world.cache || !vk.world.cache->nodes)
        return 0;

    uint32_t count = 0;
    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *entity = &fd->entities[i];
        if ((entity->flags & (RF_EFFECT_ONLY | RF_RT_RAIL_IONIZATION)) !=
            (RF_EFFECT_ONLY | RF_RT_RAIL_IONIZATION))
            continue;

        float phase = (entity->oldframe + 1.0f -
            Q_clipf(entity->backlerp, 0.0f, 1.0f)) / 3.0f;
        if (phase < 0.0f || phase >= 1.0f)
            continue;

        float distance = Distance(entity->origin, fd->vieworg);
        if (distance < 6.0f || distance >= 1024.0f)
            continue;

        vec3_t normal, to_view, leaf_point;
        VectorCopy(entity->oldorigin, normal);
        if (VectorNormalize(normal) <= 0.0f)
            continue;
        VectorSubtract(fd->vieworg, entity->origin, to_view);
        if (VectorNormalize(to_view) <= 0.0f)
            continue;
        float facing = DotProduct(normal, to_view);
        if (facing <= 0.02f)
            continue;

        VectorMA(entity->origin, 2.0f, normal, leaf_point);
        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes, leaf_point);
        if (!leaf || leaf->visframe != vk.world.visframe ||
            (leaf->contents[0] & CONTENTS_SOLID) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        uint32_t slot = min(count, capacity - 1);
        if (count >= capacity && distance >= candidates[slot].distance)
            continue;
        while (slot > 0 && distance < candidates[slot - 1].distance) {
            candidates[slot] = candidates[slot - 1];
            slot--;
        }
        candidates[slot].entity = entity;
        VectorCopy(normal, candidates[slot].normal);
        candidates[slot].phase = phase;
        candidates[slot].distance = distance;
        candidates[slot].facing = facing;
        candidates[slot].seed = vk_dust_hash(
            (int)floorf(entity->origin[0] * 0.125f),
            (int)floorf(entity->origin[1] * 0.125f),
            (int)floorf(entity->origin[2] * 0.125f));
        if (count < capacity)
            count++;
    }
    return count;
}

static uint32_t vk_append_rail_ionization_pulses(const refdef_t *fd,
                                                 uint32_t vertex_count)
{
    if (!vk.impact_texture.descriptor_set)
        return vertex_count;

    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 6,
                             (uint32_t)VK_MAX_RAIL_IONIZATIONS);
    vk_rail_ionization_candidate_t candidates[VK_MAX_RAIL_IONIZATIONS];
    uint32_t count = vk_select_rail_ionizations(fd, candidates, available);
    if (!count)
        return vertex_count;
    float strength = Cvar_ClampValue(vk_rt_rail_impact, 0.0f, 1.0f);
    for (uint32_t i = 0; i < count; i++) {
        const vk_rail_ionization_candidate_t *candidate = &candidates[i];
        const entity_t *entity = candidate->entity;
        float envelope = vk_rail_ionization_envelope(candidate->phase);
        float far_fade = Q_clipf((1024.0f - candidate->distance) / 256.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf((candidate->distance - 6.0f) / 24.0f,
                                  0.0f, 1.0f);
        float facing_fade = Q_clipf((candidate->facing - 0.02f) / 0.28f,
                                    0.0f, 1.0f);
        float color[4];
        for (int channel = 0; channel < 3; channel++) {
            color[channel] = 0.5f * (entity->rgba.u8[channel] / 255.0f +
                                     Q_clipf(entity->angles[channel],
                                             0.0f, 1.0f));
            color[channel] = min(color[channel] * 0.82f + 0.18f, 1.0f);
        }
        float source_alpha = 0.5f * (entity->rgba.u8[3] / 255.0f +
                                     Q_clipf(entity->scale, 0.0f, 1.0f));
        color[3] = min((0.16f + 0.28f * strength) * envelope * far_fade *
                       near_fade * facing_fade * source_alpha, 0.44f);
        float radius = (6.0f + candidate->phase * 16.0f) *
            (0.92f + strength * 0.12f);
        vk_append_oriented_effect_quad(&vertex_count, entity->origin,
                                       candidate->normal, radius, color);
    }
    return vertex_count;
}

static uint32_t vk_append_rail_ionization_filaments(const refdef_t *fd,
                                                    uint32_t vertex_count)
{
    if (!vk.beam_texture.descriptor_set)
        return vertex_count;

    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 18,
                             (uint32_t)VK_MAX_RAIL_IONIZATIONS);
    vk_rail_ionization_candidate_t candidates[VK_MAX_RAIL_IONIZATIONS];
    uint32_t count = vk_select_rail_ionizations(fd, candidates, available);
    if (!count)
        return vertex_count;
    float strength = Cvar_ClampValue(vk_rt_rail_impact, 0.0f, 1.0f);
    for (uint32_t i = 0; i < count; i++) {
        const vk_rail_ionization_candidate_t *candidate = &candidates[i];
        const entity_t *entity = candidate->entity;
        float envelope = vk_rail_ionization_envelope(candidate->phase);
        float far_fade = Q_clipf((1024.0f - candidate->distance) / 256.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf((candidate->distance - 6.0f) / 24.0f,
                                  0.0f, 1.0f);
        float facing_fade = Q_clipf((candidate->facing - 0.02f) / 0.28f,
                                    0.0f, 1.0f);
        vec3_t tangent, bitangent, start;
        MakeNormalVectors(candidate->normal, tangent, bitangent);
        VectorMA(entity->origin, 1.5f, candidate->normal, start);

        for (int branch = 0; branch < 3; branch++) {
            uint32_t hash = vk_dust_hash((int)candidate->seed, branch,
                                         (int)(candidate->seed >> 16));
            float angle = vk_dust_random(hash) * (2.0f * M_PIf);
            vec3_t surface_direction, end;
            VectorScale(tangent, cosf(angle), surface_direction);
            VectorMA(surface_direction, sinf(angle), bitangent,
                     surface_direction);
            float length = 8.0f + vk_dust_random(hash >> 7) * 12.0f;
            VectorMA(start, length, surface_direction, end);
            VectorMA(end, 0.8f + vk_dust_random(hash >> 13) * 2.0f,
                     candidate->normal, end);

            bool spiral_branch = branch & 1;
            float color[4];
            for (int channel = 0; channel < 3; channel++) {
                color[channel] = spiral_branch ?
                    Q_clipf(entity->angles[channel], 0.0f, 1.0f) :
                    entity->rgba.u8[channel] / 255.0f;
                color[channel] = min(color[channel] * 0.85f + 0.15f, 1.0f);
            }
            float source_alpha = spiral_branch ?
                Q_clipf(entity->scale, 0.0f, 1.0f) :
                entity->rgba.u8[3] / 255.0f;
            color[3] = min((0.14f + 0.24f * strength) * envelope * far_fade *
                near_fade * facing_fade * source_alpha, 0.38f);
            vk_append_effect_segment(&vertex_count, start, end, fd->vieworg,
                                     0.45f + strength * 0.75f, color);
        }
    }
    return vertex_count;
}

static uint32_t vk_append_lava_embers(const refdef_t *fd,
                                      const vec3_t viewaxis[3],
                                      uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_lava_embers ||
        (fd->rdflags & (RDF_UNDERWATER | RDF_NOWORLDMODEL)) ||
        !vk.world.cache || !vk.world.cache->nodes ||
        !vk.world.lava_ember_anchor_count)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_lava_embers, 0.0f, 1.0f);
    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 6,
                             (uint32_t)VK_MAX_LAVA_EMBERS);
    if (strength <= 0.0f || !available)
        return vertex_count;

    uint32_t ember_count = 0;
    for (uint32_t i = 0; i < vk.world.lava_ember_anchor_count &&
         ember_count < available; i++) {
        const vk_lava_ember_anchor_t *anchor =
            &vk.world.lava_ember_anchors[i];
        if (!anchor->face || anchor->face->drawframe != vk.world.drawframe)
            continue;

        float density = 0.30f + strength * 0.70f;
        if (vk_lava_ember_random(anchor->hash ^ 0x51ed270bu) > density)
            continue;

        vec3_t delta;
        VectorSubtract(anchor->origin, fd->vieworg, delta);
        float distance = VectorLength(delta);
        if (distance < 16.0f || distance >= 768.0f)
            continue;

        float phase = vk_lava_ember_random(anchor->hash ^ 0xa511e9b3u);
        float speed = 0.32f +
            vk_lava_ember_random(anchor->hash ^ 0x63d83595u) * 0.22f;
        float age = fmodf(fd->time * speed + phase, 1.0f);
        float angle = vk_lava_ember_random(anchor->hash ^ 0x9e3779b9u) *
            (2.0f * M_PIf);
        float curl = sinf(age * M_PIf) *
            (2.0f + 5.0f * vk_lava_ember_random(anchor->hash >> 7));
        vec3_t origin;
        VectorMA(anchor->origin, 5.0f + age * 42.0f,
                 anchor->normal, origin);
        origin[0] += cosf(angle + fd->time * 0.65f) * curl;
        origin[1] += sinf(angle + fd->time * 0.58f) * curl;

        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes, origin);
        if (!leaf || leaf->visframe != vk.world.visframe ||
            (leaf->contents[0] & (CONTENTS_SOLID | MASK_WATER)) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        float life = sinf(age * M_PIf);
        life *= life;
        float far_fade = Q_clipf((768.0f - distance) / 256.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf((distance - 16.0f) / 32.0f,
                                  0.0f, 1.0f);
        float variation = vk_lava_ember_random(anchor->hash ^ 0xc2b2ae35u);
        float color[4] = {
            1.0f,
            0.24f + variation * 0.22f + (1.0f - age) * 0.12f,
            0.025f + variation * 0.04f,
            min((0.22f + strength * 0.32f) * life * far_fade * near_fade,
                0.55f),
        };
        float scale = 3.5f + strength * 3.0f + variation * 1.5f + age;
        vk_append_particle_quad(&vertex_count, origin, VK_RT_SPRITE_HALF(scale),
                                color, viewaxis);
        ember_count++;
    }
    return vertex_count;
}

typedef struct {
    const entity_t *entity;
    float phase;
    float distance;
} vk_shockwave_candidate_t;

static void vk_shockwave_color(const refdef_t *fd, const vec3_t origin,
                               float color[4])
{
    const dlight_t *best = NULL;
    float best_distance = 96.0f;

    for (int i = 0; fd->dlights && i < fd->num_dlights; i++) {
        const dlight_t *light = &fd->dlights[i];
        float distance = Distance(origin, light->origin);
        float peak = max(light->color[0],
                         max(light->color[1], light->color[2]));
        if (distance < best_distance && peak > 0.05f) {
            best = light;
            best_distance = distance;
        }
    }

    if (!best) {
        Vector4Set(color, 1.0f, 0.35f, 0.08f, 1.0f);
        return;
    }

    float peak = max(best->color[0], max(best->color[1], best->color[2]));
    for (int i = 0; i < 3; i++)
        color[i] = Q_clipf(best->color[i] / peak * 0.85f + 0.15f,
                           0.0f, 1.0f);
    color[3] = 1.0f;
}

static uint32_t vk_append_shockwaves(const refdef_t *fd,
                                     const vec3_t viewaxis[3],
                                     uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_shockwaves ||
        !vk.shockwave_texture.descriptor_set || !fd->entities ||
        fd->num_entities <= 0 || (fd->rdflags & RDF_NOWORLDMODEL) ||
        !vk.world.cache || !vk.world.cache->nodes)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_shockwaves, 0.0f, 1.0f);
    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 6,
                             (uint32_t)VK_MAX_SHOCKWAVES);
    if (strength <= 0.0f || !available)
        return vertex_count;

    vk_shockwave_candidate_t candidates[VK_MAX_SHOCKWAVES] = { 0 };
    uint32_t count = 0;

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *entity = &fd->entities[i];
        if (!(entity->flags & RF_EXPLOSION))
            continue;

        float frame = entity->oldframe % 15u +
            (1.0f - Q_clipf(entity->backlerp, 0.0f, 1.0f));
        float phase = frame * 0.25f;
        if (phase < 0.0f || phase >= 1.0f)
            continue;

        float distance = Distance(entity->origin, fd->vieworg);
        if (distance < 8.0f || distance >= 1024.0f)
            continue;

        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                            entity->origin);
        if (!leaf || leaf->visframe != vk.world.visframe ||
            (leaf->contents[0] & CONTENTS_SOLID) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        uint32_t slot = min(count, available - 1);
        if (count >= available && distance >= candidates[slot].distance)
            continue;
        while (slot > 0 && distance < candidates[slot - 1].distance) {
            candidates[slot] = candidates[slot - 1];
            slot--;
        }
        candidates[slot].entity = entity;
        candidates[slot].phase = phase;
        candidates[slot].distance = distance;
        if (count < available)
            count++;
    }

    for (uint32_t i = 0; i < count; i++) {
        const vk_shockwave_candidate_t *candidate = &candidates[i];
        float birth = Q_clipf(candidate->phase / 0.12f, 0.0f, 1.0f);
        birth = birth * birth * (3.0f - 2.0f * birth);
        float fade = 1.0f - candidate->phase;
        fade *= fade;
        float far_fade = Q_clipf((1024.0f - candidate->distance) / 256.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf((candidate->distance - 8.0f) / 24.0f,
                                  0.0f, 1.0f);
        float color[4];
        vk_shockwave_color(fd, candidate->entity->origin, color);
        color[3] = min((0.28f + strength * 0.30f) * birth * fade *
                       far_fade * near_fade, 0.58f);
        float radius = (16.0f + candidate->phase * 96.0f) *
            (0.90f + strength * 0.15f);
        vk_append_effect_quad(&vertex_count, candidate->entity->origin,
                              radius, color, viewaxis);
    }
    return vertex_count;
}

typedef struct {
    const entity_t *entity;
    vec3_t normal;
    float phase;
    float distance;
} vk_splash_ripple_candidate_t;

static int vk_splash_ripple_contents(int splash)
{
    switch (splash) {
    case SPLASH_BLUE_WATER:
    case SPLASH_BROWN_WATER:
        return CONTENTS_WATER;
    case SPLASH_SLIME:
        return CONTENTS_SLIME;
    case SPLASH_LAVA:
        return CONTENTS_LAVA;
    default:
        return 0;
    }
}

static uint32_t vk_append_splash_ripples(const refdef_t *fd,
                                         uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_splash_ripples ||
        !vk.shockwave_texture.descriptor_set || !fd->entities ||
        fd->num_entities <= 0 || (fd->rdflags & RDF_NOWORLDMODEL) ||
        !vk.world.cache || !vk.world.cache->nodes)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_splash_ripples, 0.0f, 1.0f);
    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 12,
                             (uint32_t)VK_MAX_SPLASH_RIPPLES);
    if (strength <= 0.0f || !available)
        return vertex_count;

    vk_splash_ripple_candidate_t candidates[VK_MAX_SPLASH_RIPPLES] = { 0 };
    uint32_t count = 0;

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *entity = &fd->entities[i];
        if ((entity->flags & (RF_EFFECT_ONLY | RF_RT_SPLASH_RIPPLE)) !=
            (RF_EFFECT_ONLY | RF_RT_SPLASH_RIPPLE))
            continue;

        float phase = (entity->oldframe +
            1.0f - Q_clipf(entity->backlerp, 0.0f, 1.0f)) / 9.0f;
        if (phase < 0.0f || phase >= 1.0f)
            continue;

        float distance = Distance(entity->origin, fd->vieworg);
        if (distance < 8.0f || distance >= 1024.0f)
            continue;

        int expected_contents = vk_splash_ripple_contents(entity->skinnum);
        if (!expected_contents)
            continue;

        vec3_t normal, to_view, plus_point, minus_point, draw_normal;
        VectorCopy(entity->oldorigin, normal);
        if (VectorNormalize(normal) <= 0.0f)
            continue;
        VectorSubtract(fd->vieworg, entity->origin, to_view);
        if (VectorNormalize(to_view) <= 0.0f)
            continue;

        VectorMA(entity->origin, 4.0f, normal, plus_point);
        VectorMA(entity->origin, -4.0f, normal, minus_point);
        const mleaf_t *plus_leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                                 plus_point);
        const mleaf_t *minus_leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                                  minus_point);
        if ((!plus_leaf || !(plus_leaf->contents[0] & expected_contents)) &&
            (!minus_leaf || !(minus_leaf->contents[0] & expected_contents)))
            continue;

        if (DotProduct(normal, to_view) < 0.0f)
            VectorNegate(normal, draw_normal);
        else
            VectorCopy(normal, draw_normal);
        VectorMA(entity->origin, 4.0f, draw_normal, plus_point);
        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes, plus_point);
        if (!leaf || leaf->visframe != vk.world.visframe ||
            (leaf->contents[0] & CONTENTS_SOLID) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        uint32_t slot = min(count, available - 1);
        if (count >= available && distance >= candidates[slot].distance)
            continue;
        while (slot > 0 && distance < candidates[slot - 1].distance) {
            candidates[slot] = candidates[slot - 1];
            slot--;
        }
        candidates[slot].entity = entity;
        VectorCopy(draw_normal, candidates[slot].normal);
        candidates[slot].phase = phase;
        candidates[slot].distance = distance;
        if (count < available)
            count++;
    }

    for (uint32_t i = 0; i < count; i++) {
        const vk_splash_ripple_candidate_t *candidate = &candidates[i];
        const entity_t *entity = candidate->entity;
        float far_fade = Q_clipf((1024.0f - candidate->distance) / 256.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf((candidate->distance - 8.0f) / 24.0f,
                                  0.0f, 1.0f);
        float event_scale = Q_clipf(entity->scale, 0.75f, 1.25f);

        for (int ring = 0; ring < 2; ring++) {
            float phase = candidate->phase - ring * 0.20f;
            if (phase < 0.0f)
                continue;
            phase /= 1.0f - ring * 0.20f;
            float birth = Q_clipf(phase / 0.10f, 0.0f, 1.0f);
            birth = birth * birth * (3.0f - 2.0f * birth);
            float fade = 1.0f - phase;
            fade *= sqrtf(max(fade, 0.0f));
            float color[4] = {
                entity->rgba.u8[0] / 255.0f,
                entity->rgba.u8[1] / 255.0f,
                entity->rgba.u8[2] / 255.0f,
                min((0.13f + 0.23f * strength) *
                    (ring ? 0.72f : 1.0f) * birth * fade *
                    far_fade * near_fade, 0.36f),
            };
            float radius = (8.0f + phase * 72.0f) * event_scale *
                (0.92f + strength * 0.12f);
            vk_append_oriented_effect_quad(&vertex_count, entity->origin,
                                           candidate->normal, radius, color);
        }
    }
    return vertex_count;
}

typedef struct {
    const entity_t *entity;
    vec3_t normal;
    vec3_t direction;
    float distance;
} vk_water_wake_candidate_t;

static uint32_t vk_append_water_wakes(const refdef_t *fd,
                                      uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_water_wakes ||
        !vk.shockwave_texture.descriptor_set || !fd->entities ||
        fd->num_entities <= 0 || (fd->rdflags & RDF_NOWORLDMODEL) ||
        !vk.world.cache || !vk.world.cache->nodes)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_water_wakes, 0.0f, 1.0f);
    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 36,
                             (uint32_t)VK_MAX_WATER_WAKES);
    if (strength <= 0.0f || !available)
        return vertex_count;

    vk_water_wake_candidate_t candidates[VK_MAX_WATER_WAKES] = { 0 };
    uint32_t count = 0;

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *entity = &fd->entities[i];
        if ((entity->flags & (RF_EFFECT_ONLY | RF_RT_WATER_WAKE)) !=
            (RF_EFFECT_ONLY | RF_RT_WATER_WAKE))
            continue;

        float distance = Distance(entity->origin, fd->vieworg);
        if (distance < 1.0f || distance >= 1024.0f)
            continue;

        vec3_t normal, direction, to_view, plus_point, minus_point;
        VectorCopy(entity->angles, normal);
        VectorCopy(entity->oldorigin, direction);
        if (VectorNormalize(normal) <= 0.1f)
            continue;
        VectorMA(direction, -DotProduct(direction, normal), normal,
                 direction);
        if (VectorNormalize(direction) <= 0.1f)
            continue;

        VectorMA(entity->origin, 4.0f, normal, plus_point);
        VectorMA(entity->origin, -4.0f, normal, minus_point);
        const mleaf_t *plus_leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                                 plus_point);
        const mleaf_t *minus_leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                                  minus_point);
        bool plus_water = plus_leaf &&
            (plus_leaf->contents[0] & CONTENTS_WATER);
        bool minus_water = minus_leaf &&
            (minus_leaf->contents[0] & CONTENTS_WATER);
        if (plus_water == minus_water)
            continue;

        VectorSubtract(fd->vieworg, entity->origin, to_view);
        vec3_t draw_normal;
        if (DotProduct(normal, to_view) < 0.0f)
            VectorNegate(normal, draw_normal);
        else
            VectorCopy(normal, draw_normal);
        VectorMA(entity->origin, 4.0f, draw_normal, plus_point);
        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes, plus_point);
        if (!leaf || leaf->visframe != vk.world.visframe ||
            (leaf->contents[0] & CONTENTS_SOLID) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        uint32_t slot = min(count, available - 1);
        if (count >= available && distance >= candidates[slot].distance)
            continue;
        while (slot > 0 && distance < candidates[slot - 1].distance) {
            candidates[slot] = candidates[slot - 1];
            slot--;
        }
        candidates[slot].entity = entity;
        VectorCopy(draw_normal, candidates[slot].normal);
        VectorCopy(direction, candidates[slot].direction);
        candidates[slot].distance = distance;
        if (count < available)
            count++;
    }

    for (uint32_t i = 0; i < count; i++) {
        const vk_water_wake_candidate_t *candidate = &candidates[i];
        const entity_t *entity = candidate->entity;
        vec3_t side;
        CrossProduct(candidate->normal, candidate->direction, side);
        if (VectorNormalize(side) <= 0.1f)
            continue;

        float far_fade = Q_clipf((1024.0f - candidate->distance) / 256.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf((candidate->distance - 1.0f) / 7.0f,
                                  0.0f, 1.0f);
        float event_scale = Q_clipf(entity->scale, 0.8f, 1.25f);
        float seed = (entity->frame & 31u) * (1.0f / 31.0f);
        float base_phase = fmodf(fd->time * 1.8f + seed, 1.0f);

        for (int crest = 0; crest < 3; crest++) {
            float phase = fmodf(base_phase + crest * (1.0f / 3.0f), 1.0f);
            float birth = Q_clipf(phase / 0.08f, 0.0f, 1.0f);
            float fade = Q_clipf((1.0f - phase) / 0.22f, 0.0f, 1.0f);
            float life = birth * fade;
            float back = (5.0f + phase * 32.0f) * event_scale;
            float spread = (3.0f + phase * 17.0f) * event_scale;
            float major = (7.0f + phase * 15.0f) * event_scale;
            float minor = (2.2f + phase * 3.0f) * event_scale;

            for (int wing = -1; wing <= 1; wing += 2) {
                vec3_t center, axis;
                VectorMA(entity->origin, -back, candidate->direction,
                         center);
                VectorMA(center, wing * spread, side, center);
                VectorScale(candidate->direction, -0.82f, axis);
                VectorMA(axis, wing * 0.57f, side, axis);
                float color[4] = {
                    entity->rgba.u8[0] / 255.0f,
                    entity->rgba.u8[1] / 255.0f,
                    entity->rgba.u8[2] / 255.0f,
                    min((0.18f + strength * 0.28f) * life *
                        far_fade * near_fade, 0.46f),
                };
                vk_append_oriented_effect_ellipse(&vertex_count, center,
                                                  candidate->normal, axis,
                                                  major, minor, color);
            }
        }
    }
    return vertex_count;
}

typedef struct {
    const entity_t *entity;
    float phase;
    float distance;
    float seed;
} vk_teleport_vortex_candidate_t;

static uint32_t vk_append_teleport_vortices(const refdef_t *fd,
                                            const vec3_t viewaxis[3],
                                            uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_teleport_vortex ||
        !vk.shockwave_texture.descriptor_set || !fd->entities ||
        fd->num_entities <= 0 || (fd->rdflags & RDF_NOWORLDMODEL) ||
        !vk.world.cache || !vk.world.cache->nodes)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_teleport_vortex, 0.0f, 1.0f);
    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 60,
                             (uint32_t)VK_MAX_TELEPORT_VORTICES);
    if (strength <= 0.0f || !available)
        return vertex_count;

    vk_teleport_vortex_candidate_t candidates[VK_MAX_TELEPORT_VORTICES] = { 0 };
    uint32_t count = 0;

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *entity = &fd->entities[i];
        if ((entity->flags & (RF_EFFECT_ONLY | RF_RT_TELEPORT)) !=
            (RF_EFFECT_ONLY | RF_RT_TELEPORT))
            continue;
        if (entity->skinnum < RT_TELEPORT_STANDARD ||
            entity->skinnum > RT_TELEPORT_BOSS)
            continue;

        float duration = max(entity->angles[0], 1.0f);
        float phase = (entity->oldframe +
            1.0f - Q_clipf(entity->backlerp, 0.0f, 1.0f)) / duration;
        if (phase < 0.0f || phase >= 1.0f)
            continue;

        float distance = Distance(entity->origin, fd->vieworg);
        if (distance >= 1536.0f)
            continue;

        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                            entity->origin);
        if (!leaf || leaf->visframe != vk.world.visframe ||
            (leaf->contents[0] & CONTENTS_SOLID) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        uint32_t slot = min(count, available - 1);
        if (count >= available && distance >= candidates[slot].distance)
            continue;
        while (slot > 0 && distance < candidates[slot - 1].distance) {
            candidates[slot] = candidates[slot - 1];
            slot--;
        }
        candidates[slot].entity = entity;
        candidates[slot].phase = phase;
        candidates[slot].distance = distance;
        candidates[slot].seed = vk_dust_random(vk_dust_hash(
            (int)floorf(entity->origin[0] * 0.0625f),
            (int)floorf(entity->origin[1] * 0.0625f),
            (int)floorf(entity->origin[2] * 0.0625f)));
        if (count < available)
            count++;
    }

    for (uint32_t i = 0; i < count; i++) {
        const vk_teleport_vortex_candidate_t *candidate = &candidates[i];
        const entity_t *entity = candidate->entity;
        float birth = Q_clipf(candidate->phase / 0.12f, 0.0f, 1.0f);
        birth = birth * birth * (3.0f - 2.0f * birth);
        float fade = Q_clipf((1.0f - candidate->phase) / 0.30f,
                             0.0f, 1.0f);
        fade = fade * fade * (3.0f - 2.0f * fade);
        float far_fade = Q_clipf((1536.0f - candidate->distance) / 384.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf(candidate->distance / 48.0f, 0.0f, 1.0f);
        float envelope = birth * fade * far_fade * near_fade;
        float event_scale = Q_clipf(entity->scale, 0.9f, 2.0f);
        float base_color[3] = {
            entity->rgba.u8[0] / 255.0f,
            entity->rgba.u8[1] / 255.0f,
            entity->rgba.u8[2] / 255.0f,
        };
        float accent_color[3];
        if (entity->skinnum == RT_TELEPORT_BOSS) {
            VectorSet(accent_color, 0.72f, 0.28f, 1.0f);
        } else {
            for (int channel = 0; channel < 3; channel++)
                accent_color[channel] = min(base_color[channel] * 0.72f +
                                            0.28f, 1.0f);
        }
        float seed_angle = candidate->seed * (2.0f * M_PIf);

        for (int ring = 0; ring < 2; ring++) {
            float direction = ring ? -1.0f : 1.0f;
            float angle = seed_angle + direction * fd->time *
                (1.8f + ring * 0.65f);
            float tilt = 0.18f + 0.06f * sinf(angle * 1.7f);
            vec3_t normal = {
                cosf(angle) * tilt,
                sinf(angle) * tilt,
                1.0f,
            };
            VectorNormalize(normal);
            vec3_t origin;
            VectorCopy(entity->origin, origin);
            origin[2] += event_scale * (8.0f + ring * 18.0f +
                sinf(angle * 1.3f) * 3.0f);
            float radius = event_scale *
                (18.0f + candidate->phase * 32.0f + ring * 7.0f);
            const float *ring_color = ring ? accent_color : base_color;
            float color[4] = {
                ring_color[0], ring_color[1], ring_color[2],
                min((0.18f + strength * 0.28f) *
                    (ring ? 0.78f : 1.0f) * envelope, 0.46f),
            };
            vk_append_oriented_effect_quad(&vertex_count, origin, normal,
                                           radius, color);
        }

        for (int level = 0; level < 4; level++) {
            float travel = fmodf(candidate->phase * 1.65f +
                                 level * 0.25f + candidate->seed, 1.0f);
            float level_fade = sinf(travel * M_PIf);
            vec3_t origin;
            VectorCopy(entity->origin, origin);
            origin[2] += event_scale * (-6.0f + travel * 62.0f);
            float pulse = 0.5f + 0.5f * sinf(
                fd->time * 4.0f + level * 1.7f + seed_angle);
            float radius = event_scale * (5.0f + pulse * 4.0f);
            const float *level_color = (level & 1) ?
                accent_color : base_color;
            float color[4] = {
                level_color[0], level_color[1], level_color[2],
                min((0.10f + strength * 0.18f) * envelope * level_fade,
                    0.28f),
            };
            static const vec3_t up = { 0.0f, 0.0f, 1.0f };
            vk_append_oriented_effect_quad(&vertex_count, origin, up,
                                           radius, color);
        }

        for (int mote = 0; mote < 4; mote++) {
            float angle = seed_angle + fd->time *
                (2.4f + mote * 0.13f) + mote * (0.5f * M_PIf);
            float orbit = event_scale *
                (19.0f + 4.0f * sinf(angle * 1.9f + mote));
            float travel = fmodf(candidate->phase * 1.35f +
                                 mote * 0.25f + candidate->seed, 1.0f);
            vec3_t origin = {
                entity->origin[0] + cosf(angle) * orbit,
                entity->origin[1] + sinf(angle) * orbit,
                entity->origin[2] + event_scale * (-3.0f + travel * 58.0f),
            };
            float mote_fade = sinf(travel * M_PIf);
            float color[4] = {
                accent_color[0], accent_color[1], accent_color[2],
                min((0.12f + strength * 0.22f) * envelope * mote_fade,
                    0.34f),
            };
            float radius = event_scale * (2.2f + strength * 1.2f);
            vk_append_effect_quad(&vertex_count, origin, radius, color,
                                  viewaxis);
        }
    }
    return vertex_count;
}

typedef struct {
    const entity_t *entity;
    float phase;
    float distance;
    float seed;
    int type;
} vk_energy_collapse_candidate_t;

static uint32_t vk_append_energy_collapses(const refdef_t *fd,
                                            const vec3_t viewaxis[3],
                                            uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_energy_collapse ||
        !vk.shockwave_texture.descriptor_set || !fd->entities ||
        fd->num_entities <= 0 || (fd->rdflags & RDF_NOWORLDMODEL) ||
        !vk.world.cache || !vk.world.cache->nodes)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_energy_collapse, 0.0f, 1.0f);
    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 48,
                             (uint32_t)VK_MAX_ENERGY_COLLAPSES);
    if (strength <= 0.0f || !available)
        return vertex_count;

    vk_energy_collapse_candidate_t
        candidates[VK_MAX_ENERGY_COLLAPSES] = { 0 };
    uint32_t count = 0;

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *entity = &fd->entities[i];
        if ((entity->flags & (RF_EFFECT_ONLY | RF_RT_ENERGY_COLLAPSE)) !=
            (RF_EFFECT_ONLY | RF_RT_ENERGY_COLLAPSE))
            continue;
        if (entity->skinnum < RT_ENERGY_BFG_CORE ||
            entity->skinnum > RT_ENERGY_TRACKER)
            continue;

        float duration = max(entity->angles[0], 1.0f);
        float phase = (entity->oldframe +
            1.0f - Q_clipf(entity->backlerp, 0.0f, 1.0f)) / duration;
        if (phase < 0.0f || phase >= 1.0f)
            continue;

        float distance = Distance(entity->origin, fd->vieworg);
        if (distance >= 1536.0f)
            continue;

        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                            entity->origin);
        if (!leaf || leaf->visframe != vk.world.visframe ||
            (leaf->contents[0] & CONTENTS_SOLID) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        uint32_t slot = min(count, available - 1);
        if (count >= available && distance >= candidates[slot].distance)
            continue;
        while (slot > 0 && distance < candidates[slot - 1].distance) {
            candidates[slot] = candidates[slot - 1];
            slot--;
        }
        candidates[slot].entity = entity;
        candidates[slot].phase = phase;
        candidates[slot].distance = distance;
        candidates[slot].type = entity->skinnum;
        candidates[slot].seed = vk_dust_random(vk_dust_hash(
            (int)floorf(entity->origin[0] * 0.0625f) ^
                (int)entity->angles[1],
            (int)floorf(entity->origin[1] * 0.0625f),
            (int)floorf(entity->origin[2] * 0.0625f)));
        if (count < available)
            count++;
    }

    for (uint32_t i = 0; i < count; i++) {
        const vk_energy_collapse_candidate_t *candidate = &candidates[i];
        const entity_t *entity = candidate->entity;
        float birth = Q_clipf(candidate->phase / 0.08f, 0.0f, 1.0f);
        birth = birth * birth * (3.0f - 2.0f * birth);
        float fade = Q_clipf((1.0f - candidate->phase) / 0.22f,
                             0.0f, 1.0f);
        fade = fade * fade * (3.0f - 2.0f * fade);
        float far_fade = Q_clipf((1536.0f - candidate->distance) / 384.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf(candidate->distance / 40.0f,
                                  0.0f, 1.0f);
        float envelope = birth * fade * far_fade * near_fade;
        float event_scale = Q_clipf(entity->scale, 0.7f, 1.6f);
        float smooth = candidate->phase * candidate->phase *
            (3.0f - 2.0f * candidate->phase);
        float base_color[3] = {
            entity->rgba.u8[0] / 255.0f,
            entity->rgba.u8[1] / 255.0f,
            entity->rgba.u8[2] / 255.0f,
        };
        float accent_color[3];
        for (int channel = 0; channel < 3; channel++)
            accent_color[channel] = min(base_color[channel] * 0.62f +
                                        0.38f, 1.0f);

        vec3_t rotation = {
            candidate->seed * 180.0f + candidate->phase * 210.0f,
            candidate->seed * 270.0f - candidate->phase * 170.0f,
            candidate->phase * 120.0f,
        };
        vec3_t axes[3];
        AnglesToAxis(rotation, axes);
        float radius = event_scale * (8.0f + (1.0f - smooth) * 50.0f);
        for (int ring = 0; ring < 3; ring++) {
            const float *ring_color = ring == 1 ?
                accent_color : base_color;
            float color[4] = {
                ring_color[0], ring_color[1], ring_color[2],
                min((0.18f + strength * 0.28f) * envelope *
                    (1.0f - ring * 0.10f), 0.46f),
            };
            vk_append_oriented_effect_quad(&vertex_count, entity->origin,
                                           axes[ring],
                                           radius * (0.88f + ring * 0.12f),
                                           color);
        }

        float seed_angle = candidate->seed * (2.0f * M_PIf);
        float orbit = event_scale * (5.0f + (1.0f - smooth) * 58.0f);
        for (int node = 0; node < 4; node++) {
            float angle = seed_angle + node * (0.5f * M_PIf) +
                candidate->phase * (5.8f + node * 0.22f);
            vec3_t origin;
            VectorCopy(entity->origin, origin);
            VectorMA(origin, cosf(angle) * orbit, axes[1], origin);
            VectorMA(origin, sinf(angle) * orbit, axes[2], origin);
            VectorMA(origin, sinf(angle * 1.7f) * orbit * 0.18f,
                     axes[0], origin);
            float color[4] = {
                accent_color[0], accent_color[1], accent_color[2],
                min((0.15f + strength * 0.25f) * envelope, 0.40f),
            };
            float node_radius = event_scale * (3.0f + strength * 1.8f);
            vk_append_effect_quad(&vertex_count, origin, node_radius,
                                  color, viewaxis);
        }

        float final_phase = Q_clipf((candidate->phase - 0.62f) / 0.38f,
                                    0.0f, 1.0f);
        float final_energy = sinf(final_phase * M_PIf);
        float core_color[4] = {
            accent_color[0], accent_color[1], accent_color[2],
            min((0.22f + strength * 0.34f) * final_energy * envelope,
                0.56f),
        };
        float core_radius = event_scale * (6.0f + final_phase * 12.0f);
        vk_append_effect_quad(&vertex_count, entity->origin, core_radius,
                              core_color, viewaxis);
    }
    return vertex_count;
}

typedef struct {
    const entity_t *entity;
    float phase;
    float distance;
} vk_item_respawn_candidate_t;

static uint32_t vk_append_item_materializations(const refdef_t *fd,
                                                uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_item_respawn ||
        !vk.shockwave_texture.descriptor_set || !fd->entities ||
        fd->num_entities <= 0 || (fd->rdflags & RDF_NOWORLDMODEL) ||
        !vk.world.cache || !vk.world.cache->nodes)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_item_respawn, 0.0f, 1.0f);
    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 18,
                             (uint32_t)VK_MAX_ITEM_RESPAWNS);
    if (strength <= 0.0f || !available)
        return vertex_count;

    vk_item_respawn_candidate_t candidates[VK_MAX_ITEM_RESPAWNS] = { 0 };
    uint32_t count = 0;

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *entity = &fd->entities[i];
        if ((entity->flags & (RF_EFFECT_ONLY | RF_RT_ITEM_RESPAWN)) !=
            (RF_EFFECT_ONLY | RF_RT_ITEM_RESPAWN))
            continue;

        float duration = max(entity->angles[0], 1.0f);
        float phase = (entity->oldframe +
            1.0f - Q_clipf(entity->backlerp, 0.0f, 1.0f)) / duration;
        if (phase < 0.0f || phase >= 1.0f)
            continue;

        float distance = Distance(entity->origin, fd->vieworg);
        if (distance < 6.0f || distance >= 1024.0f)
            continue;

        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                            entity->origin);
        if (!leaf || leaf->visframe != vk.world.visframe ||
            (leaf->contents[0] & CONTENTS_SOLID) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        uint32_t slot = min(count, available - 1);
        if (count >= available && distance >= candidates[slot].distance)
            continue;
        while (slot > 0 && distance < candidates[slot - 1].distance) {
            candidates[slot] = candidates[slot - 1];
            slot--;
        }
        candidates[slot].entity = entity;
        candidates[slot].phase = phase;
        candidates[slot].distance = distance;
        if (count < available)
            count++;
    }

    static const vec3_t up = { 0.0f, 0.0f, 1.0f };
    for (uint32_t i = 0; i < count; i++) {
        const vk_item_respawn_candidate_t *candidate = &candidates[i];
        const entity_t *entity = candidate->entity;
        float phase = candidate->phase;
        float far_fade = Q_clipf((1024.0f - candidate->distance) / 256.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf((candidate->distance - 6.0f) / 30.0f,
                                  0.0f, 1.0f);
        float visibility = far_fade * near_fade;
        float base_color[3] = {
            entity->rgba.u8[0] / 255.0f,
            entity->rgba.u8[1] / 255.0f,
            entity->rgba.u8[2] / 255.0f,
        };
        float accent_color[3];
        for (int channel = 0; channel < 3; channel++)
            accent_color[channel] = min(base_color[channel] * 0.62f +
                                        0.38f, 1.0f);
        float event_scale = Q_clipf(entity->scale, 0.75f, 1.25f);

        if (phase < 0.84f) {
            float local = phase / 0.84f;
            float smooth = local * local * (3.0f - 2.0f * local);
            float energy = sinf(local * M_PIf);
            vec3_t origin;
            VectorCopy(entity->origin, origin);
            origin[2] += event_scale * (-12.0f + smooth * 3.0f);
            float color[4] = {
                base_color[0], base_color[1], base_color[2],
                min((0.18f + strength * 0.30f) * energy * visibility,
                    0.48f),
            };
            float radius = event_scale * (10.0f + smooth * 38.0f);
            vk_append_oriented_effect_quad(&vertex_count, origin, up,
                                           radius, color);
        }

        float scan_smooth = phase * phase * (3.0f - 2.0f * phase);
        float scan_energy = sinf(phase * M_PIf);
        vec3_t scan_origin;
        VectorCopy(entity->origin, scan_origin);
        scan_origin[2] += event_scale * (-12.0f + scan_smooth * 52.0f);
        float scan_color[4] = {
            accent_color[0], accent_color[1], accent_color[2],
            min((0.22f + strength * 0.32f) * scan_energy * visibility,
                0.54f),
        };
        float scan_radius = event_scale *
            (22.0f + sinf(phase * 2.0f * M_PIf) * 3.0f);
        vk_append_oriented_effect_quad(&vertex_count, scan_origin, up,
                                       scan_radius, scan_color);

        if (phase >= 0.28f) {
            float local = (phase - 0.28f) / 0.72f;
            float smooth = local * local * (3.0f - 2.0f * local);
            float energy = sinf(local * M_PIf);
            vec3_t origin;
            VectorCopy(entity->origin, origin);
            origin[2] += event_scale * (18.0f + smooth * 16.0f);
            float color[4] = {
                accent_color[0], accent_color[1], accent_color[2],
                min((0.16f + strength * 0.26f) * energy * visibility,
                    0.42f),
            };
            float radius = event_scale * (28.0f - smooth * 12.0f);
            vk_append_oriented_effect_quad(&vertex_count, origin, up,
                                           radius, color);
        }
    }
    return vertex_count;
}

typedef struct {
    const entity_t *entity;
    vec3_t normal;
    float phase;
    float distance;
    uint32_t seed;
    int severity;
} vk_landing_dust_candidate_t;

static uint32_t vk_append_landing_dust(const refdef_t *fd,
                                       const vec3_t viewaxis[3],
                                       uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_landing_dust ||
        !vk.particle_texture.descriptor_set || !fd->entities ||
        fd->num_entities <= 0 || (fd->rdflags & RDF_NOWORLDMODEL) ||
        !vk.world.cache || !vk.world.cache->nodes)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_landing_dust, 0.0f, 1.0f);
    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 48,
                             (uint32_t)VK_MAX_LANDING_DUST);
    if (strength <= 0.0f || !available)
        return vertex_count;

    const mleaf_t *view_leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                             fd->vieworg);
    if (!view_leaf || (view_leaf->contents[0] & MASK_WATER))
        return vertex_count;

    vk_landing_dust_candidate_t
        candidates[VK_MAX_LANDING_DUST] = { 0 };
    uint32_t count = 0;

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *entity = &fd->entities[i];
        if ((entity->flags & (RF_EFFECT_ONLY | RF_RT_LANDING_DUST)) !=
            (RF_EFFECT_ONLY | RF_RT_LANDING_DUST))
            continue;

        int severity = entity->skinnum & RT_LANDING_SEVERITY_MASK;
        if (severity < RT_LANDING_SHORT || severity > RT_LANDING_FAR)
            continue;

        float duration = max(entity->angles[0], 1.0f);
        float phase = (entity->oldframe +
            1.0f - Q_clipf(entity->backlerp, 0.0f, 1.0f)) / duration;
        if (phase < 0.0f || phase >= 1.0f)
            continue;

        float distance = Distance(entity->origin, fd->vieworg);
        if (distance < 8.0f || distance >= 768.0f)
            continue;

        vec3_t normal, leaf_point;
        VectorCopy(entity->oldorigin, normal);
        if (VectorNormalize(normal) <= 0.0f || normal[2] < 0.45f)
            continue;
        VectorMA(entity->origin, 2.0f, normal, leaf_point);
        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                            leaf_point);
        if (!leaf || leaf->visframe != vk.world.visframe ||
            (leaf->contents[0] & (CONTENTS_SOLID | MASK_WATER)) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        uint32_t slot = min(count, available - 1);
        if (count >= available && distance >= candidates[slot].distance)
            continue;
        while (slot > 0 && distance < candidates[slot - 1].distance) {
            candidates[slot] = candidates[slot - 1];
            slot--;
        }
        candidates[slot].entity = entity;
        VectorCopy(normal, candidates[slot].normal);
        candidates[slot].phase = phase;
        candidates[slot].distance = distance;
        candidates[slot].severity = severity;
        candidates[slot].seed = vk_dust_hash(
            (int)floorf(entity->origin[0] * 0.125f) ^
                (int)entity->angles[1],
            (int)floorf(entity->origin[1] * 0.125f),
            (int)floorf(entity->origin[2] * 0.125f));
        if (count < available)
            count++;
    }

    static const int mote_counts[] = { 4, 6, 8 };
    for (uint32_t i = 0; i < count; i++) {
        const vk_landing_dust_candidate_t *candidate = &candidates[i];
        const entity_t *entity = candidate->entity;
        float birth = Q_clipf(candidate->phase / 0.10f, 0.0f, 1.0f);
        birth = birth * birth * (3.0f - 2.0f * birth);
        float fade = 1.0f - candidate->phase;
        fade *= sqrtf(max(fade, 0.0f));
        float far_fade = Q_clipf((768.0f - candidate->distance) / 192.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf((candidate->distance - 8.0f) / 24.0f,
                                  0.0f, 1.0f);
        float local_fade = (entity->skinnum & RT_LANDING_LOCAL) ? 0.68f : 1.0f;
        float envelope = birth * fade * far_fade * near_fade * local_fade;
        float event_scale = Q_clipf(entity->scale, 0.65f, 1.35f);
        float travel_phase = candidate->phase * (2.0f - candidate->phase);
        vec3_t tangent, bitangent;
        MakeNormalVectors(candidate->normal, tangent, bitangent);

        for (int mote = 0; mote < mote_counts[candidate->severity]; mote++) {
            uint32_t hash = vk_dust_hash((int)candidate->seed, mote,
                                         candidate->severity);
            float random = vk_dust_random(hash >> 7);
            float angle = vk_dust_random(hash) * (2.0f * M_PIf) +
                candidate->phase * (random - 0.5f) * 0.32f;
            vec3_t direction, origin;
            VectorScale(tangent, cosf(angle), direction);
            VectorMA(direction, sinf(angle), bitangent, direction);
            float travel = event_scale * (13.0f + random * 15.0f) *
                travel_phase;
            VectorMA(entity->origin, travel, direction, origin);
            float arc = event_scale *
                (2.0f + sinf(candidate->phase * M_PIf) *
                 (5.0f + vk_dust_random(hash >> 15) * 7.0f));
            VectorMA(origin, arc, candidate->normal, origin);

            float tint = 0.82f + vk_dust_random(hash >> 20) * 0.18f;
            float color[4] = {
                entity->rgba.u8[0] / 255.0f * tint,
                entity->rgba.u8[1] / 255.0f * tint,
                entity->rgba.u8[2] / 255.0f * tint,
                min((0.14f + strength * 0.27f) * envelope *
                    (0.85f + random * 0.15f), 0.40f),
            };
            float scale = event_scale *
                (2.0f + vk_dust_random(hash >> 11) * 1.35f) *
                (0.72f + candidate->phase * 0.72f);
            if (entity->skinnum & RT_LANDING_LOCAL)
                scale *= 0.88f;
            vk_append_particle_quad(&vertex_count, origin,
                                    VK_RT_SPRITE_HALF(scale), color, viewaxis);
        }
    }
    return vertex_count;
}

typedef struct {
    const entity_t *entity;
    vec3_t normal;
    float phase;
    float distance;
    float facing;
    uint32_t seed;
} vk_electric_filament_candidate_t;

static uint32_t vk_append_electric_filaments(const refdef_t *fd,
                                             uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_electric_filaments ||
        !vk.beam_texture.descriptor_set || !fd->entities ||
        fd->num_entities <= 0 || (fd->rdflags & RDF_NOWORLDMODEL) ||
        !vk.world.cache || !vk.world.cache->nodes)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_electric_filaments, 0.0f, 1.0f);
    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 36,
                             (uint32_t)VK_MAX_ELECTRIC_FILAMENTS);
    if (strength <= 0.0f || !available)
        return vertex_count;

    vk_electric_filament_candidate_t
        candidates[VK_MAX_ELECTRIC_FILAMENTS] = { 0 };
    uint32_t count = 0;

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *entity = &fd->entities[i];
        if ((entity->flags & (RF_EFFECT_ONLY | RF_RT_ELECTRIC)) !=
            (RF_EFFECT_ONLY | RF_RT_ELECTRIC))
            continue;

        float duration = max(entity->angles[0], 1.0f);
        float phase = (entity->oldframe +
            1.0f - Q_clipf(entity->backlerp, 0.0f, 1.0f)) / duration;
        if (phase < 0.0f || phase >= 1.0f)
            continue;

        float distance = Distance(entity->origin, fd->vieworg);
        if (distance < 8.0f || distance >= 768.0f)
            continue;

        vec3_t normal, to_view, leaf_point;
        VectorCopy(entity->oldorigin, normal);
        if (VectorNormalize(normal) <= 0.0f)
            continue;
        VectorSubtract(fd->vieworg, entity->origin, to_view);
        if (VectorNormalize(to_view) <= 0.0f)
            continue;
        float facing = DotProduct(normal, to_view);
        if (facing <= 0.02f)
            continue;

        VectorMA(entity->origin, 2.0f, normal, leaf_point);
        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                            leaf_point);
        if (!leaf || leaf->visframe != vk.world.visframe ||
            (leaf->contents[0] & CONTENTS_SOLID) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        uint32_t slot = min(count, available - 1);
        if (count >= available && distance >= candidates[slot].distance)
            continue;
        while (slot > 0 && distance < candidates[slot - 1].distance) {
            candidates[slot] = candidates[slot - 1];
            slot--;
        }
        candidates[slot].entity = entity;
        VectorCopy(normal, candidates[slot].normal);
        candidates[slot].phase = phase;
        candidates[slot].distance = distance;
        candidates[slot].facing = facing;
        candidates[slot].seed = vk_dust_hash(
            (int)floorf(entity->origin[0] * 0.125f) ^
                (int)entity->angles[1],
            (int)floorf(entity->origin[1] * 0.125f),
            (int)floorf(entity->origin[2] * 0.125f));
        if (count < available)
            count++;
    }

    for (uint32_t i = 0; i < count; i++) {
        const vk_electric_filament_candidate_t *candidate = &candidates[i];
        const entity_t *entity = candidate->entity;
        float birth = Q_clipf(candidate->phase / 0.08f, 0.0f, 1.0f);
        birth = birth * birth * (3.0f - 2.0f * birth);
        float fade = 1.0f - candidate->phase;
        fade *= sqrtf(max(fade, 0.0f));
        float far_fade = Q_clipf((768.0f - candidate->distance) / 192.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf((candidate->distance - 8.0f) / 24.0f,
                                  0.0f, 1.0f);
        float facing_fade = Q_clipf((candidate->facing - 0.02f) / 0.28f,
                                    0.0f, 1.0f);
        float envelope = birth * fade * far_fade * near_fade * facing_fade;
        float event_scale = Q_clipf(entity->scale, 0.75f, 1.25f);
        float base_color[3] = {
            entity->rgba.u8[0] / 255.0f,
            entity->rgba.u8[1] / 255.0f,
            entity->rgba.u8[2] / 255.0f,
        };
        float accent_color[3];
        for (int channel = 0; channel < 3; channel++)
            accent_color[channel] = min(base_color[channel] * 0.52f +
                                        0.48f, 1.0f);

        vec3_t tangent, bitangent, start;
        MakeNormalVectors(candidate->normal, tangent, bitangent);
        VectorMA(entity->origin, 2.2f, candidate->normal, start);

        for (int branch = 0; branch < 3; branch++) {
            uint32_t hash = vk_dust_hash((int)candidate->seed, branch,
                                         (int)(candidate->seed >> 16));
            float angle = vk_dust_random(hash) * (2.0f * M_PIf);
            vec3_t surface_direction, side_direction;
            VectorScale(tangent, cosf(angle), surface_direction);
            VectorMA(surface_direction, sinf(angle), bitangent,
                     surface_direction);
            VectorScale(tangent, -sinf(angle), side_direction);
            VectorMA(side_direction, cosf(angle), bitangent, side_direction);

            float length = event_scale *
                (18.0f + vk_dust_random(hash >> 7) * 18.0f) *
                (0.88f + strength * 0.24f);
            vec3_t midpoint, end;
            VectorMA(start, length *
                     (0.42f + vk_dust_random(hash >> 11) * 0.16f),
                     surface_direction, midpoint);
            VectorMA(midpoint,
                     (vk_dust_random(hash >> 15) - 0.5f) * length * 0.34f,
                     side_direction, midpoint);
            VectorMA(midpoint, 1.5f + vk_dust_random(hash >> 3) * 2.5f,
                     candidate->normal, midpoint);
            VectorMA(start, length, surface_direction, end);
            VectorMA(end,
                     (vk_dust_random(hash >> 20) - 0.5f) * length * 0.22f,
                     side_direction, end);
            VectorMA(end, 0.8f + vk_dust_random(hash >> 23) * 2.2f,
                     candidate->normal, end);

            float pulse = 0.76f + 0.24f * sinf(
                candidate->phase * (7.0f + branch) * M_PIf +
                vk_dust_random(hash >> 5) * (2.0f * M_PIf));
            const float *branch_color = branch ? base_color : accent_color;
            float color[4] = {
                branch_color[0], branch_color[1], branch_color[2],
                min((0.32f + strength * 0.40f) * envelope * pulse,
                    0.72f),
            };
            float width = event_scale * (0.65f + strength * 1.05f) *
                (branch ? 0.82f : 1.0f);
            vk_append_effect_segment(&vertex_count, start, midpoint,
                                     fd->vieworg, width, color);
            vk_append_effect_segment(&vertex_count, midpoint, end,
                                     fd->vieworg, width * 0.78f, color);
        }
    }
    return vertex_count;
}

typedef struct {
    const entity_t *entity;
    vec3_t normal;
    float phase;
    float distance;
    float facing;
} vk_impact_candidate_t;

static uint32_t vk_append_impact_marks(const refdef_t *fd,
                                       uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_impact_marks ||
        !vk.impact_texture.descriptor_set || !fd->entities ||
        fd->num_entities <= 0 || (fd->rdflags & RDF_NOWORLDMODEL) ||
        !vk.world.cache || !vk.world.cache->nodes)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_impact_marks, 0.0f, 1.0f);
    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 6,
                             (uint32_t)VK_MAX_IMPACT_MARKS);
    if (strength <= 0.0f || !available)
        return vertex_count;

    vk_impact_candidate_t candidates[VK_MAX_IMPACT_MARKS] = { 0 };
    uint32_t count = 0;

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *entity = &fd->entities[i];
        if ((entity->flags & (RF_EFFECT_ONLY | RF_RT_IMPACT)) !=
            (RF_EFFECT_ONLY | RF_RT_IMPACT))
            continue;

        float phase = (entity->oldframe +
            1.0f - Q_clipf(entity->backlerp, 0.0f, 1.0f)) * 0.25f;
        if (phase < 0.0f || phase >= 1.0f)
            continue;

        float distance = Distance(entity->origin, fd->vieworg);
        if (distance < 6.0f || distance >= 768.0f)
            continue;

        vec3_t normal, to_view, leaf_point;
        VectorCopy(entity->oldorigin, normal);
        if (VectorNormalize(normal) <= 0.0f)
            continue;
        VectorSubtract(fd->vieworg, entity->origin, to_view);
        if (VectorNormalize(to_view) <= 0.0f)
            continue;
        float facing = DotProduct(normal, to_view);
        if (facing <= 0.02f)
            continue;

        VectorMA(entity->origin, 2.0f, normal, leaf_point);
        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes, leaf_point);
        if (!leaf || leaf->visframe != vk.world.visframe ||
            (leaf->contents[0] & CONTENTS_SOLID) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        uint32_t slot = min(count, available - 1);
        if (count >= available && distance >= candidates[slot].distance)
            continue;
        while (slot > 0 && distance < candidates[slot - 1].distance) {
            candidates[slot] = candidates[slot - 1];
            slot--;
        }
        candidates[slot].entity = entity;
        VectorCopy(normal, candidates[slot].normal);
        candidates[slot].phase = phase;
        candidates[slot].distance = distance;
        candidates[slot].facing = facing;
        if (count < available)
            count++;
    }

    for (uint32_t i = 0; i < count; i++) {
        const vk_impact_candidate_t *candidate = &candidates[i];
        const entity_t *entity = candidate->entity;
        float birth = Q_clipf(candidate->phase / 0.08f, 0.0f, 1.0f);
        birth = birth * birth * (3.0f - 2.0f * birth);
        float fade = 1.0f - candidate->phase;
        fade *= sqrtf(max(fade, 0.0f));
        float far_fade = Q_clipf((768.0f - candidate->distance) / 192.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf((candidate->distance - 6.0f) / 18.0f,
                                  0.0f, 1.0f);
        float facing_fade = Q_clipf((candidate->facing - 0.02f) / 0.23f,
                                    0.0f, 1.0f);
        color_t source_color;
        source_color.u32 = entity->skinnum >= 0 ?
            d_8to24table[entity->skinnum & 0xff] : entity->rgba.u32;
        float color[4] = {
            source_color.u8[0] / 255.0f,
            source_color.u8[1] / 255.0f,
            source_color.u8[2] / 255.0f,
            min((0.34f + 0.38f * strength) * birth * fade * far_fade *
                near_fade * facing_fade, 0.72f),
        };
        float event_scale = Q_clipf(entity->scale, 0.65f, 1.25f);
        float radius = (5.0f + candidate->phase * 19.0f) * event_scale *
            (0.90f + strength * 0.15f);
        vk_append_oriented_effect_quad(&vertex_count, entity->origin,
                                       candidate->normal, radius, color);
    }
    return vertex_count;
}

typedef struct {
    uint32_t index;
    float score;
    float distance;
    float fade;
} vk_corona_light_t;

static uint32_t vk_select_corona_lights(const refdef_t *fd,
                                        vk_corona_light_t *lights,
                                        uint32_t limit)
{
    if (!fd || !fd->dlights || fd->num_dlights <= 0 || !limit ||
        !vk.world.cache || !vk.world.cache->nodes)
        return 0;

    uint32_t count = 0;

    for (int i = 0; i < fd->num_dlights; i++) {
        const dlight_t *light = &fd->dlights[i];
        float peak = max(light->color[0],
                         max(light->color[1], light->color[2]));
        if (light->intensity < 160.0f || peak <= 0.10f)
            continue;

        float distance = Distance(light->origin, fd->vieworg);
        if (distance >= 1024.0f)
            continue;
        float far_fade = Q_clipf((1024.0f - distance) / 256.0f,
                                 0.0f, 1.0f);
        float near_fade = Q_clipf((distance - 8.0f) / 16.0f,
                                  0.0f, 1.0f);
        float fade = far_fade * near_fade;
        if (fade <= 0.0f)
            continue;

        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes,
                                            light->origin);
        if (!leaf || leaf->visframe != vk.world.visframe ||
            (leaf->contents[0] & CONTENTS_SOLID) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        float score = light->intensity * peak * fade;
        uint32_t slot = min(count, limit - 1);
        while (slot > 0 && score > lights[slot - 1].score) {
            lights[slot] = lights[slot - 1];
            slot--;
        }
        if (count >= limit && score <= lights[slot].score)
            continue;
        lights[slot].index = i;
        lights[slot].score = score;
        lights[slot].distance = distance;
        lights[slot].fade = fade;
        if (count < limit)
            count++;
    }
    return count;
}

static uint32_t vk_append_light_coronas(const refdef_t *fd,
                                        const vec3_t viewaxis[3],
                                        uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_light_coronas ||
        (fd->rdflags & RDF_NOWORLDMODEL) ||
        !vk_dynamic_lights_enabled() || !vk.world.cache ||
        !vk.world.cache->nodes)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_light_coronas, 0.0f, 1.0f);
    uint32_t available = (VK_MAX_PARTICLE_VERTICES - vertex_count) / 6;
    if (strength <= 0.0f || !available)
        return vertex_count;

    bool draw_core = available >= 2;
    uint32_t limit = draw_core ? available / 2 : 1;
    limit = min(limit, (uint32_t)VK_MAX_LIGHT_CORONAS);
    vk_corona_light_t lights[VK_MAX_LIGHT_CORONAS] = { 0 };
    uint32_t count = vk_select_corona_lights(fd, lights, limit);

    for (uint32_t i = 0; i < count; i++) {
        const dlight_t *light = &fd->dlights[lights[i].index];
        float peak = max(light->color[0],
                         max(light->color[1], light->color[2]));
        float energy = 0.35f + 0.65f * Q_clipf(
            (light->intensity - 160.0f) / 320.0f, 0.0f, 1.0f);
        float reach = max(light->intensity - DLIGHT_CUTOFF, 1.0f);
        float scale = Q_clipf(8.0f + reach * 0.06f, 10.0f, 32.0f) *
            (0.80f + 0.35f * strength);
        float color[4];
        for (int channel = 0; channel < 3; channel++)
            color[channel] = Q_clipf(light->color[channel] /
                max(peak, 0.001f) * (0.70f + 0.30f * strength),
                0.0f, 1.0f);
        color[3] = min((0.08f + 0.14f * strength) * energy *
                       lights[i].fade, 0.24f);
        vk_append_particle_quad(&vertex_count, light->origin, VK_RT_SPRITE_HALF(scale),
                                color, viewaxis);

        if (draw_core) {
            for (int channel = 0; channel < 3; channel++)
                color[channel] += (1.0f - color[channel]) * 0.35f;
            color[3] = min((0.10f + 0.16f * strength) * energy *
                           lights[i].fade, 0.28f);
            vk_append_particle_quad(&vertex_count, light->origin,
                                    VK_RT_SPRITE_HALF(scale * 0.38f), color, viewaxis);
        }
    }
    return vertex_count;
}

typedef struct {
    uint32_t index;
    float score;
} vk_emissive_mote_source_t;

static uint32_t vk_select_emissive_mote_sources(
    const refdef_t *fd,
    vk_emissive_mote_source_t sources[VK_MAX_EMISSIVE_MOTE_SOURCES])
{
    uint32_t count = 0;

    if (!fd || !vk.world.cache || !vk.world.cache->nodes ||
        !vk.world.surface_lights_ready)
        return 0;

    for (uint32_t i = 0; i < vk.world.surface_light_count; i++) {
        const vk_surface_light_t *light = &vk.world.surface_lights[i];
        float distance = Distance(light->origin, fd->vieworg);
        if (distance >= 512.0f)
            continue;

        // Surface-light centers lie directly on BSP planes and can classify
        // into the solid leaf. Test a point safely in front of the emitter.
        vec3_t leaf_point;
        VectorMA(light->origin, 8.0f, light->normal, leaf_point);
        const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes, leaf_point);
        if (!leaf)
            continue;
        if (leaf->visframe != vk.world.visframe)
            continue;
        if ((leaf->contents[0] & (CONTENTS_SOLID | MASK_WATER)) ||
            (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
            continue;

        float fade = Q_clipf((512.0f - distance) / 192.0f, 0.0f, 1.0f);
        float score = light->strength * fade *
            (0.75f + 0.25f * min(light->radius / 24.0f, 1.0f));
        uint32_t slot = min(count,
                            (uint32_t)VK_MAX_EMISSIVE_MOTE_SOURCES - 1);
        while (slot > 0 && score > sources[slot - 1].score) {
            sources[slot] = sources[slot - 1];
            slot--;
        }
        if (count >= VK_MAX_EMISSIVE_MOTE_SOURCES &&
            score <= sources[slot].score)
            continue;
        sources[slot].index = i;
        sources[slot].score = score;
        if (count < VK_MAX_EMISSIVE_MOTE_SOURCES)
            count++;
    }
    return count;
}

static uint32_t vk_append_emissive_motes(const refdef_t *fd,
                                         const vec3_t viewaxis[3],
                                         uint32_t vertex_count)
{
    if (!fd || !vk.raytracing_active || !vk_rt_emissive_motes ||
        (fd->rdflags & (RDF_UNDERWATER | RDF_NOWORLDMODEL)) ||
        vk.view_liquid_kind || !vk.world.cache || !vk.world.cache->nodes)
        return vertex_count;

    float strength = Cvar_ClampValue(vk_rt_emissive_motes, 0.0f, 1.0f);
    uint32_t available = min((VK_MAX_PARTICLE_VERTICES - vertex_count) / 6,
                             (uint32_t)VK_MAX_EMISSIVE_MOTES);
    if (strength <= 0.0f || !available)
        return vertex_count;

    vk_emissive_mote_source_t sources[VK_MAX_EMISSIVE_MOTE_SOURCES] = { 0 };
    uint32_t source_count = vk_select_emissive_mote_sources(fd, sources);
    uint32_t mote_count = 0;
    int per_source = 1 + (strength >= 0.35f) + (strength >= 0.75f);

    for (uint32_t i = 0; i < source_count && mote_count < available; i++) {
        const vk_surface_light_t *light =
            &vk.world.surface_lights[sources[i].index];
        vec3_t reference = { 0.0f, 0.0f, 1.0f };
        vec3_t tangent, bitangent;
        if (fabsf(light->normal[2]) > 0.90f)
            VectorSet(reference, 0.0f, 1.0f, 0.0f);
        CrossProduct(reference, light->normal, tangent);
        if (VectorNormalize(tangent) <= 0.0f)
            continue;
        CrossProduct(light->normal, tangent, bitangent);
        VectorNormalize(bitangent);

        uint32_t source_hash = vk_dust_hash(
            (int)floorf(light->origin[0] * 0.125f),
            (int)floorf(light->origin[1] * 0.125f),
            (int)floorf(light->origin[2] * 0.125f)) ^ sources[i].index;
        for (int j = 0; j < per_source && mote_count < available; j++) {
            uint32_t hash = vk_dust_hash((int)source_hash, j,
                                         (int)sources[i].index);
            float phase = vk_dust_random(hash) * (2.0f * M_PIf);
            float speed = 0.32f + vk_dust_random(hash >> 7) * 0.24f;
            float angle = fd->time * speed + phase;
            float orbit = Q_clipf(light->radius *
                                  (0.25f + vk_dust_random(hash >> 11) * 0.25f),
                                  2.0f, 14.0f);
            float outward = 6.0f + 16.0f *
                (0.5f + 0.5f * sinf(angle * 0.73f + phase * 0.41f));
            vec3_t origin;
            VectorMA(light->origin, outward, light->normal, origin);
            VectorMA(origin, sinf(angle) * orbit, tangent, origin);
            VectorMA(origin, cosf(angle * 0.83f + phase) * orbit,
                     bitangent, origin);

            float distance = Distance(origin, fd->vieworg);
            if (distance >= 512.0f)
                continue;
            const mleaf_t *leaf = BSP_PointLeaf(vk.world.cache->nodes, origin);
            if (!leaf || leaf->visframe != vk.world.visframe ||
                (leaf->contents[0] & (CONTENTS_SOLID | MASK_WATER)) ||
                (fd->areabits && !Q_IsBitSet(fd->areabits, leaf->area)))
                continue;

            float far_fade = Q_clipf((512.0f - distance) / 192.0f,
                                     0.0f, 1.0f);
            float near_fade = Q_clipf((distance - 16.0f) / 32.0f,
                                      0.0f, 1.0f);
            float peak = max(light->color[0],
                             max(light->color[1], light->color[2]));
            float color[4];
            for (int cidx = 0; cidx < 3; cidx++)
                color[cidx] = Q_clipf(light->color[cidx] /
                    max(peak, 0.001f) * (0.55f + 0.30f * strength),
                    0.0f, 1.0f);
            color[3] = (0.08f + 0.10f * strength) * far_fade * near_fade;
            float scale = 1.1f + vk_dust_random(hash >> 15) * 0.8f +
                distance * 0.002f;
            vk_append_particle_quad(&vertex_count, origin, VK_RT_SPRITE_HALF(scale),
                                    color, viewaxis);
            mote_count++;
        }
    }
    return vertex_count;
}
#else
static uint32_t vk_append_rail_ionization_pulses(const refdef_t *fd,
                                                 uint32_t vertex_count)
{
    (void)fd;
    return vertex_count;
}

static uint32_t vk_append_rail_ionization_filaments(const refdef_t *fd,
                                                    uint32_t vertex_count)
{
    (void)fd;
    return vertex_count;
}

static uint32_t vk_append_teleport_vortices(const refdef_t *fd,
                                            const vec3_t viewaxis[3],
                                            uint32_t vertex_count)
{
    (void)fd;
    (void)viewaxis;
    return vertex_count;
}

static uint32_t vk_append_energy_collapses(const refdef_t *fd,
                                            const vec3_t viewaxis[3],
                                            uint32_t vertex_count)
{
    (void)fd;
    (void)viewaxis;
    return vertex_count;
}

static uint32_t vk_append_item_materializations(const refdef_t *fd,
                                                uint32_t vertex_count)
{
    (void)fd;
    return vertex_count;
}

static uint32_t vk_append_landing_dust(const refdef_t *fd,
                                       const vec3_t viewaxis[3],
                                       uint32_t vertex_count)
{
    (void)fd;
    (void)viewaxis;
    return vertex_count;
}

static uint32_t vk_append_electric_filaments(const refdef_t *fd,
                                             uint32_t vertex_count)
{
    (void)fd;
    return vertex_count;
}

static uint32_t vk_append_splash_ripples(const refdef_t *fd,
                                         uint32_t vertex_count)
{
    (void)fd;
    return vertex_count;
}

static uint32_t vk_append_water_wakes(const refdef_t *fd,
                                      uint32_t vertex_count)
{
    (void)fd;
    return vertex_count;
}

static uint32_t vk_append_impact_marks(const refdef_t *fd,
                                       uint32_t vertex_count)
{
    (void)fd;
    return vertex_count;
}

static uint32_t vk_append_shockwaves(const refdef_t *fd,
                                     const vec3_t viewaxis[3],
                                     uint32_t vertex_count)
{
    (void)fd;
    (void)viewaxis;
    return vertex_count;
}

static uint32_t vk_append_lava_embers(const refdef_t *fd,
                                      const vec3_t viewaxis[3],
                                      uint32_t vertex_count)
{
    (void)fd;
    (void)viewaxis;
    return vertex_count;
}

static uint32_t vk_append_light_coronas(const refdef_t *fd,
                                        const vec3_t viewaxis[3],
                                        uint32_t vertex_count)
{
    (void)fd;
    (void)viewaxis;
    return vertex_count;
}

static uint32_t vk_append_emissive_motes(const refdef_t *fd,
                                         const vec3_t viewaxis[3],
                                         uint32_t vertex_count)
{
    (void)fd;
    (void)viewaxis;
    return vertex_count;
}
#endif

void vk_draw_particles(const refdef_t *fd)
{
    if (!fd || !vk.sprite_pipeline || !vk.particle_texture.descriptor_set ||
        !vk.particle_vertices.buffer || !vk.particle_vertices.memory ||
        !vk.particle_vertices_mapped)
        return;

    vk_draw_scope_t old_scope = vk.draw_scope;
    vk.draw_scope = VK_DRAW_PARTICLE;

    vec3_t viewaxis[3];
    VkPipeline pipeline = (vk_partstyle && vk_partstyle->integer &&
        vk.particle_add_pipeline) ? vk.particle_add_pipeline : vk.sprite_pipeline;
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;
    mat4_t proj, view, mvp;
    vk_world_push_t push = { 0 };
    uint32_t vertex_count = 0;

    AnglesToAxis(fd->viewangles, viewaxis);

    vk_projection_matrix(proj, fd->fov_x, fd->fov_y, fd->rdflags);
    vk_view_matrix(view, fd);
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    vk_fog_params(fd, push.fog);
    push.intensity = 1.0f;

    for (int i = 0; fd->particles && i < fd->num_particles &&
         vertex_count + 6 <= VK_MAX_PARTICLE_VERTICES; i++) {
        const particle_t *particle = &fd->particles[i];
        vec3_t transformed;
        vec_t dist, scale;
        color_t color;

        VectorSubtract(particle->origin, fd->vieworg, transformed);
        dist = DotProduct(transformed, viewaxis[0]);

        scale = 1.0f;
        if (dist > 20.0f)
            scale += dist * 0.004f;
        scale *= (vk_partscale ? vk_partscale->value : 2.0f) * particle->scale;
        if (particle->color == -1)
            color.u32 = particle->rgba.u32;
        else
            color.u32 = d_8to24table[particle->color & 0xff];
        color.u8[3] *= particle->alpha;

        float rgba[4] = {
            color.u8[0] / 255.0f, color.u8[1] / 255.0f,
            color.u8[2] / 255.0f, color.u8[3] / 255.0f,
        };
        // The OpenGL renderer draws a triangle whose texture coordinates run
        // to VK_PARTICLE_SIZE, so the round particle texture fills a circle
        // of radius scale * VK_PARTICLE_SCALE around the origin. Draw a quad
        // that holds exactly that circle.
        vk_append_particle_quad(&vertex_count, particle->origin,
                                scale * VK_PARTICLE_SCALE, rgba, viewaxis);
    }

    uint32_t particle_vertex_count = vertex_count;
    vertex_count = vk_append_dust_motes(fd, viewaxis, vertex_count);
    vertex_count = vk_append_landing_dust(fd, viewaxis, vertex_count);
    uint32_t dust_vertex_count = vertex_count;
    vertex_count = vk_append_light_coronas(fd, viewaxis, vertex_count);
    vertex_count = vk_append_emissive_motes(fd, viewaxis, vertex_count);
    vertex_count = vk_append_lava_embers(fd, viewaxis, vertex_count);
    uint32_t shockwave_first_vertex = vertex_count;
    vertex_count = vk_append_shockwaves(fd, viewaxis, vertex_count);
    vertex_count = vk_append_splash_ripples(fd, vertex_count);
    vertex_count = vk_append_water_wakes(fd, vertex_count);
    vertex_count = vk_append_teleport_vortices(fd, viewaxis, vertex_count);
    vertex_count = vk_append_energy_collapses(fd, viewaxis, vertex_count);
    vertex_count = vk_append_item_materializations(fd, vertex_count);
    uint32_t impact_first_vertex = vertex_count;
    vertex_count = vk_append_impact_marks(fd, vertex_count);
    vertex_count = vk_append_rail_ionization_pulses(fd, vertex_count);
    uint32_t electric_first_vertex = vertex_count;
    vertex_count = vk_append_electric_filaments(fd, vertex_count);
    vertex_count = vk_append_rail_ionization_filaments(fd, vertex_count);

    if (!vertex_count) {
        vk.draw_scope = old_scope;
        return;
    }

    VkDeviceSize size = vertex_count * sizeof(vk.particle_batch[0]);
    memcpy((byte *)vk.particle_vertices_mapped + offset, vk.particle_batch, size);

    vk_matrix_multiply(mvp, proj, view);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    Vector4Set(push.color, 1.0f, 1.0f, 1.0f, 1.0f);

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.particle_vertices.buffer, &offset);
    vk_bind_texture_descriptor(cmd, vk.particle_texture.descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    if (particle_vertex_count) {
        vk.CmdDraw(cmd, particle_vertex_count, 1, 0, 0);
        c.trisDrawn += particle_vertex_count / 3;
        vk_count_batch3d();
    }
    if (dust_vertex_count > particle_vertex_count) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         vk.sprite_pipeline);
        vk.CmdDraw(cmd, dust_vertex_count - particle_vertex_count, 1,
                   particle_vertex_count, 0);
        c.trisDrawn += (dust_vertex_count - particle_vertex_count) / 3;
        vk_count_batch3d();
    }
    if (shockwave_first_vertex > dust_vertex_count) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         vk.particle_add_pipeline ?
                            vk.particle_add_pipeline : vk.sprite_pipeline);
        vk.CmdDraw(cmd, shockwave_first_vertex - dust_vertex_count, 1,
                   dust_vertex_count, 0);
        c.trisDrawn += (shockwave_first_vertex - dust_vertex_count) / 3;
        vk_count_batch3d();
    }
    if (impact_first_vertex > shockwave_first_vertex) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         vk.particle_add_pipeline ?
                            vk.particle_add_pipeline : vk.sprite_pipeline);
        vk_bind_texture_descriptor(cmd, vk.shockwave_texture.descriptor_set);
        vk.CmdDraw(cmd, impact_first_vertex - shockwave_first_vertex, 1,
                   shockwave_first_vertex, 0);
        c.trisDrawn += (impact_first_vertex - shockwave_first_vertex) / 3;
        vk_count_batch3d();
    }
    if (electric_first_vertex > impact_first_vertex) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         vk.particle_add_pipeline ?
                            vk.particle_add_pipeline : vk.sprite_pipeline);
        vk_bind_texture_descriptor(cmd, vk.impact_texture.descriptor_set);
        vk.CmdDraw(cmd, electric_first_vertex - impact_first_vertex, 1,
                   impact_first_vertex, 0);
        c.trisDrawn += (electric_first_vertex - impact_first_vertex) / 3;
        vk_count_batch3d();
    }
    if (vertex_count > electric_first_vertex) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         vk.particle_add_pipeline ?
                            vk.particle_add_pipeline : vk.sprite_pipeline);
        vk_bind_texture_descriptor(cmd, vk.beam_texture.descriptor_set);
        vk.CmdDraw(cmd, vertex_count - electric_first_vertex, 1,
                   electric_first_vertex, 0);
        c.trisDrawn += (vertex_count - electric_first_vertex) / 3;
        vk_count_batch3d();
    }
    vk.draw_scope = old_scope;
}

#endif // USE_VULKAN
