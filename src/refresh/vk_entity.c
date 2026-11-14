/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan entities: alias models, shells, cel-shading, shadows, sprites, flares, beams and the entity passes.

#include "vk_local.h"

#if USE_VULKAN

static const image_t *vk_skin_for_model(const vk_model_t *model, const entity_t *ent)
{
    if (ent->flags & RF_SHELL_MASK)
        return R_SHELLTEXTURE;
    if (ent->skin)
        return IMG_ForHandle(ent->skin);
    if (!model->skin_count)
        return R_NOTEXTURE;
    if (ent->skinnum < 0 || ent->skinnum >= model->skin_count)
        return model->skins[0];
    if (model->skins[ent->skinnum] == R_NOTEXTURE)
        return model->skins[0];
    return model->skins[ent->skinnum];
}

static const image_t *vk_skin_for_alias_batch(const vk_model_t *model,
                                              const vk_alias_batch_t *batch,
                                              const entity_t *ent)
{
    if (ent->flags & RF_SHELL_MASK)
        return R_SHELLTEXTURE;
    if (ent->skin)
        return IMG_ForHandle(ent->skin);
    if (!batch || !batch->skin_count)
        return vk_skin_for_model(model, ent);

    int skin = ent->skinnum;
    if (skin < 0 || skin >= batch->skin_count)
        skin = 0;

    const image_t *image = model->skins[batch->skin_start + skin];
    if (image == R_NOTEXTURE && batch->skin_count > 0)
        image = model->skins[batch->skin_start];
    if (!image)
        image = R_NOTEXTURE;

    return image;
}

void vk_add_dynamic_lights(const refdef_t *fd, const vec3_t origin, vec3_t color)
{
    if (!vk_dynamic_lights_enabled())
        return;
    if (!fd || fd->num_dlights <= 0 || !fd->dlights)
        return;

    const dlight_t *light = fd->dlights;

    for (int i = 0; i < fd->num_dlights; i++, light++) {
        float f = light->intensity - DLIGHT_CUTOFF - Distance(light->origin, origin);
        if (f > 0.0f) {
            f *= 1.0f / 255.0f;
            VectorMA(color, f, light->color, color);
        }
    }
}

float vk_entity_light_modulate(void)
{
    return Cvar_ClampValue(vk_modulate, 0.0f, 1e6f) *
           Cvar_ClampValue(vk_modulate_entities, 0.0f, 1e6f);
}

void vk_adjust_entity_light(vec3_t color)
{
    float add = 255.0f * Cvar_ClampValue(vk_brightness, -1.0f, 1.0f);
    float modulate = vk_entity_light_modulate();
    float scale = Cvar_ClampValue(vk_coloredlightmaps, 0.0f, 1.0f);
    float maxc;

    color[0] = max((color[0] + add) * modulate, 0.0f);
    color[1] = max((color[1] + add) * modulate, 0.0f);
    color[2] = max((color[2] + add) * modulate, 0.0f);

    maxc = max(max(color[0], color[1]), color[2]);
    if (maxc > 255.0f)
        VectorScale(color, 255.0f / maxc, color);

    if (scale != 1.0f) {
        float y = LUMINANCE(color[0], color[1], color[2]);
        color[0] = y + (color[0] - y) * scale;
        color[1] = y + (color[1] - y) * scale;
        color[2] = y + (color[2] - y) * scale;
    }

    VectorScale(color, 1.0f / 255.0f, color);
}

static void vk_entity_light_color(const entity_t *ent, const refdef_t *fd, vec4_t color)
{
    uint64_t flags = ent->flags;
    float f, m;

    if (flags & RF_SHELL_MASK) {
        VectorClear(color);
        if (flags & RF_SHELL_LITE_GREEN)
            VectorSet(color, 0.56f, 0.93f, 0.56f);
        if (flags & RF_SHELL_HALF_DAM)
            VectorSet(color, 0.56f, 0.59f, 0.45f);
        if (flags & RF_SHELL_DOUBLE) {
            color[0] = 0.9f;
            color[1] = 0.7f;
        }
        if (flags & RF_SHELL_RED)
            color[0] = 1.0f;
        if (flags & RF_SHELL_GREEN)
            color[1] = 1.0f;
        if (flags & RF_SHELL_BLUE)
            color[2] = 1.0f;
        // highlight shells in the color of the item's skin tint
        if (flags & RF_SKINTINT)
            VectorCopy(ent->skin_tint, color);
    } else if (flags & RF_FULLBRIGHT) {
        VectorSet(color, 1.0f, 1.0f, 1.0f);
    } else if ((flags & RF_IR_VISIBLE) && (fd->rdflags & RDF_IRGOGGLES)) {
        VectorSet(color, 1.0f, 0.0f, 0.0f);
    } else if (flags & RF_TRACKER) {
        VectorClear(color);
    } else {
        if ((vk_fullbright && vk_fullbright->integer) ||
            !vk_static_light_point(ent->origin, fd, color)) {
            VectorSet(color, 1.0f, 1.0f, 1.0f);
        }

        vk_add_dynamic_lights(fd, ent->origin, color);

        if (vk_doublelight_entities && vk_doublelight_entities->integer)
            VectorScale(color, vk_entity_light_modulate(), color);

        if (flags & RF_MINLIGHT) {
            f = VectorLength(color);
            if (!f)
                VectorSet(color, 0.1f, 0.1f, 0.1f);
            else if (f < 0.1f)
                VectorScale(color, 0.1f / f, color);
        }

        if (flags & RF_GLOW) {
            f = 0.1f * sinf(fd->time * 7.0f);
            for (int i = 0; i < 3; i++) {
                m = color[i] * 0.8f;
                color[i] += f;
                if (color[i] < m)
                    color[i] = m;
            }
        }

        color[0] = Q_clipf(color[0], 0.0f, 1.0f);
        color[1] = Q_clipf(color[1], 0.0f, 1.0f);
        color[2] = Q_clipf(color[2], 0.0f, 1.0f);
    }

    color[3] = (flags & RF_TRANSLUCENT) ? ent->alpha : 1.0f;
}

