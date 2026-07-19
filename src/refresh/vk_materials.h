#pragma once

typedef struct {
    char texture_base[MAX_QPATH];
    float roughness;
    float specular;
} vk_material_params_t;

void VK_MaterialsLoad(const char *map);
void VK_MaterialsShutdown(void);
bool VK_MaterialForImage(const char *name, vk_material_params_t *params);
