/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan model loading: sprites, MD2, MD3 and MD5 replacement models.

#include "vk_local.h"

#if USE_VULKAN

static void vk_free_model(vk_model_t *model)
{
    if (model->frames) {
        Z_Free(model->frames);
        model->frames = NULL;
    }
    if (model->alias_frames) {
        Z_Free(model->alias_frames);
        model->alias_frames = NULL;
    }
    if (model->alias_batches) {
        Z_Free(model->alias_batches);
        model->alias_batches = NULL;
    }
    if (model->skins) {
        Z_Free(model->skins);
        model->skins = NULL;
    }
    if (model->skin_names) {
        Z_Free(model->skin_names);
        model->skin_names = NULL;
    }
#if USE_MD5
    if (model->md5_frames)
        Z_Free(model->md5_frames);
    if (model->md5_batches)
        Z_Free(model->md5_batches);
    if (model->md5_skins)
        Z_Free(model->md5_skins);
    vk_destroy_mesh(&model->md5_mesh);
    vk_destroy_buffer(&model->md5_line_indices);
#endif
    vk_destroy_mesh(&model->mesh);
    vk_destroy_buffer(&model->alias_line_indices);
    model->alias_line_index_count = 0;
    memset(model, 0, sizeof(*model));
}

void vk_free_models(bool all)
{
    for (uint32_t i = 0; i < vk.model_count; i++) {
        vk_model_t *model = &vk.models[i];

        if (model->type == VK_MODEL_FREE)
            continue;
        if (!all && model->registration_sequence == r_registration_sequence)
            continue;

        vk_free_model(model);
    }

    if (all)
        vk.model_count = 0;
}

void vk_model_list_f(void)
{
    static const char types[] = "FAS";
    int count = 0;

    Com_Printf("------------------\n");
    for (uint32_t i = 0; i < vk.model_count; i++) {
        const vk_model_t *model = &vk.models[i];

        if (model->type == VK_MODEL_FREE)
            continue;

        Com_Printf("%c %5u verts %5u idx",
                   types[model->type],
                   model->vertex_count,
                   model->mesh.index_count);
        if (model->type == VK_MODEL_ALIAS) {
            Com_Printf(" %2d frm %2d bat %2d skin",
                       model->frame_count,
                       model->alias_batch_count,
                       model->skin_count);
        } else if (model->type == VK_MODEL_SPRITE) {
            Com_Printf(" %2d frm", model->frame_count);
        }
        Com_Printf(" : %s\n", model->name);
        count++;
    }
    Com_Printf("Total Vulkan models: %d (out of %u slots)\n",
               count, vk.model_count);
}

#if USE_DEBUG
size_t VKR_ViewCluster_m(char *buffer, size_t size)
{
    return Q_snprintf(buffer, size, "%d", vk.world.viewcluster);
}

size_t VKR_ViewLeaf_m(char *buffer, size_t size)
{
    const bsp_t *bsp = vk.world.cache;

    if (bsp && bsp->nodes && bsp->leafs) {
        const mleaf_t *leaf = BSP_PointLeaf(bsp->nodes, vk.world.vieworg);

        if (leaf) {
            return Q_snprintf(buffer, size, "%td %d %d %d %#x",
                              leaf - bsp->leafs,
                              leaf->cluster,
                              leaf->numleafbrushes,
                              leaf->numleaffaces,
                              leaf->contents[0]);
        }
    }

    return Q_strlcpy(buffer, "", size);
}
#endif

static vk_model_t *vk_find_model(const char *name)
{
    for (uint32_t i = 0; i < vk.model_count; i++) {
        vk_model_t *model = &vk.models[i];

        if (model->type != VK_MODEL_FREE && !FS_pathcmp(model->name, name))
            return model;
    }

    return NULL;
}

static vk_model_t *vk_alloc_model(void)
{
    for (uint32_t i = 0; i < vk.model_count; i++) {
        if (vk.models[i].type == VK_MODEL_FREE)
            return &vk.models[i];
    }

    if (vk.model_count == q_countof(vk.models))
        return NULL;

    return &vk.models[vk.model_count++];
}