static void vk_alias_shadedir(const entity_t *ent, vec4_t shadedir)
{
    float yaw, cy, sy, cp, sp;

    Vector4Clear(shadedir);

    if (!vk_dotshading || !vk_dotshading->integer)
        return;
    if (ent->flags & (RF_SHELL_MASK | RF_TRACKER))
        return;

    yaw = -DEG2RAD(ent->angles[YAW]);
    cy = cosf(yaw);
    sy = sinf(yaw);
    cp = cosf(-M_PIf / 4.0f);
    sp = sinf(-M_PIf / 4.0f);

    shadedir[0] = cp * cy;
    shadedir[1] = cp * sy;
    shadedir[2] = -sp;
    shadedir[3] = 1.0f;
}

static void vk_draw_alias_pass(VkCommandBuffer cmd, VkPipeline pipeline,
                               const VkBuffer buffers[2],
                               const VkDeviceSize offsets[2],
                               const vk_model_t *model,
                               const vk_alias_batch_t *batch,
                               const vk_texture_t *texture,
                               const vk_alias_push_t *push)
{
    uint32_t first_index = batch ? batch->first_index : 0;
    uint32_t index_count = batch ? batch->index_count : model->mesh.index_count;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_vertex_buffers(cmd, 0, 2, buffers, offsets);
    vk_bind_index_buffer(cmd, model->mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(*push), push);
    vk.CmdDrawIndexed(cmd, index_count, 1, first_index, 0, 0);
    c.trisDrawn += index_count / 3;
    vk_count_batch3d();
}

static void vk_draw_alias_color_pass(VkCommandBuffer cmd, VkPipeline pipeline,
                                     const VkBuffer buffers[2],
                                     const VkDeviceSize offsets[2],
                                     const vk_model_t *model,
                                     const vk_alias_shadow_push_t *push,
                                     bool count_stats)
{
    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_vertex_buffers(cmd, 0, 2, buffers, offsets);
    vk_bind_index_buffer(cmd, model->mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(*push), push);
    vk.CmdDrawIndexed(cmd, model->mesh.index_count, 1, 0, 0, 0);
    if (count_stats) {
        c.trisDrawn += model->mesh.index_count / 3;
        vk_count_batch3d();
    }
}

static void vk_draw_alias_cel_edges(VkCommandBuffer cmd,
                                    const VkBuffer buffers[2],
                                    const VkDeviceSize offsets[2],
                                    const vk_model_t *model,
                                    const vk_alias_batch_t *batch,
                                    const vk_alias_push_t *base_push,
                                    const entity_t *ent,
                                    const refdef_t *fd,
                                    float alpha)
{
    if (!vk.alias_cel_pipeline || !vk.textures[1].descriptor_set)
        return;

    uint32_t first_index = batch ? batch->first_index : 0;
    uint32_t index_count = batch ? batch->index_count : model->mesh.index_count;
    if (!index_count)
        return;

    vk_alias_push_t push = *base_push;
    Vector4Clear(push.skin_tint);
    float width = Cvar_ClampValue(vk_celshading, 0.0f, 10.0f) * alpha;
    float distance = max(Distance(ent->origin, fd->vieworg), 1.0f);
    float view_height = max(fd->height, 1);
    float entity_scale = ent->scale ? fabsf(ent->scale) : 1.0f;
    float world_per_pixel = 2.0f * distance *
        tanf(DEG2RAD(fd->fov_y) * 0.5f) / view_height;

    Vector4Set(push.color, 0.0f, 0.0f, 0.0f, alpha);
    Vector4Clear(push.shadedir);
    push.shellscale = width * world_per_pixel / entity_scale;
    push.depthscale = (ent->flags & RF_DEPTHHACK) ? 0.25f : 1.0f;
    push.intensity = 1.0f;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                     vk.alias_cel_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 2, buffers, offsets);
    vk_bind_index_buffer(cmd, model->mesh.indices.buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, vk.textures[1].descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, index_count, 1, first_index, 0, 0);
    vk_count_batch3d();
}

static void vk_draw_alias_outlines(VkCommandBuffer cmd,
                                    const VkBuffer buffers[2],
                                    const VkDeviceSize offsets[2],
                                    vk_model_t *model,
                                    const vk_alias_batch_t *batch,
                                    const vk_texture_t *texture,
                                    const vk_alias_push_t *push,
                                    const entity_t *ent,
                                    const refdef_t *fd)
{
    bool showtris = gl_showtris && (gl_showtris->integer & SHOWTRIS_MESH);
    float cel_alpha = 0.0f;

    if (vk_celshading && vk_celshading->value > 0.0f && fd &&
        !(ent->flags & (RF_TRANSLUCENT | RF_SHELL_MASK | RF_TRACKER))) {
        cel_alpha = 1.0f - Distance(ent->origin, fd->vieworg) / 700.0f;
        if (cel_alpha >= 0.01f) {
            vk_draw_alias_cel_edges(cmd, buffers, offsets, model, batch,
                                    push, ent, fd, cel_alpha);
        }
    }

    if (!showtris)
        return;

    if (!vk.alias_line_pipeline || !model->alias_line_indices.buffer ||
        !model->alias_line_index_count)
        return;

    uint32_t first_index = batch ? (batch->first_index / 3) * 6 : 0;
    uint32_t index_count = batch ?
        (batch->index_count / 3) * 6 : model->alias_line_index_count;

    if (!index_count ||
        first_index + index_count > model->alias_line_index_count)
        return;

    vk_alias_push_t outline = *push;
    Vector4Clear(outline.skin_tint);
    Vector4Set(outline.color, 0.0f, 0.0f, 0.0f, 1.0f);
    Vector4Clear(outline.shadedir);
    outline.depthscale = 0.0f;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                       vk.alias_line_pipeline);
    vk_bind_vertex_buffers(cmd, 0, 2, buffers, offsets);
    vk_bind_index_buffer(cmd, model->alias_line_indices.buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(outline), &outline);
    vk.CmdDrawIndexed(cmd, index_count, 1, first_index, 0, 0);
    vk_count_batch3d();
}

