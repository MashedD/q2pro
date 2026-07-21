/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/shared.h"
#include "common/common.h"
#include "common/files.h"
#include "common/zone.h"
#include "vk_materials.h"

typedef struct {
    char name[MAX_QPATH];
    vk_material_params_t params;
} vk_material_def_t;

static vk_material_def_t *vk_materials;
static uint32_t vk_material_count;
static uint32_t vk_material_capacity;

static char *vk_mat_trim(char *s)
{
    while (*s && *s <= ' ')
        s++;
    char *end = s + strlen(s);
    while (end > s && end[-1] <= ' ')
        *--end = '\0';
    return s;
}

static void vk_mat_normalize_name(char *name)
{
    FS_NormalizePath(name);
    char *extension = COM_FileExtension(name);
    if (*extension)
        *extension = '\0';
}

static vk_material_def_t *vk_mat_find(const char *name)
{
    for (uint32_t i = 0; i < vk_material_count; i++) {
        if (!Q_stricmp(vk_materials[i].name, name))
            return &vk_materials[i];
    }
    return NULL;
}

static vk_material_def_t *vk_mat_add(const char *name)
{
    char normalized[MAX_QPATH];
    Q_strlcpy(normalized, name, sizeof(normalized));
    vk_mat_normalize_name(normalized);
    if (!normalized[0])
        return NULL;

    vk_material_def_t *material = vk_mat_find(normalized);
    if (material)
        return material;

    if (vk_material_count == vk_material_capacity) {
        uint32_t capacity = vk_material_capacity ? vk_material_capacity * 2 : 256;
        vk_materials = Z_Realloc(vk_materials, sizeof(*vk_materials) * capacity);
        vk_material_capacity = capacity;
    }

    material = &vk_materials[vk_material_count++];
    memset(material, 0, sizeof(*material));
    Q_strlcpy(material->name, normalized, sizeof(material->name));
    material->params.roughness = 1.0f;
    material->params.emissive_factor = 1.0f;
    return material;
}

static void vk_mat_expand_star(char *out, size_t size, const char *value,
                               const char *material_name)
{
    const char *base = strrchr(material_name, '/');
    base = base ? base + 1 : material_name;
    const char *star = strchr(value, '*');
    if (!star) {
        Q_strlcpy(out, value, size);
        return;
    }

    size_t prefix = min((size_t)(star - value), size - 1);
    memcpy(out, value, prefix);
    out[prefix] = '\0';
    Q_strlcat(out, base, size);
    Q_strlcat(out, star + 1, size);
}

static void vk_mat_apply(vk_material_def_t **active, uint32_t active_count,
                         const char *key, const char *value,
                         const char *path, unsigned line)
{
    if (!Q_stricmp(key, "texture_base")) {
        for (uint32_t i = 0; i < active_count; i++)
            vk_mat_expand_star(active[i]->params.texture_base,
                               sizeof(active[i]->params.texture_base),
                               value, active[i]->name);
        return;
    }

    if (!Q_stricmp(key, "texture_emissive")) {
        for (uint32_t i = 0; i < active_count; i++)
            vk_mat_expand_star(active[i]->params.texture_emissive,
                               sizeof(active[i]->params.texture_emissive),
                               value, active[i]->name);
        return;
    }

    if (!Q_stricmp(key, "is_light")) {
        int enabled = Q_atoi(value);
        for (uint32_t i = 0; i < active_count; i++)
            active[i]->params.is_light = enabled != 0;
        return;
    }

    if (!Q_stricmp(key, "roughness_override") ||
        !Q_stricmp(key, "specular_scale") ||
        !Q_stricmp(key, "emissive_factor")) {
        char *end;
        float parsed = strtof(value, &end);
        if (end == value || *vk_mat_trim(end)) {
            Com_WPrintf("Ignoring invalid %s at %s:%u\n", key, path, line);
            return;
        }
        parsed = Q_clipf(parsed, 0.0f, 1.0f);
        for (uint32_t i = 0; i < active_count; i++) {
            if (!Q_stricmp(key, "roughness_override"))
                active[i]->params.roughness = parsed;
            else if (!Q_stricmp(key, "specular_scale"))
                active[i]->params.specular = parsed;
            else
                active[i]->params.emissive_factor = parsed;
        }
    }
}