static qhandle_t vk_load_sprite_model(const char *name, const byte *rawdata, size_t length)
{
    dsp2header_t header;
    vk_model_t *model;
    const dsp2frame_t *src_frame;

    if (length < sizeof(header))
        return 0;

    header.ident = LittleLong(((const dsp2header_t *)rawdata)->ident);
    header.version = LittleLong(((const dsp2header_t *)rawdata)->version);
    header.numframes = LittleLong(((const dsp2header_t *)rawdata)->numframes);

    if (header.ident != SP2_IDENT || header.version != SP2_VERSION)
        return 0;
    if (header.numframes < 1 || header.numframes > SP2_MAX_FRAMES)
        return 0;
    if (sizeof(dsp2header_t) + sizeof(dsp2frame_t) * header.numframes > length)
        return 0;

    model = vk_alloc_model();
    if (!model)
        return 0;

    Q_strlcpy(model->name, name, sizeof(model->name));
    model->type = VK_MODEL_SPRITE;
    model->registration_sequence = r_registration_sequence;
    model->frame_count = header.numframes;
    model->frames = Z_Mallocz(sizeof(model->frames[0]) * model->frame_count);

    src_frame = (const dsp2frame_t *)(rawdata + sizeof(dsp2header_t));
    for (int i = 0; i < model->frame_count; i++) {
        char image_name[SP2_MAX_FRAMENAME];
        vk_sprite_frame_t *dst_frame = &model->frames[i];

        dst_frame->width = (int32_t)LittleLong(src_frame[i].width);
        dst_frame->height = (int32_t)LittleLong(src_frame[i].height);
        dst_frame->origin_x = (int32_t)LittleLong(src_frame[i].origin_x);
        dst_frame->origin_y = (int32_t)LittleLong(src_frame[i].origin_y);

        if (!Q_memccpy(image_name, src_frame[i].name, 0, sizeof(image_name)))
            dst_frame->image = R_NOTEXTURE;
        else
            dst_frame->image = IMG_Find(image_name, IT_SPRITE, IF_NONE);
    }

    return (model - vk.models) + 1;
}

static bool vk_check_md2_bounds(const dmd2header_t *header, size_t length)
{
    if (header->skinwidth < 1 || header->skinheight < 1 ||
        header->skinwidth > MD2_MAX_SKINWIDTH ||
        header->skinheight > MD2_MAX_SKINHEIGHT)
        return false;
    if (header->framesize < sizeof(dmd2frame_t) ||
        header->framesize > MD2_MAX_FRAMESIZE)
        return false;
    if (header->num_skins > MD2_MAX_SKINS ||
        header->num_xyz < 3 || header->num_xyz > MD2_MAX_VERTS ||
        header->num_st < 3 || header->num_tris < 1 ||
        header->num_tris > MD2_MAX_TRIANGLES ||
        header->num_frames < 1 || header->num_frames > MD2_MAX_FRAMES)
        return false;
    if ((uint64_t)header->ofs_skins + header->num_skins * MD2_MAX_SKINNAME > length ||
        (uint64_t)header->ofs_st + header->num_st * sizeof(dmd2stvert_t) > length ||
        (uint64_t)header->ofs_tris + header->num_tris * sizeof(dmd2triangle_t) > length ||
        (uint64_t)header->ofs_frames + header->num_frames * header->framesize > length ||
        header->ofs_end > length)
        return false;

    return true;
}