void vk_trace_bmodel_light_points(const refdef_t *fd, const bsp_t *bsp,
                                         const vec3_t origin, const vec3_t end,
                                         lightpoint_t *point)
{
    if (!fd)
        return;

    for (int i = 0; i < fd->num_entities; i++) {
        const entity_t *ent = &fd->entities[i];
        if (!(ent->model & BIT(31)))
            continue;

        int index = ~ent->model;
        if (index < 1 || index >= bsp->nummodels)
            continue;

        const mmodel_t *model = &bsp->models[index];
        if (!model->numfaces)
            continue;

        const vec_t *angles = NULL;
        if (!VectorEmpty(ent->angles)) {
            if (fabsf(origin[0] - ent->origin[0]) > model->radius ||
                fabsf(origin[1] - ent->origin[1]) > model->radius)
                continue;
            angles = ent->angles;
        } else {
            vec3_t mins, maxs;
            VectorAdd(model->mins, ent->origin, mins);
            VectorAdd(model->maxs, ent->origin, maxs);
            if (origin[0] < mins[0] || origin[0] > maxs[0] ||
                origin[1] < mins[1] || origin[1] > maxs[1])
                continue;
        }

        lightpoint_t model_point;
        BSP_TransformedLightPoint(&model_point, origin, end,
                                  model->headnode,
                                  vk.world.nolm_mask | SURF_TRANS_MASK,
                                  ent->origin, angles);
        if (model_point.fraction < point->fraction)
            *point = model_point;
    }
}

static bool vk_alias_shadow_point(const entity_t *ent, const refdef_t *fd,
                                  lightpoint_t *point)
{
    const bsp_t *bsp = vk.world.cache;
    vec3_t end;

    if (!bsp || !bsp->nodes || !bsp->lightmap)
        return false;

    VectorCopy(ent->origin, end);
    end[2] -= 8192.0f;
    BSP_LightPoint(point, ent->origin, end, bsp->nodes,
                   vk.world.nolm_mask | SURF_TRANS_MASK);
    vk_trace_bmodel_light_points(fd, bsp, ent->origin, end, point);
    return point->surf != NULL;
}

static void vk_shadow_projection_matrix(mat4_t matrix, const cplane_t *plane,
                                        const vec3_t dir)
{
    matrix[ 0] =  plane->normal[1] * dir[1] + plane->normal[2] * dir[2];
    matrix[ 4] = -plane->normal[1] * dir[0];
    matrix[ 8] = -plane->normal[2] * dir[0];
    matrix[12] =  plane->dist * dir[0];

    matrix[ 1] = -plane->normal[0] * dir[1];
    matrix[ 5] =  plane->normal[0] * dir[0] + plane->normal[2] * dir[2];
    matrix[ 9] = -plane->normal[2] * dir[1];
    matrix[13] =  plane->dist * dir[1];

    matrix[ 2] = -plane->normal[0] * dir[2];
    matrix[ 6] = -plane->normal[1] * dir[2];
    matrix[10] =  plane->normal[0] * dir[0] + plane->normal[1] * dir[1];
    matrix[14] =  plane->dist * dir[2];

    matrix[ 3] = 0.0f;
    matrix[ 7] = 0.0f;
    matrix[11] = 0.0f;
    matrix[15] = DotProduct(plane->normal, dir);
}

static void vk_draw_alias_shadow(const entity_t *ent, const refdef_t *fd,
                                 const vec3_t axis[3],
                                 const vk_model_t *model,
                                 const VkBuffer buffers[2],
                                 const VkDeviceSize offsets[2],
                                 const vk_alias_lerp_t *lerp,
                                 bool count_stats)
{
    lightpoint_t point;
    vec3_t dir;
    float w, radius, alpha = 0.5f;
    vec4_t color;
    mat4_t proj, view, model_matrix, shadow_proj, shadow_model, view_model;
    vk_alias_shadow_push_t push;

    if (!vk_shadows || !vk_shadows->integer || !vk.alias_shadow_pipeline)
        return;
    if (ent->flags & (RF_WEAPONMODEL | RF_NOSHADOW))
        return;
    // OpenGL obtains the shadow receiver from its normal entity-lighting
    // query. Entities that bypass that query do not cast a projected shadow.
    if (ent->flags & (RF_SHELL_MASK | RF_FULLBRIGHT | RF_TRACKER))
        return;
    if ((ent->flags & RF_IR_VISIBLE) && (fd->rdflags & RDF_IRGOGGLES))
        return;
    if (!model->alias_frames || !vk_alias_shadow_point(ent, fd, &point))
        return;

    w = point.plane.normal[2];
    if (point.surf->drawflags & DSURF_PLANEBACK)
        w = -w;
    if (w < 0.5f)
        return;

    radius = model->alias_frames[lerp->frame].radius * (1.0f - lerp->backlerp) +
        model->alias_frames[lerp->oldframe].radius * lerp->backlerp;
    radius *= ent->scale ? ent->scale : 1.0f;

    if (vk_shadows->integer >= 2) {
        float dist = ent->origin[2] - point.pos[2] - radius;
        if (dist > radius * 4.0f)
            return;
        if (dist > 0.0f)
            alpha = 0.5f - dist / (radius * 8.0f);
    }

    if (vk_cull_models && vk_cull_models->integer) {
        float min_d = -radius / w;
        for (int i = 0; i < 4; i++) {
            if (PlaneDiff(point.pos, &vk.world.frustum[i]) < min_d) {
                c.shadowsCulled++;
                return;
            }
        }
    }

    if (point.surf->drawflags & DSURF_PLANEBACK)
        VectorSet(dir, 0.0f, 0.0f, -1.0f);
    else
        VectorSet(dir, 0.0f, 0.0f, 1.0f);

    vk_shadow_projection_matrix(shadow_proj, &point.plane, dir);
    vk_entity_model_matrix(model_matrix, ent, axis);
    vk_matrix_multiply(shadow_model, shadow_proj, model_matrix);
    vk_projection_matrix(proj, fd->fov_x, fd->fov_y, fd->rdflags);
    vk_view_matrix(view, fd);
    vk_matrix_multiply(view_model, view, shadow_model);
    vk_matrix_multiply(push.mvp, proj, view_model);

    vk_entity_light_color(ent, fd, color);
    Vector4Set(push.color, 0.0f, 0.0f, 0.0f, color[3] * alpha);
    Vector4Clear(push.shadedir);
    push.backlerp = lerp->backlerp;
    push.shellscale = (ent->flags & RF_SHELL_MASK) && !(ent->flags & RF_NOSHELLSCALE) ?
        POWERSUIT_SCALE : 0.0f;
    push.depthscale = (ent->flags & RF_DEPTHHACK) ? 0.25f : 1.0f;
    push._pad = 0.0f;
    vk_fog_params(fd, push.fog);
    push.intensity = 1.0f;
    VectorClear(push._pad2);
    Vector4Set(push.height_x, shadow_model[0], shadow_model[4],
               shadow_model[8], shadow_model[12]);
    Vector4Set(push.height_y, shadow_model[1], shadow_model[5],
               shadow_model[9], shadow_model[13]);
    Vector4Set(push.height_z, shadow_model[2], shadow_model[6],
               shadow_model[10], shadow_model[14]);
    vk_height_fog_params(fd, push.heightfog_start, push.heightfog_end,
                         push.heightfog_view, push.heightfog_params);

    vk_draw_alias_color_pass(vk.command_buffers[vk.current_image],
                              vk.alias_shadow_pipeline, buffers, offsets,
                              model, &push, count_stats);
}