static void vk_mat_parse_header(char *header, vk_material_def_t **active,
                                uint32_t *active_count)
{
    *active_count = 0;
    char *colon = strrchr(header, ':');
    if (!colon)
        return;
    *colon = '\0';

    for (char *name = strtok(header, ","); name; name = strtok(NULL, ",")) {
        vk_material_def_t *material = vk_mat_add(vk_mat_trim(name));
        if (material && *active_count < 64)
            active[(*active_count)++] = material;
    }
}

static void vk_mat_load_file(const char *path)
{
    char *data;
    int length = FS_LoadFile(path, (void **)&data);
    if (length < 0)
        return;

    vk_material_def_t *active[64];
    uint32_t active_count = 0;
    char header[MAX_STRING_CHARS] = "";
    char *cursor = data;
    unsigned line_number = 0;

    while (cursor && *cursor) {
        char *line = cursor;
        char *newline = strpbrk(cursor, "\r\n");
        if (newline) {
            char terminator = *newline;
            *newline = '\0';
            cursor = newline + 1;
            if (*cursor == '\n' && terminator == '\r')
                cursor++;
        } else {
            cursor = NULL;
        }
        line_number++;

        char *comment = strstr(line, "//");
        if (comment)
            *comment = '\0';
        bool indented = line[0] && line[0] <= ' ';
        char *text = vk_mat_trim(line);
        if (!*text)
            continue;

        if (!indented || header[0]) {
            if (header[0])
                Q_strlcat(header, " ", sizeof(header));
            Q_strlcat(header, text, sizeof(header));
            if (strchr(text, ':')) {
                vk_mat_parse_header(header, active, &active_count);
                header[0] = '\0';
            }
            continue;
        }

        char *value = text;
        while (*value > ' ')
            value++;
        if (!*value) {
            Com_WPrintf("Ignoring malformed material property at %s:%u\n",
                        path, line_number);
            continue;
        }
        *value++ = '\0';
        vk_mat_apply(active, active_count, text, vk_mat_trim(value),
                     path, line_number);
    }

    FS_FreeFile(data);
}

void VK_MaterialsShutdown(void)
{
    if (vk_materials)
        Z_Free(vk_materials);
    vk_materials = NULL;
    vk_material_count = 0;
    vk_material_capacity = 0;
}

void VK_MaterialsLoad(const char *map)
{
    VK_MaterialsShutdown();

    int count = 0;
    void **files = FS_ListFiles("materials", ".mat",
                                FS_SEARCH_RECURSIVE | FS_SEARCH_SAVEPATH,
                                &count);
    for (int i = 0; i < count; i++)
        vk_mat_load_file(files[i]);
    FS_FreeList(files);

    if (map && *map) {
        char base[MAX_QPATH];
        COM_SplitPath(map, base, sizeof(base), NULL, 0, true);
        vk_mat_load_file(va("%s.mat", base));
    }
}

bool VK_MaterialForImage(const char *name, vk_material_params_t *params)
{
    if (!name || !params)
        return false;

    char normalized[MAX_QPATH];
    Q_strlcpy(normalized, name, sizeof(normalized));
    vk_mat_normalize_name(normalized);
    vk_material_def_t *material = vk_mat_find(normalized);
    if (!material) {
        for (uint32_t i = 0; i < vk_material_count; i++) {
            char base[MAX_QPATH];
            Q_strlcpy(base, vk_materials[i].params.texture_base, sizeof(base));
            vk_mat_normalize_name(base);
            if (base[0] && !Q_stricmp(base, normalized)) {
                material = &vk_materials[i];
                break;
            }
        }
    }
    if (!material)
        return false;
    *params = material->params;
    return true;
}