#if USE_MD5
static bool vk_load_md5_variant(vk_model_t *model)
{
    memhunk_t hunk = { 0 };
    md5_model_t *md5;
    vk_vertex_t *vertices = NULL;
    uint32_t *indices = NULL;
    uint32_t *line_indices = NULL;
    uint32_t line_index_count = 0;
    uint32_t vertex_count = 0, index_count = 0;
    bool ok = false;

    if (!model || !vk_md5_load || !vk_md5_load->integer ||
        !model->skin_names || !model->alias_batch_count)
        return false;

    int skin_count = model->alias_batches[0].skin_count;
    md5 = MOD_LoadMD5Replacement(model->name, model->frame_count,
                                 skin_count, model->skin_names, &hunk);
    if (!md5)
        return false;

    for (int i = 0; i < md5->num_meshes; i++) {
        const md5_mesh_t *mesh = &md5->meshes[i];
        if (mesh->num_verts < 1 || mesh->num_indices < 3 ||
            UINT32_MAX - vertex_count < (uint32_t)mesh->num_verts ||
            UINT32_MAX - index_count < (uint32_t)mesh->num_indices)
            goto out;
        vertex_count += mesh->num_verts;
        index_count += mesh->num_indices;
    }
    if (!vertex_count || !index_count || md5->num_frames < 1 ||
        (uint64_t)vertex_count * md5->num_frames > UINT32_MAX)
        goto out;

    vertices = Z_Malloc(sizeof(*vertices) * vertex_count * md5->num_frames);
    indices = Z_Malloc(sizeof(*indices) * index_count);
    model->md5_frames = Z_Mallocz(sizeof(*model->md5_frames) * md5->num_frames);
    model->md5_batches = Z_Mallocz(sizeof(*model->md5_batches) * md5->num_meshes);
    if (md5->num_skins) {
        model->md5_skins = Z_Malloc(sizeof(*model->md5_skins) * md5->num_skins);
        memcpy(model->md5_skins, md5->skins,
               sizeof(*model->md5_skins) * md5->num_skins);
    }

    uint32_t vertex_base = 0, first_index = 0;
    for (int mesh_index = 0; mesh_index < md5->num_meshes; mesh_index++) {
        const md5_mesh_t *mesh = &md5->meshes[mesh_index];
        model->md5_batches[mesh_index] = (vk_alias_batch_t) {
            .first_index = first_index,
            .index_count = mesh->num_indices,
            .skin_start = 0,
            .skin_count = md5->num_skins,
        };

        for (int i = 0; i < mesh->num_indices; i++) {
            if (mesh->indices[i] >= mesh->num_verts)
                goto out;
            indices[first_index + i] = vertex_base + mesh->indices[i];
        }

        for (int frame = 0; frame < md5->num_frames; frame++) {
            const md5_joint_t *skeleton =
                &md5->skeleton_frames[frame * md5->num_joints];
            vk_alias_frame_t *frame_info = &model->md5_frames[frame];
            if (mesh_index == 0)
                ClearBounds(frame_info->bounds[0], frame_info->bounds[1]);

            for (int vert_index = 0; vert_index < mesh->num_verts; vert_index++) {
                const md5_vertex_t *vert = &mesh->vertices[vert_index];
                vk_vertex_t *dst = &vertices[frame * vertex_count +
                                             vertex_base + vert_index];
                VectorClear(dst->position);
                VectorClear(dst->normal);

                for (int weight_index = 0; weight_index < vert->count; weight_index++) {
                    int index = vert->start + weight_index;
                    const md5_weight_t *weight = &mesh->weights[index];
                    const md5_joint_t *joint = &skeleton[mesh->jointnums[index]];
                    vec3_t transformed;

                    VectorRotate(weight->pos, joint->axis, transformed);
                    VectorMA(joint->pos, joint->scale, transformed, transformed);
                    VectorMA(dst->position, weight->bias, transformed, dst->position);
                    VectorRotate(vert->normal, joint->axis, transformed);
                    VectorMA(dst->normal, weight->bias, transformed, dst->normal);
                }

                VectorNormalize(dst->normal);
                Vector4Set(dst->color, 1.0f, 1.0f, 1.0f, 1.0f);
                dst->uv[0] = mesh->tcoords[vert_index].st[0];
                dst->uv[1] = mesh->tcoords[vert_index].st[1];
                AddPointToBounds(dst->position, frame_info->bounds[0],
                                 frame_info->bounds[1]);
            }
        }

        vertex_base += mesh->num_verts;
        first_index += mesh->num_indices;
    }

    for (int frame = 0; frame < md5->num_frames; frame++)
        model->md5_frames[frame].radius =
            RadiusFromBounds(model->md5_frames[frame].bounds[0],
                             model->md5_frames[frame].bounds[1]);

    if (!vk_upload_mesh(&model->md5_mesh, vertices,
                        vertex_count * md5->num_frames, indices, index_count))
        goto out;
    line_indices = vk_build_line_indices(indices, index_count, &line_index_count);
    if (!line_indices ||
        !vk_upload_buffer(&model->md5_line_indices, line_indices,
                          sizeof(*line_indices) * line_index_count,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
        goto out;

    model->md5_line_index_count = line_index_count;
    model->md5_frame_count = md5->num_frames;
    model->md5_batch_count = md5->num_meshes;
    model->md5_skin_count = md5->num_skins;
    model->md5_vertex_count = vertex_count;
    ok = true;

out:
    if (!ok) {
        if (model->md5_frames) {
            Z_Free(model->md5_frames);
            model->md5_frames = NULL;
        }
        if (model->md5_batches) {
            Z_Free(model->md5_batches);
            model->md5_batches = NULL;
        }
        if (model->md5_skins) {
            Z_Free(model->md5_skins);
            model->md5_skins = NULL;
        }
        vk_destroy_mesh(&model->md5_mesh);
        vk_destroy_buffer(&model->md5_line_indices);
    }
    if (vertices)
        Z_Free(vertices);
    if (indices)
        Z_Free(indices);
    if (line_indices)
        Z_Free(line_indices);
    Hunk_Free(&hunk);
    return ok;
}
#endif

static qhandle_t vk_load_md2_model(const char *name, const byte *rawdata, size_t length)
{
    dmd2header_t header;
    const dmd2triangle_t *src_tri;
    const dmd2stvert_t *src_tc;
    vk_model_t *model = NULL;
    uint16_t *vert_indices = NULL;
    uint16_t *tc_indices = NULL;
    uint32_t *final_indices = NULL;
    uint16_t *remap = NULL;
    vk_vertex_t *vertices = NULL;
    uint32_t *indices = NULL;
    uint32_t *line_indices = NULL;
    uint32_t line_index_count = 0;
    vk_alias_frame_t *alias_frames = NULL;
    uint32_t numindices = 0;
    uint32_t numverts = 0;
    qhandle_t handle = 0;

    if (length < sizeof(header))
        return 0;

    const dmd2header_t *src_header = (const dmd2header_t *)rawdata;
    header.ident = LittleLong(src_header->ident);
    header.version = LittleLong(src_header->version);
    header.skinwidth = LittleLong(src_header->skinwidth);
    header.skinheight = LittleLong(src_header->skinheight);
    header.framesize = LittleLong(src_header->framesize);
    header.num_skins = LittleLong(src_header->num_skins);
    header.num_xyz = LittleLong(src_header->num_xyz);
    header.num_st = LittleLong(src_header->num_st);
    header.num_tris = LittleLong(src_header->num_tris);
    header.num_glcmds = LittleLong(src_header->num_glcmds);
    header.num_frames = LittleLong(src_header->num_frames);
    header.ofs_skins = LittleLong(src_header->ofs_skins);
    header.ofs_st = LittleLong(src_header->ofs_st);
    header.ofs_tris = LittleLong(src_header->ofs_tris);
    header.ofs_frames = LittleLong(src_header->ofs_frames);
    header.ofs_glcmds = LittleLong(src_header->ofs_glcmds);
    header.ofs_end = LittleLong(src_header->ofs_end);

    if (header.ident != MD2_IDENT || header.version != MD2_VERSION ||
        !vk_check_md2_bounds(&header, length))
        return 0;

    uint32_t max_indices = header.num_tris * 3;
    vert_indices = Z_Malloc(sizeof(*vert_indices) * max_indices);
    tc_indices = Z_Malloc(sizeof(*tc_indices) * max_indices);
    final_indices = Z_Malloc(sizeof(*final_indices) * max_indices);
    remap = Z_Malloc(sizeof(*remap) * max_indices);

    src_tri = (const dmd2triangle_t *)(rawdata + header.ofs_tris);
    for (uint32_t i = 0; i < header.num_tris; i++) {
        uint32_t base = numindices;
        uint32_t j;

        for (j = 0; j < 3; j++) {
            uint16_t idx_xyz = LittleShort(src_tri[i].index_xyz[j]);
            uint16_t idx_st = LittleShort(src_tri[i].index_st[j]);

            if (idx_xyz >= header.num_xyz || idx_st >= header.num_st)
                break;

            vert_indices[base + j] = idx_xyz;
            tc_indices[base + j] = idx_st;
        }
        if (j == 3)
            numindices += 3;
    }

    if (numindices < 3)
        goto out;

    for (uint32_t i = 0; i < numindices; i++)
        remap[i] = UINT16_MAX;

    src_tc = (const dmd2stvert_t *)(rawdata + header.ofs_st);
    for (uint32_t i = 0; i < numindices; i++) {
        if (remap[i] != UINT16_MAX)
            continue;

        for (uint32_t j = i + 1; j < numindices; j++) {
            if (vert_indices[i] == vert_indices[j] &&
                src_tc[tc_indices[i]].s == src_tc[tc_indices[j]].s &&
                src_tc[tc_indices[i]].t == src_tc[tc_indices[j]].t) {
                remap[j] = i;
                final_indices[j] = numverts;
            }
        }

        remap[i] = i;
        final_indices[i] = numverts++;
    }

    if (!numverts || (uint64_t)numverts * header.num_frames > UINT32_MAX)
        goto out;

    vertices = Z_Malloc(sizeof(*vertices) * numverts * header.num_frames);
    indices = Z_Malloc(sizeof(*indices) * numindices);
    alias_frames = Z_Mallocz(sizeof(*alias_frames) * header.num_frames);

    for (uint32_t i = 0; i < numindices; i++)
        indices[i] = final_indices[i];

    float scale_s = 1.0f / header.skinwidth;
    float scale_t = 1.0f / header.skinheight;
    for (uint32_t frame = 0; frame < header.num_frames; frame++) {
        const dmd2frame_t *src_frame =
            (const dmd2frame_t *)(rawdata + header.ofs_frames + frame * header.framesize);
        vec3_t scale, translate, radius_mins, radius_maxs;

        LittleVector(src_frame->scale, scale);
        LittleVector(src_frame->translate, translate);
        ClearBounds(radius_mins, radius_maxs);
        ClearBounds(alias_frames[frame].bounds[0], alias_frames[frame].bounds[1]);

        for (uint32_t i = 0; i < numindices; i++) {
            if (remap[i] != i)
                continue;

            const dmd2trivertx_t *src_vert = &src_frame->verts[vert_indices[i]];
            vk_vertex_t *dst = &vertices[frame * numverts + final_indices[i]];
            vec3_t relative;

            relative[0] = src_vert->v[0] * scale[0];
            relative[1] = src_vert->v[1] * scale[1];
            relative[2] = src_vert->v[2] * scale[2];
            VectorAdd(relative, translate, dst->position);
            AddPointToBounds(relative, radius_mins, radius_maxs);
            AddPointToBounds(dst->position, alias_frames[frame].bounds[0],
                             alias_frames[frame].bounds[1]);
            dst->color[0] = 1.0f;
            dst->color[1] = 1.0f;
            dst->color[2] = 1.0f;
            dst->color[3] = 1.0f;
            dst->uv[0] = (int16_t)LittleShort(src_tc[tc_indices[i]].s) * scale_s;
            dst->uv[1] = (int16_t)LittleShort(src_tc[tc_indices[i]].t) * scale_t;
            if (src_vert->lightnormalindex < NUMVERTEXNORMALS)
                VectorCopy(bytedirs[src_vert->lightnormalindex], dst->normal);
            else
                VectorSet(dst->normal, 0.0f, 0.0f, 1.0f);
        }

        alias_frames[frame].radius = RadiusFromBounds(radius_mins, radius_maxs);
    }

    model = vk_alloc_model();
    if (!model)
        goto out;

    Q_strlcpy(model->name, name, sizeof(model->name));
    model->type = VK_MODEL_ALIAS;
    model->registration_sequence = r_registration_sequence;
    model->frame_count = header.num_frames;
    model->alias_frames = alias_frames;
    alias_frames = NULL;
    model->alias_batch_count = 1;
    model->alias_batches = Z_Mallocz(sizeof(model->alias_batches[0]));
    model->alias_batches[0] = (vk_alias_batch_t) {
        .first_index = 0,
        .index_count = numindices,
        .skin_start = 0,
        .skin_count = header.num_skins,
    };
    model->vertex_count = numverts;
    model->skin_count = header.num_skins;
    if (model->skin_count) {
        model->skins = Z_Mallocz(sizeof(model->skins[0]) * model->skin_count);
        model->skin_names = Z_Mallocz(sizeof(model->skin_names[0]) * model->skin_count);
    }

    const char *src_skin = (const char *)rawdata + header.ofs_skins;
    for (int i = 0; i < model->skin_count; i++) {
        char skin_name[MD2_MAX_SKINNAME];

        if (!Q_memccpy(skin_name, src_skin, 0, sizeof(skin_name)))
            model->skins[i] = R_NOTEXTURE;
        else {
            Q_strlcpy(model->skin_names[i], skin_name,
                      sizeof(model->skin_names[i]));
            model->skins[i] = IMG_Find(skin_name, IT_SKIN, IF_NONE);
        }
        src_skin += MD2_MAX_SKINNAME;
    }

    if (!vk_upload_mesh(&model->mesh, vertices, numverts * header.num_frames,
                        indices, numindices)) {
        vk_free_model(model);
        goto out;
    }

    line_indices = vk_build_line_indices(indices, numindices, &line_index_count);
    if (!line_indices ||
        !vk_upload_buffer(&model->alias_line_indices, line_indices,
                          sizeof(*line_indices) * line_index_count,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        vk_free_model(model);
        goto out;
    }
    model->alias_line_index_count = line_index_count;

#if USE_MD5
    vk_load_md5_variant(model);
#endif

    handle = (model - vk.models) + 1;

out:
    if (vert_indices)
        Z_Free(vert_indices);
    if (tc_indices)
        Z_Free(tc_indices);
    if (final_indices)
        Z_Free(final_indices);
    if (remap)
        Z_Free(remap);
    if (vertices)
        Z_Free(vertices);
    if (indices)
        Z_Free(indices);
    if (line_indices)
        Z_Free(line_indices);
    if (alias_frames)
        Z_Free(alias_frames);
    return handle;
}

#if USE_MD3
static bool vk_check_md3_bounds(const dmd3header_t *header, size_t length)
{
    if (header->num_frames < 1 || header->num_frames > MD3_MAX_FRAMES ||
        header->num_meshes < 1 || header->num_meshes > MD3_MAX_MESHES)
        return false;
    if ((uint64_t)header->ofs_frames +
        header->num_frames * sizeof(dmd3frame_t) > length)
        return false;
    if (header->ofs_meshes > length)
        return false;

    return true;
}

static bool vk_check_md3_mesh_bounds(const dmd3mesh_t *mesh, uint32_t frame_count,
                                     size_t length)
{
    if (mesh->ident != MD3_IDENT)
        return false;
    if (mesh->meshsize < sizeof(*mesh) || mesh->meshsize > length)
        return false;
    if (mesh->num_frames != frame_count)
        return false;
    if (mesh->num_verts < 3 || mesh->num_verts > MD3_MAX_VERTS ||
        mesh->num_tris < 1 || mesh->num_tris > MD3_MAX_TRIANGLES ||
        mesh->num_skins > MD3_MAX_SKINS)
        return false;
    if ((uint64_t)mesh->ofs_indexes +
        mesh->num_tris * 3 * sizeof(uint32_t) > mesh->meshsize ||
        (uint64_t)mesh->ofs_skins +
        mesh->num_skins * sizeof(dmd3skin_t) > mesh->meshsize ||
        (uint64_t)mesh->ofs_tcs +
        mesh->num_verts * sizeof(dmd3coord_t) > mesh->meshsize ||
        (uint64_t)mesh->ofs_verts +
        mesh->num_verts * frame_count * sizeof(dmd3vertex_t) > mesh->meshsize)
        return false;

    return true;
}

static void vk_md3_normal(uint8_t lat_byte, uint8_t lng_byte, vec3_t normal)
{
    float lat = lat_byte * (2.0f * M_PIf / 255.0f);
    float lng = lng_byte * (2.0f * M_PIf / 255.0f);

    normal[0] = sinf(lat) * cosf(lng);
    normal[1] = sinf(lat) * sinf(lng);
    normal[2] = cosf(lat);
}

static qhandle_t vk_load_md3_model(const char *name, const byte *rawdata, size_t length)
{
    typedef struct {
        dmd3mesh_t header;
        const byte *data;
        uint32_t vertex_base;
        uint32_t first_index;
        int skin_start;
    } vk_md3_mesh_info_t;

    dmd3header_t header;
    vk_md3_mesh_info_t mesh_info[MD3_MAX_MESHES];
    vk_vertex_t *vertices = NULL;
    uint32_t *indices = NULL;
    uint32_t *line_indices = NULL;
    uint32_t line_index_count = 0;
    vk_alias_frame_t *alias_frames = NULL;
    vk_alias_batch_t *batches = NULL;
    image_t **skins = NULL;
    maliasskinname_t *skin_names = NULL;
    vk_model_t *model = NULL;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    int skin_count = 0;
    qhandle_t handle = 0;

    if (length < sizeof(header))
        return 0;

    const dmd3header_t *src_header = (const dmd3header_t *)rawdata;
    header.ident = LittleLong(src_header->ident);
    header.version = LittleLong(src_header->version);
    header.flags = LittleLong(src_header->flags);
    header.num_frames = LittleLong(src_header->num_frames);
    header.num_tags = LittleLong(src_header->num_tags);
    header.num_meshes = LittleLong(src_header->num_meshes);
    header.num_skins = LittleLong(src_header->num_skins);
    header.ofs_frames = LittleLong(src_header->ofs_frames);
    header.ofs_tags = LittleLong(src_header->ofs_tags);
    header.ofs_meshes = LittleLong(src_header->ofs_meshes);
    header.ofs_end = LittleLong(src_header->ofs_end);

    if (header.ident != MD3_IDENT || header.version != MD3_VERSION ||
        !vk_check_md3_bounds(&header, length))
        return 0;

    const byte *mesh_data = rawdata + header.ofs_meshes;
    size_t remaining = length - header.ofs_meshes;
    for (uint32_t i = 0; i < header.num_meshes; i++) {
        if (remaining < sizeof(dmd3mesh_t))
            goto out;

        const dmd3mesh_t *src_mesh = (const dmd3mesh_t *)mesh_data;
        dmd3mesh_t *mesh = &mesh_info[i].header;

        mesh->ident = LittleLong(src_mesh->ident);
        mesh->flags = LittleLong(src_mesh->flags);
        mesh->num_frames = LittleLong(src_mesh->num_frames);
        mesh->num_skins = LittleLong(src_mesh->num_skins);
        mesh->num_verts = LittleLong(src_mesh->num_verts);
        mesh->num_tris = LittleLong(src_mesh->num_tris);
        mesh->ofs_indexes = LittleLong(src_mesh->ofs_indexes);
        mesh->ofs_skins = LittleLong(src_mesh->ofs_skins);
        mesh->ofs_tcs = LittleLong(src_mesh->ofs_tcs);
        mesh->ofs_verts = LittleLong(src_mesh->ofs_verts);
        mesh->meshsize = LittleLong(src_mesh->meshsize);

        if (!vk_check_md3_mesh_bounds(mesh, header.num_frames, remaining))
            goto out;
        if (UINT32_MAX - vertex_count < mesh->num_verts ||
            UINT32_MAX - index_count < mesh->num_tris * 3)
            goto out;
        if (skin_count > INT_MAX - (int)mesh->num_skins)
            goto out;

        mesh_info[i].data = mesh_data;
        mesh_info[i].vertex_base = vertex_count;
        mesh_info[i].first_index = index_count;
        mesh_info[i].skin_start = skin_count;
        vertex_count += mesh->num_verts;
        index_count += mesh->num_tris * 3;
        skin_count += mesh->num_skins;

        mesh_data += mesh->meshsize;
        remaining -= mesh->meshsize;
    }

    if (!vertex_count || !index_count ||
        (uint64_t)vertex_count * header.num_frames > UINT32_MAX)
        goto out;

    vertices = Z_Malloc(sizeof(*vertices) * vertex_count * header.num_frames);
    indices = Z_Malloc(sizeof(*indices) * index_count);
    alias_frames = Z_Mallocz(sizeof(*alias_frames) * header.num_frames);
    batches = Z_Mallocz(sizeof(*batches) * header.num_meshes);
    if (skin_count) {
        skins = Z_Mallocz(sizeof(*skins) * skin_count);
        skin_names = Z_Mallocz(sizeof(*skin_names) * skin_count);
    }

    const dmd3frame_t *src_frame =
        (const dmd3frame_t *)(rawdata + header.ofs_frames);
    for (uint32_t frame = 0; frame < header.num_frames; frame++)
        ClearBounds(alias_frames[frame].bounds[0],
                    alias_frames[frame].bounds[1]);

    for (uint32_t mesh_index = 0; mesh_index < header.num_meshes; mesh_index++) {
        const vk_md3_mesh_info_t *info = &mesh_info[mesh_index];
        const dmd3mesh_t *mesh = &info->header;
        const dmd3coord_t *src_tc =
            (const dmd3coord_t *)(info->data + mesh->ofs_tcs);
        const uint32_t *src_index =
            (const uint32_t *)(info->data + mesh->ofs_indexes);
        const dmd3skin_t *src_skin =
            (const dmd3skin_t *)(info->data + mesh->ofs_skins);

        batches[mesh_index] = (vk_alias_batch_t) {
            .first_index = info->first_index,
            .index_count = mesh->num_tris * 3,
            .skin_start = info->skin_start,
            .skin_count = mesh->num_skins,
        };

        for (uint32_t skin = 0; skin < mesh->num_skins; skin++) {
            char skin_name[MD3_MAX_PATH];

            if (!Q_memccpy(skin_name, src_skin[skin].name, 0, sizeof(skin_name)))
                skins[info->skin_start + skin] = R_NOTEXTURE;
            else {
                Q_strlcpy(skin_names[info->skin_start + skin], skin_name,
                          sizeof(skin_names[info->skin_start + skin]));
                skins[info->skin_start + skin] =
                    IMG_Find(skin_name, IT_SKIN, IF_NONE);
            }
        }

        for (uint32_t i = 0; i < mesh->num_tris * 3; i++) {
            uint32_t index = LittleLong(src_index[i]);

            if (index >= mesh->num_verts)
                goto out;
            indices[info->first_index + i] = info->vertex_base + index;
        }

        for (uint32_t frame = 0; frame < header.num_frames; frame++) {
            const dmd3frame_t *frame_info =
                (const dmd3frame_t *)(rawdata + header.ofs_frames) + frame;
            const dmd3vertex_t *src_vert =
                (const dmd3vertex_t *)(info->data + mesh->ofs_verts) +
                frame * mesh->num_verts;
            vec3_t translate;

            LittleVector(frame_info->translate, translate);
            for (uint32_t vert = 0; vert < mesh->num_verts; vert++) {
                vk_vertex_t *dst =
                    &vertices[frame * vertex_count + info->vertex_base + vert];
                vec3_t local;

                local[0] =
                    (int16_t)LittleShort(src_vert[vert].point[0]) * MD3_XYZ_SCALE;
                local[1] =
                    (int16_t)LittleShort(src_vert[vert].point[1]) * MD3_XYZ_SCALE;
                local[2] =
                    (int16_t)LittleShort(src_vert[vert].point[2]) * MD3_XYZ_SCALE;
                VectorAdd(local, translate, dst->position);
                AddPointToBounds(local, alias_frames[frame].bounds[0],
                                 alias_frames[frame].bounds[1]);
                dst->color[0] = 1.0f;
                dst->color[1] = 1.0f;
                dst->color[2] = 1.0f;
                dst->color[3] = 1.0f;
                dst->uv[0] = LittleFloat(src_tc[vert].st[0]);
                dst->uv[1] = LittleFloat(src_tc[vert].st[1]);
                vk_md3_normal(src_vert[vert].norm[0], src_vert[vert].norm[1],
                              dst->normal);
            }
        }
    }

    // Match OpenGL by deriving bounds from decoded mesh vertices. MD3 frame
    // header bounds are exporter metadata and are not necessarily reliable.
    for (uint32_t frame = 0; frame < header.num_frames; frame++) {
        vec3_t translate;

        alias_frames[frame].radius =
            RadiusFromBounds(alias_frames[frame].bounds[0],
                             alias_frames[frame].bounds[1]);
        LittleVector(src_frame[frame].translate, translate);
        VectorAdd(alias_frames[frame].bounds[0], translate,
                  alias_frames[frame].bounds[0]);
        VectorAdd(alias_frames[frame].bounds[1], translate,
                  alias_frames[frame].bounds[1]);
    }

    model = vk_alloc_model();
    if (!model)
        goto out;

    Q_strlcpy(model->name, name, sizeof(model->name));
    model->type = VK_MODEL_ALIAS;
    model->registration_sequence = r_registration_sequence;
    model->frame_count = header.num_frames;
    model->alias_frames = alias_frames;
    alias_frames = NULL;
    model->alias_batch_count = header.num_meshes;
    model->alias_batches = batches;
    batches = NULL;
    model->vertex_count = vertex_count;
    model->skin_count = skin_count;
    model->skins = skins;
    skins = NULL;
    model->skin_names = skin_names;
    skin_names = NULL;

    if (!vk_upload_mesh(&model->mesh, vertices, vertex_count * header.num_frames,
                        indices, index_count)) {
        vk_free_model(model);
        goto out;
    }

    line_indices = vk_build_line_indices(indices, index_count, &line_index_count);
    if (!line_indices ||
        !vk_upload_buffer(&model->alias_line_indices, line_indices,
                          sizeof(*line_indices) * line_index_count,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        vk_free_model(model);
        goto out;
    }
    model->alias_line_index_count = line_index_count;

#if USE_MD5
    vk_load_md5_variant(model);
#endif

    handle = (model - vk.models) + 1;

out:
    if (vertices)
        Z_Free(vertices);
    if (indices)
        Z_Free(indices);
    if (line_indices)
        Z_Free(line_indices);
    if (alias_frames)
        Z_Free(alias_frames);
    if (batches)
        Z_Free(batches);
    if (skins)
        Z_Free(skins);
    if (skin_names)
        Z_Free(skin_names);

    return handle;
}
#endif

vk_model_t *vk_model_for_handle(qhandle_t handle)
{
    if (handle <= 0 || handle > vk.model_count)
        return NULL;

    vk_model_t *model = &vk.models[handle - 1];
    if (model->type == VK_MODEL_FREE)
        return NULL;

    return model;
}

qhandle_t VKR_RegisterModel(const char *name)
{
    char normalized[MAX_QPATH];
    size_t namelen;
    byte *rawdata;
    int ret;
    vk_model_t *model;
    qhandle_t handle;
    bool supported;

    if (!name || !*name)
        return 0;
    if (*name == '*')
        return ~Q_atoi(name + 1);

    namelen = FS_NormalizePathBuffer(normalized, name, sizeof(normalized));
    if (!namelen || namelen >= sizeof(normalized))
        return 0;

    model = vk_find_model(normalized);
    if (model) {
        model->registration_sequence = r_registration_sequence;
        for (int i = 0; i < model->frame_count; i++) {
            if (model->type == VK_MODEL_SPRITE && model->frames[i].image)
                model->frames[i].image->registration_sequence = r_registration_sequence;
        }
        for (int i = 0; i < model->skin_count; i++) {
            if (model->skins[i])
                model->skins[i]->registration_sequence = r_registration_sequence;
        }
#if USE_MD5
        for (int i = 0; i < model->md5_skin_count; i++) {
            if (model->md5_skins[i])
                model->md5_skins[i]->registration_sequence = r_registration_sequence;
        }
#endif
        return (model - vk.models) + 1;
    }

    ret = FS_LoadFile(normalized, (void **)&rawdata);
    if (!rawdata)
        return 0;

    handle = 0;
    supported = false;
    if (ret >= 4 && LittleLong(*(uint32_t *)rawdata) == SP2_IDENT) {
        supported = true;
        handle = vk_load_sprite_model(normalized, rawdata, ret);
    } else if (ret >= 4 && LittleLong(*(uint32_t *)rawdata) == MD2_IDENT) {
        supported = true;
        handle = vk_load_md2_model(normalized, rawdata, ret);
    }
#if USE_MD3
    else if (ret >= 4 && LittleLong(*(uint32_t *)rawdata) == MD3_IDENT) {
        supported = true;
        handle = vk_load_md3_model(normalized, rawdata, ret);
    }
#endif

    FS_FreeFile(rawdata);
    if (!handle && supported)
        Com_WPrintf("Couldn't load Vulkan model %s\n", normalized);
    return handle;
}

#endif // USE_VULKAN