static void vk_draw_alias_model(const entity_t *ent, const refdef_t *fd)
{
    vk_model_t *model = vk_model_for_handle(ent->model);
#if USE_MD5
    vk_model_t md5_view;
#endif

    bool translucent = ent->flags & RF_TRANSLUCENT;
    bool bloom_only = ent->flags & RF_BLOOM_ONLY;
    bool bloom_shell = (ent->flags & RF_SHELL_MASK) && !(ent->flags & RF_NOBLOOM);
    bool draw_model = !bloom_only || vk.drawing_bloom;

    if (!model || model->type != VK_MODEL_ALIAS ||
        !model->mesh.vertices.buffer || !model->mesh.indices.buffer ||
        !model->vertex_count || !model->alias_batch_count)
        return;

#if USE_MD5
    if (model->md5_mesh.vertices.buffer && model->md5_mesh.indices.buffer &&
        model->md5_frame_count && model->md5_vertex_count &&
        vk_md5_use && vk_md5_use->integer &&
        ((ent->flags & RF_NO_LOD) || !vk_md5_distance ||
         vk_md5_distance->value <= 0.0f ||
         Distance(ent->origin, fd->vieworg) <= vk_md5_distance->value)) {
        md5_view = *model;
        md5_view.mesh = model->md5_mesh;
        md5_view.alias_line_indices = model->md5_line_indices;
        md5_view.alias_line_index_count = model->md5_line_index_count;
        // Match OpenGL: replacement geometry uses the original model's frame
        // bounds for visibility and projected-shadow sizing.
        md5_view.alias_frames = model->alias_frames;
        md5_view.alias_batches = model->md5_batches;
        md5_view.skins = model->md5_skins;
        md5_view.frame_count = model->md5_frame_count;
        md5_view.alias_batch_count = model->md5_batch_count;
        md5_view.skin_count = model->md5_skin_count;
        md5_view.vertex_count = model->md5_vertex_count;
        model = &md5_view;
    }
#endif

    if (vk.drawing_bloom && !bloom_only && !bloom_shell &&
        !vk_alias_model_has_glowmap(model))
        return;

    vec3_t axis[3];
    mat4_t mvp;
    vk_alias_push_t push;
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    vk_alias_lerp_t lerp = vk_alias_lerp_for_entity(model, ent, fd);
    VkDeviceSize offsets[] = {
        (VkDeviceSize)lerp.frame * model->vertex_count * sizeof(vk_vertex_t),
        (VkDeviceSize)lerp.oldframe * model->vertex_count * sizeof(vk_vertex_t),
    };
    VkBuffer buffers[] = {
        model->mesh.vertices.buffer,
        model->mesh.vertices.buffer,
    };

    vk_entity_axis(ent, axis);
    if (vk_alias_model_culled(model, ent, axis, lerp.frame, lerp.oldframe)) {
        if (!vk.drawing_bloom)
            vk_draw_alias_shadow(ent, fd, axis, model, buffers, offsets, &lerp, true);
        return;
    }

    if (!draw_model) {
        vk_draw_alias_shadow(ent, fd, axis, model, buffers, offsets, &lerp, true);
        return;
    }

    vk_entity_mvp(mvp, fd, ent, axis);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    vk_entity_light_color(ent, fd, push.color);
    vk_alias_shadedir(ent, push.shadedir);
    push.backlerp = lerp.backlerp;
    push.shellscale = (ent->flags & RF_SHELL_MASK) && !(ent->flags & RF_NOSHELLSCALE) ?
        ((ent->flags & RF_WEAPONMODEL) ? WEAPONSHELL_SCALE : POWERSUIT_SCALE) : 0.0f;
    push.depthscale = (ent->flags & RF_DEPTHHACK) ? 0.25f : 1.0f;
    push._pad = 0.0f;
    vk_fog_params(fd, push.fog);
    push.intensity = vk_texture_intensity();
    push.desaturation = 0.0f;
    push._pad2[0] = push._pad2[1] = 0.0f;
    // Skin tints, as in the OpenGL renderer: shells keep their own color.
    Vector4Clear(push.skin_tint_dark);
    if ((ent->flags & RF_SKINTINT) && !(ent->flags & RF_SHELL_MASK)) {
        if (ent->skin_tint_gamma > 0.0f) {
            VectorCopy(ent->skin_tint, push.skin_tint);
            VectorCopy(ent->skin_tint_dark, push.skin_tint_dark);
            push.skin_tint_dark[3] = ent->skin_tint_gamma;
        } else {
            VectorScale(ent->skin_tint, SKINTINT_BRIGHTNESS, push.skin_tint);
        }
        push.skin_tint[3] = 1.0f;
    } else {
        Vector4Clear(push.skin_tint);
    }

    for (int i = 0; i < model->alias_batch_count; i++) {
        const vk_alias_batch_t *batch = &model->alias_batches[i];
        const image_t *skin = vk_skin_for_alias_batch(model, batch, ent);
        VkPipeline pipeline;

        if (!skin || skin->texnum >= MAX_RIMAGES)
            continue;

        const vk_texture_t *texture = vk_texture_for_index(skin->texnum, true);
        if (!texture)
            continue;

        // Shells glow from their front faces only, as in OpenGL, which culls
        // back faces. Without culling, their front and back faces were both
        // blended around the model, doubling the glow into a bright ring.
        if (vk.drawing_bloom && (ent->flags & RF_SHELL_MASK) &&
            !vk_entity_mirrored(ent) && vk.alias_shell_bloom_pipeline)
            pipeline = vk.alias_shell_bloom_pipeline;
        else if (vk.drawing_bloom)
            pipeline = (bloom_only || bloom_shell) ? vk.alias_bloom_pipeline : VK_NULL_HANDLE;
        else if (translucent)
            pipeline = vk.alias_blend_pipeline;
        else if ((skin->flags & IF_TRANSPARENT) && vk.alias_alpha_pipeline)
            pipeline = vk.alias_alpha_pipeline;
        else
            pipeline = vk.alias_pipeline;

        if (!vk.drawing_bloom && translucent &&
            (ent->flags & (RF_FULLBRIGHT | RF_BLOOM_ONLY)) == 0 &&
            vk.alias_depth_pipeline) {
            vk_draw_alias_pass(cmd, vk.alias_depth_pipeline, buffers, offsets,
                               model, batch, texture, &push);
        }

        if (pipeline)
            vk_draw_alias_pass(cmd, pipeline, buffers, offsets, model, batch,
                               texture, &push);
        if (skin->texnum2 && skin->texnum2 < MAX_RIMAGES &&
            vk.alias_blend_pipeline) {
            const vk_texture_t *glow = vk_texture_for_index(skin->texnum2, false);

            if (glow) {
                vk_alias_push_t glow_push = push;
                // Glowmaps are added untinted, as in the OpenGL renderer.
                Vector4Clear(glow_push.skin_tint);

                glow_push.intensity = vk_glowmap_intensity();
                vk_draw_alias_pass(cmd, vk.drawing_bloom ? vk.alias_bloom_pipeline :
                                   vk.alias_blend_pipeline, buffers, offsets,
                                   model, batch, glow, &glow_push);
            }
        }
        if (!vk.drawing_bloom)
            vk_draw_alias_outlines(cmd, buffers, offsets, model, batch,
                                   texture, &push, ent, fd);
    }

    if (!vk.drawing_bloom)
        vk_draw_alias_shadow(ent, fd, axis, model, buffers, offsets, &lerp, false);
}

static void vk_draw_sprite(const entity_t *ent, const refdef_t *fd)
{
    vk_model_t *model = vk_model_for_handle(ent->model);

    if (!model || model->type != VK_MODEL_SPRITE || !vk.sprite_pipeline ||
        !vk.sprite_quad.vertices.buffer || !vk.sprite_quad.indices.buffer)
        return;

    const vk_sprite_frame_t *frame = &model->frames[ent->frame % model->frame_count];
    if (!frame->image || frame->image->texnum >= MAX_RIMAGES)
        return;

    const vk_texture_t *texture = vk_texture_for_index(frame->image->texnum, true);
    if (!texture)
        return;

    bool translucent = ent->flags & RF_TRANSLUCENT;
    VkPipeline pipeline = vk.drawing_bloom ? vk.sprite_bloom_pipeline :
                                             vk.sprite_pipeline;

    if (!vk.drawing_bloom && !translucent &&
        (frame->image->flags & (IF_TRANSPARENT | IF_PALETTED)) ==
            (IF_TRANSPARENT | IF_PALETTED) &&
        vk.sprite_alpha_pipeline) {
        pipeline = vk.sprite_alpha_pipeline;
    }

    vec3_t viewaxis[3], left, right, down, up, xaxis, yaxis, origin;
    float scale = ent->scale ? ent->scale : 1.0f;
    mat4_t model_matrix, mvp;
    vk_world_push_t push = { 0 };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    AnglesToAxis(fd->viewangles, viewaxis);
    VectorScale(viewaxis[1], frame->origin_x * scale, left);
    VectorScale(viewaxis[1], (frame->origin_x - frame->width) * scale, right);
    VectorScale(viewaxis[2], -frame->origin_y * scale, down);
    VectorScale(viewaxis[2], (frame->height - frame->origin_y) * scale, up);
    VectorSubtract(right, left, xaxis);
    VectorSubtract(up, down, yaxis);
    VectorAdd3(ent->origin, left, down, origin);

    memset(model_matrix, 0, sizeof(model_matrix));
    model_matrix[0] = xaxis[0];
    model_matrix[1] = xaxis[1];
    model_matrix[2] = xaxis[2];
    model_matrix[4] = yaxis[0];
    model_matrix[5] = yaxis[1];
    model_matrix[6] = yaxis[2];
    model_matrix[10] = 1.0f;
    model_matrix[12] = origin[0];
    model_matrix[13] = origin[1];
    model_matrix[14] = origin[2];
    model_matrix[15] = 1.0f;

    vk_model_mvp(mvp, fd, model_matrix);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    push.color[0] = 1.0f;
    push.color[1] = 1.0f;
    push.color[2] = 1.0f;
    push.color[3] = translucent ? ent->alpha : 1.0f;
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    vk_fog_params(fd, push.fog);
    push.intensity = 1.0f;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.sprite_quad.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.sprite_quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, vk.sprite_quad.index_count, 1, 0, 0, 0);
    c.trisDrawn += vk.sprite_quad.index_count / 3;
    vk_count_batch3d();
    vk_draw_fx_outlines(&vk.sprite_quad, &vk.sprite_quad_line_indices,
                        vk.sprite_quad_line_index_count, mvp);
}

static bool vk_flare_occluded(const entity_t *ent, const refdef_t *fd)
{
    bsp_t *bsp = vk.world.cache;
    lightpoint_t point;
    vec3_t end;

    if (!bsp || !bsp->nodes)
        return false;

    VectorCopy(ent->origin, end);
    const mleaf_t *leaf = BSP_PointLeaf(bsp->nodes, end);
    if (leaf && (leaf->contents[0] & CONTENTS_SOLID)) {
        vec3_t dir;

        VectorSubtract(end, fd->vieworg, dir);
        if (VectorNormalize(dir) > 0.0f)
            VectorMA(end, -5.0f, dir, end);
    }

    BSP_LightPoint(&point, fd->vieworg, end, bsp->nodes,
                   vk.world.nolm_mask | SURF_TRANS_MASK);
    vk_trace_bmodel_light_points(fd, bsp, fd->vieworg, end, &point);

    return point.surf && point.fraction < 0.995f;
}

static bool vk_flare_visible(const entity_t *ent, const refdef_t *fd)
{
    for (int i = 0; i < 4; i++) {
        if (PlaneDiff(ent->origin, &vk.world.frustum[i]) < -2.5f)
            return false;
    }

    return !vk_flare_occluded(ent, fd);
}

static float vk_flare_frac(const entity_t *ent, const refdef_t *fd)
{
    float target = vk_flare_visible(ent, fd) ? 1.0f : 0.0f;
    int key = ent->skinnum;

    if (key < 0 || key >= MAX_EDICTS)
        return target;

    float *frac = &vk.flare_fracs[key];
    float speed = vk_flarespeed ? vk_flarespeed->value : 8.0f;

    if (com_eventTime - vk.flare_times[key] >= 2500)
        *frac = 0.0f;
    vk.flare_times[key] = com_eventTime;

    if (speed <= 0.0f) {
        *frac = target;
    } else if (*frac < target) {
        *frac += speed * fd->frametime;
        if (*frac > target)
            *frac = target;
    } else if (*frac > target) {
        *frac -= speed * fd->frametime;
        if (*frac < target)
            *frac = target;
    }

    return *frac;
}

static void vk_draw_flare(const entity_t *ent, const refdef_t *fd)
{
    if (ent->skin <= 0 || ent->skin >= r_numImages || !vk.sprite_pipeline ||
        !vk.sprite_quad.vertices.buffer || !vk.sprite_quad.indices.buffer)
        return;

    float frac = vk_flare_frac(ent, fd);
    if (frac <= 0.0f)
        return;

    const image_t *image = IMG_ForHandle(ent->skin);
    if (!image || image->texnum >= MAX_RIMAGES)
        return;

    const vk_texture_t *texture = vk_texture_for_index(image->texnum, true);
    if (!texture)
        return;

    bool def = image->flags & IF_DEFAULT_FLARE;
    float scale = (float)(25 << def) * (ent->scale * frac);
    if (scale <= 0.0f)
        return;

    vec3_t viewaxis[3], left, right, down, up, xaxis, yaxis, origin;
    mat4_t model_matrix, mvp;
    vk_world_push_t push = { 0 };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;
    color_t color;

    AnglesToAxis(fd->viewangles, viewaxis);
    if (ent->flags & RF_FLARE_LOCK_ANGLE) {
        VectorScale(viewaxis[1], scale, left);
        VectorScale(viewaxis[1], -scale, right);
        VectorScale(viewaxis[2], -scale, down);
        VectorScale(viewaxis[2], scale, up);
    } else {
        vec3_t dir, r, u;

        VectorSubtract(ent->origin, fd->vieworg, dir);
        if (VectorNormalize(dir) <= 0.0f)
            return;

        MakeNormalVectors(dir, r, u);
        VectorScale(r, -scale, left);
        VectorScale(r, scale, right);
        VectorScale(u, -scale, down);
        VectorScale(u, scale, up);
    }

    VectorSubtract(right, left, xaxis);
    VectorSubtract(up, down, yaxis);
    VectorAdd3(ent->origin, left, down, origin);

    memset(model_matrix, 0, sizeof(model_matrix));
    model_matrix[0] = xaxis[0];
    model_matrix[1] = xaxis[1];
    model_matrix[2] = xaxis[2];
    model_matrix[4] = yaxis[0];
    model_matrix[5] = yaxis[1];
    model_matrix[6] = yaxis[2];
    model_matrix[10] = 1.0f;
    model_matrix[12] = origin[0];
    model_matrix[13] = origin[1];
    model_matrix[14] = origin[2];
    model_matrix[15] = 1.0f;

    vk_model_mvp(mvp, fd, model_matrix);
    memcpy(push.mvp, mvp, sizeof(push.mvp));

    color.u32 = ent->rgba.u32;
    if (ent->flags & (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE)) {
        VectorClear(color.u8);
        if (ent->flags & RF_SHELL_RED)
            color.u8[0] = 255;
        if (ent->flags & RF_SHELL_GREEN)
            color.u8[1] = 255;
        if (ent->flags & RF_SHELL_BLUE)
            color.u8[2] = 255;
    }
    color.u8[3] = (128 + def * 32) * (ent->alpha * frac);

    push.color[0] = color.u8[0] / 255.0f;
    push.color[1] = color.u8[1] / 255.0f;
    push.color[2] = color.u8[2] / 255.0f;
    push.color[3] = color.u8[3] / 255.0f;
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    vk_fog_params(fd, push.fog);
    push.intensity = def ? -1.0f : 1.0f;

    VkPipeline pipeline = (def && vk.particle_add_pipeline) ?
        vk.particle_add_pipeline : vk.sprite_pipeline;

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_vertex_buffers(cmd, 0, 1, &vk.sprite_quad.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.sprite_quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, vk.sprite_quad.index_count, 1, 0, 0, 0);
    c.trisDrawn += vk.sprite_quad.index_count / 3;
    vk_count_batch3d();
    vk_draw_fx_outlines(&vk.sprite_quad, &vk.sprite_quad_line_indices,
                        vk.sprite_quad_line_index_count, mvp);
}

static void vk_draw_beam_segment(const vec3_t start, const vec3_t end,
                                 const refdef_t *fd, const float color[4],
                                 float width)
{
    vec3_t dir, to_view, normal, xaxis, yaxis, origin;
    mat4_t model_matrix, mvp;
    vk_world_push_t push = { 0 };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    VectorSubtract(end, start, dir);
    VectorSubtract(fd->vieworg, start, to_view);
    CrossProduct(dir, to_view, normal);
    if (VectorNormalize(normal) < 0.1f)
        return;

    VectorScale(normal, width, normal);
    VectorCopy(dir, xaxis);
    VectorScale(normal, -2.0f, yaxis);
    VectorAdd(start, normal, origin);

    memset(model_matrix, 0, sizeof(model_matrix));
    model_matrix[0] = xaxis[0];
    model_matrix[1] = xaxis[1];
    model_matrix[2] = xaxis[2];
    model_matrix[4] = yaxis[0];
    model_matrix[5] = yaxis[1];
    model_matrix[6] = yaxis[2];
    model_matrix[10] = 1.0f;
    VectorAdd(start, normal, origin);
    model_matrix[12] = origin[0];
    model_matrix[13] = origin[1];
    model_matrix[14] = origin[2];
    model_matrix[15] = 1.0f;

    vk_model_mvp(mvp, fd, model_matrix);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    memcpy(push.color, color, sizeof(push.color));
    Vector4Clear(push.scroll);
    Vector4Clear(push.dlight);
    vk_fog_params(fd, push.fog);
    push.intensity = 1.0f;

    vk_bind_vertex_buffers(cmd, 0, 1, &vk.sprite_quad.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.sprite_quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, vk.sprite_quad.index_count, 1, 0, 0, 0);
    c.trisDrawn += vk.sprite_quad.index_count / 3;
    vk_count_batch3d();
    vk_draw_fx_outlines(&vk.sprite_quad, &vk.sprite_quad_line_indices,
                        vk.sprite_quad_line_index_count, mvp);
    if (gl_showtris && (gl_showtris->integer & SHOWTRIS_FX)) {
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.sprite_pipeline);
        vk_bind_vertex_buffers(cmd, 0, 1, &vk.sprite_quad.vertices.buffer, &offset);
        vk_bind_index_buffer(cmd, vk.sprite_quad.indices.buffer, 0,
                             VK_INDEX_TYPE_UINT32);
        vk_bind_texture_descriptor(cmd, vk.beam_texture.descriptor_set);
    }
}

static void vk_draw_poly_beam_segment(const vec3_t start, const vec3_t end,
                                      const refdef_t *fd, const float color[4],
                                      float width)
{
    if (!vk.beam_cylinder.vertices.buffer || !vk.beam_cylinder.indices.buffer)
        return;

    vec3_t dir, right, up;
    mat4_t model_matrix, mvp;
    vk_color3d_push_t push;
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    VectorSubtract(end, start, dir);
    if (VectorNormalize(dir) < 0.1f)
        return;

    MakeNormalVectors(dir, right, up);
    VectorScale(right, width, right);
    VectorScale(up, width, up);

    memset(model_matrix, 0, sizeof(model_matrix));
    VectorSubtract(end, start, dir);
    model_matrix[0] = dir[0];
    model_matrix[1] = dir[1];
    model_matrix[2] = dir[2];
    model_matrix[4] = right[0];
    model_matrix[5] = right[1];
    model_matrix[6] = right[2];
    model_matrix[8] = up[0];
    model_matrix[9] = up[1];
    model_matrix[10] = up[2];
    model_matrix[12] = start[0];
    model_matrix[13] = start[1];
    model_matrix[14] = start[2];
    model_matrix[15] = 1.0f;

    vk_model_mvp(mvp, fd, model_matrix);
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    memcpy(push.color, color, sizeof(push.color));

    vk_bind_vertex_buffers(cmd, 0, 1, &vk.beam_cylinder.vertices.buffer, &offset);
    vk_bind_index_buffer(cmd, vk.beam_cylinder.indices.buffer, 0,
                         VK_INDEX_TYPE_UINT32);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, vk.beam_cylinder.index_count, 1, 0, 0, 0);
    c.trisDrawn += vk.beam_cylinder.index_count / 3;
    vk_count_batch3d();
    vk_draw_fx_outlines(&vk.beam_cylinder, &vk.beam_cylinder_line_indices,
                        vk.beam_cylinder_line_index_count, mvp);
    if (gl_showtris && (gl_showtris->integer & SHOWTRIS_FX))
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                         vk.drawing_bloom ? vk.beam_bloom_pipeline : vk.beam_pipeline);
}

#define VK_MIN_LIGHTNING_SEGMENTS   3
#define VK_MAX_LIGHTNING_SEGMENTS   7
#define VK_MIN_SEGMENT_LENGTH       16

static void vk_draw_lightning_beam(const vec3_t start, const vec3_t end,
                                   const refdef_t *fd, const float color[4],
                                   float width, bool poly)
{
    vec3_t dir, segments[VK_MAX_LIGHTNING_SEGMENTS + 1];
    vec3_t right, up;
    vec_t length, segment_length;
    int num_segments, max_segments;

    VectorSubtract(end, start, dir);
    length = VectorNormalize(dir);
    if (length < 0.1f)
        return;

    max_segments = Q_clip(length / VK_MIN_SEGMENT_LENGTH, 1, VK_MAX_LIGHTNING_SEGMENTS);
    if (max_segments <= VK_MIN_LIGHTNING_SEGMENTS)
        num_segments = max_segments;
    else
        num_segments = VK_MIN_LIGHTNING_SEGMENTS +
            Com_SlowRand() % (max_segments - VK_MIN_LIGHTNING_SEGMENTS + 1);

    if (num_segments > 1)
        MakeNormalVectors(dir, right, up);

    segment_length = length / num_segments;
    for (int i = 1; i < num_segments; i++) {
        vec3_t point;
        float offs;

        VectorMA(start, i * segment_length, dir, point);

        offs = Com_SlowCrand() * (segment_length * 0.35f);
        VectorMA(point, offs, right, point);

        offs = Com_SlowCrand() * (segment_length * 0.35f);
        VectorMA(point, offs, up, segments[i]);
    }

    VectorCopy(start, segments[0]);
    VectorCopy(end, segments[num_segments]);

    if (poly) {
        for (int i = 0; i < num_segments; i++)
            vk_draw_poly_beam_segment(segments[i], segments[i + 1], fd, color, width);
    } else {
        for (int i = 0; i < num_segments; i++)
            vk_draw_beam_segment(segments[i], segments[i + 1], fd, color, width);
    }
}

static void vk_draw_beam(const entity_t *ent, const refdef_t *fd)
{
    if ((!vk.beam_pipeline && !vk.sprite_pipeline) ||
        !vk.sprite_quad.vertices.buffer || !vk.sprite_quad.indices.buffer ||
        !ent->frame)
        return;

    color_t color;
    float push_color[4];
    bool poly = vk_beamstyle && vk_beamstyle->integer;
    bool additive = ent->flags & RF_BEAM_ADDITIVE;
    float scale = poly ? 0.5f : 1.2f;
    float width = abs((int16_t)ent->frame) * scale;
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];

    if (ent->skinnum == -1)
        color.u32 = ent->rgba.u32;
    else
        color.u32 = d_8to24table[ent->skinnum & 0xff];
    color.u8[3] *= ent->alpha;

    push_color[0] = color.u8[0] / 255.0f;
    push_color[1] = color.u8[1] / 255.0f;
    push_color[2] = color.u8[2] / 255.0f;
    push_color[3] = color.u8[3] / 255.0f;

    if (poly) {
        VkPipeline pipeline;
        if (additive)
            pipeline = vk.drawing_bloom ? vk.beam_add_bloom_pipeline :
                vk.beam_add_pipeline;
        else
            pipeline = vk.drawing_bloom ? vk.beam_bloom_pipeline :
                vk.beam_pipeline;
        if (!pipeline)
            return;
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    } else {
        VkPipeline pipeline = vk.drawing_bloom ? vk.sprite_bloom_pipeline :
            (additive ? vk.particle_add_pipeline : vk.sprite_pipeline);
        if (!pipeline || !vk.beam_texture.descriptor_set)
            return;
        vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vk_bind_texture_descriptor(cmd, vk.beam_texture.descriptor_set);
    }

    if (ent->flags & RF_GLOW) {
        vk_draw_lightning_beam(ent->origin, ent->oldorigin, fd, push_color,
                               width, poly);
    } else if (poly) {
        vk_draw_poly_beam_segment(ent->origin, ent->oldorigin, fd, push_color, width);
    } else {
        vk_draw_beam_segment(ent->origin, ent->oldorigin, fd, push_color, width);
    }
}

static bool vk_entity_in_pass(const entity_t *ent, vk_entity_pass_t pass)
{
    if (ent->flags & RF_EFFECT_ONLY)
        return false;

    if (ent->flags & RF_BEAM)
        return pass == VK_ENTITY_BEAM;

    if (ent->flags & RF_BLOOM_ONLY) {
        if (pass == VK_ENTITY_BLOOM_ONLY)
            return true;
        if (pass != VK_ENTITY_OPAQUE || (ent->model & BIT(31)))
            return false;

        vk_model_t *model = vk_model_for_handle(ent->model);
        return model && model->type == VK_MODEL_ALIAS;
    }

    if (pass == VK_ENTITY_BLOOM_ONLY)
        return false;

    if (pass == VK_ENTITY_BLOOM_SOURCE) {
        if (ent->flags & RF_FLARE)
            return false;
        if (ent->model & BIT(31))
            return false;

        vk_model_t *model = vk_model_for_handle(ent->model);
        if (!model || model->type != VK_MODEL_ALIAS)
            return false;

        return ((ent->flags & RF_SHELL_MASK) && !(ent->flags & RF_NOBLOOM)) ||
            vk_alias_model_has_glowmap(model);
    }

    if (ent->flags & RF_FLARE)
        return pass == VK_ENTITY_ALPHA_FRONT;

    if ((ent->model & BIT(31)) && pass == VK_ENTITY_BMODEL_ALPHA)
        return !(ent->flags & RF_TRANSLUCENT);

    if (!(ent->flags & RF_TRANSLUCENT))
        return (ent->model & BIT(31)) ?
            pass == VK_ENTITY_BMODEL : pass == VK_ENTITY_OPAQUE;

    if ((ent->flags & RF_WEAPONMODEL) ||
        ent->alpha <= (vk_draworder ? vk_draworder->value : 1.0f)) {
        return pass == VK_ENTITY_ALPHA_FRONT;
    }

    return pass == VK_ENTITY_ALPHA_BACK;
}

static void vk_draw_entity(const entity_t *ent, const refdef_t *fd,
                           vk_entity_pass_t pass)
{
    if (pass == VK_ENTITY_BEAM) {
        vk_draw_beam(ent, fd);
        return;
    }

    if (ent->flags & RF_FLARE) {
        vk_draw_flare(ent, fd);
        return;
    }

    if (ent->model & BIT(31)) {
        vk_draw_bmodel(ent, fd, pass == VK_ENTITY_BMODEL_ALPHA);
        return;
    }

    vk_model_t *model = vk_model_for_handle(ent->model);

    if (!model) {
        vk_draw_null_model(ent, fd);
        return;
    }
    if (model->type == VK_MODEL_SPRITE)
        vk_draw_sprite(ent, fd);
    else if (model->type == VK_MODEL_ALIAS)
        vk_draw_alias_model(ent, fd);

    if (!vk.drawing_bloom && vk_showorigins && vk_showorigins->integer)
        vk_draw_null_model(ent, fd);
}

void vk_draw_entities(const refdef_t *fd, vk_entity_pass_t pass)
{
    if (vk_drawentities && !vk_drawentities->integer)
        return;
    if (fd->num_entities <= 0 || !fd->entities)
        return;

    vk_draw_scope_t old_scope = vk.draw_scope;
    vk.draw_scope = VK_DRAW_ENTITY;

    for (int i = fd->num_entities - 1; i >= 0; i--) {
        const entity_t *ent = &fd->entities[i];

        if (vk_entity_in_pass(ent, pass))
            vk_draw_entity(ent, fd, pass);
    }

    vk.draw_scope = old_scope;
}

#endif // USE_VULKAN
