/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan ray tracing: device support, acceleration structures, reflection materials and the static lighting bake.

#include "vk_local.h"
#include "vk_spirv.h"

#if USE_VULKAN

#if USE_VULKAN_RAYTRACING
static void vk_rt_material_normalize(char *name)
{
    if (!Q_stricmpn(name, "textures/", 9))
        memmove(name, name + 9, strlen(name + 9) + 1);
    COM_StripExtension(name, name, MAX_TEXNAME);
}

const vk_rt_material_rule_t *vk_rt_material_rule(const char *name)
{
    char normalized[MAX_TEXNAME];
    if (!name)
        return NULL;
    Q_strlcpy(normalized, name, sizeof(normalized));
    vk_rt_material_normalize(normalized);
    for (uint32_t i = 0; i < vk.world.rt_material_rule_count; i++) {
        if (!Q_stricmp(vk.world.rt_material_rules[i].name, normalized))
            return &vk.world.rt_material_rules[i];
    }
    return NULL;
}

void vk_load_rt_materials(void)
{
    byte *data = NULL;
    int len = FS_LoadFile("materials/rt_reflections.mat", (void **)&data);
    vk.world.rt_material_rule_count = 0;
    vk.world.rt_material_file_loaded = len >= 0 && data != NULL;
    if (!vk.world.rt_material_file_loaded)
        return;

    char *cursor = (char *)data;
    char *line = cursor;
    vk_rt_material_rule_t *current = NULL;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *comment = strstr(line, "//");
        if (comment)
            *comment = 0;
        while (*line && isspace((unsigned char)*line))
            line++;
        char *end = line + strlen(line);
        while (end > line && isspace((unsigned char)end[-1]))
            *--end = 0;
        if (*line) {
            char key[32], value[64];
            if (line[strlen(line) - 1] == ':') {
                line[strlen(line) - 1] = 0;
                vk_rt_material_normalize(line);
                if (vk.world.rt_material_rule_count < VK_RT_MATERIAL_RULES) {
                    current = &vk.world.rt_material_rules[vk.world.rt_material_rule_count++];
                    memset(current, 0, sizeof(*current));
                    Q_strlcpy(current->name, line, sizeof(current->name));
                    current->reflect = 1.0f;
                    current->roughness = 0.5f;
                } else {
                    current = NULL;
                }
            } else if (current && sscanf(line, "%31s %63s", key, value) == 2) {
                float v = strtof(value, NULL);
                if (!Q_stricmp(key, "reflect"))
                    current->reflect = min(max(v, 0.0f), 1.0f);
                else if (!Q_stricmp(key, "roughness"))
                    current->roughness = min(max(v, 0.0f), 1.0f);
                else
                    Com_DPrintf("Unknown RT material key '%s'\n", key);
            }
        }
        line = next;
    }
    FS_FreeFile(data);
    Com_Printf("Vulkan RT materials: %u reflection rules loaded\n",
               vk.world.rt_material_rule_count);
}
#endif

#if USE_VULKAN_RAYTRACING
static VkDeviceAddress vk_buffer_device_address(VkBuffer buffer)
{
    VkBufferDeviceAddressInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer,
    };
    return vk.GetBufferDeviceAddress(vk.device, &info);
}

void vk_rt_prefix_error(const char *stage)
{
    char detail[MAX_STRING_CHARS];
    Q_strlcpy(detail, Com_GetLastError(), sizeof(detail));
    Com_SetLastError(va("%s: %s", stage,
                        detail[0] ? detail : "unspecified Vulkan failure"));
}

void vk_destroy_acceleration_structure(vk_acceleration_structure_t *as)
{
    if (as->handle && vk.DestroyAccelerationStructureKHR)
        vk.DestroyAccelerationStructureKHR(vk.device, as->handle, NULL);
    as->handle = VK_NULL_HANDLE;
    vk_destroy_buffer(&as->storage);
}

bool vk_build_mesh_blas(vk_acceleration_structure_t *as,
                               const vk_buffer_t *vertices,
                               uint32_t vertex_count,
                               const vk_buffer_t *indices,
                               uint32_t index_count)
{
    if (!vk.raytracing_active || !vertices || !vertices->buffer ||
        !indices || !indices->buffer || vertex_count < 3 || index_count < 3)
        return true;

    VkAccelerationStructureGeometryKHR geometry = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
        .geometry.triangles = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData.deviceAddress = vk_buffer_device_address(vertices->buffer),
            .vertexStride = sizeof(vk_vertex_t),
            .maxVertex = vertex_count - 1,
            .indexType = VK_INDEX_TYPE_UINT32,
            .indexData.deviceAddress = vk_buffer_device_address(indices->buffer),
        },
    };
    if (!geometry.geometry.triangles.vertexData.deviceAddress ||
        !geometry.geometry.triangles.indexData.deviceAddress) {
        Com_SetLastError("BLAS vertex or index buffer has no device address");
        return false;
    }
    uint32_t primitive_count = index_count / 3;
    VkAccelerationStructureBuildGeometryInfoKHR build = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry,
    };
    VkAccelerationStructureBuildSizesInfoKHR sizes = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    vk.GetAccelerationStructureBuildSizesKHR(
        vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &build, &primitive_count, &sizes);

    vk_acceleration_structure_t created = { 0 };
    if (!sizes.accelerationStructureSize || !sizes.buildScratchSize) {
        Com_SetLastError("Vulkan returned empty BLAS build sizes");
        goto fail;
    }
    if (!vk_create_buffer(sizes.accelerationStructureSize,
                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &created.storage.buffer, &created.storage.memory))
        goto fail;
    created.storage.size = sizes.accelerationStructureSize;

    VkAccelerationStructureCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = created.storage.buffer,
        .size = sizes.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
    };
    VkResult result = vk.CreateAccelerationStructureKHR(
        vk.device, &create_info, NULL, &created.handle);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkCreateAccelerationStructureKHR", result);
        goto fail;
    }

    vk_buffer_t scratch = { 0 };
    if (!vk_create_buffer(sizes.buildScratchSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &scratch.buffer, &scratch.memory))
        goto fail;
    scratch.size = sizes.buildScratchSize;
    build.dstAccelerationStructure = created.handle;
    build.scratchData.deviceAddress = vk_buffer_device_address(scratch.buffer);
    if (!build.scratchData.deviceAddress) {
        Com_SetLastError("BLAS scratch buffer has no device address");
        vk_destroy_buffer(&scratch);
        goto fail;
    }
    VkAccelerationStructureBuildRangeInfoKHR range = {
        .primitiveCount = primitive_count,
    };
    const VkAccelerationStructureBuildRangeInfoKHR *ranges[] = { &range };
    VkCommandBuffer cmd;
    if (!vk_begin_immediate(&cmd)) {
        vk_destroy_buffer(&scratch);
        goto fail;
    }
    vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &build, ranges);
    if (!vk_end_immediate(cmd)) {
        vk_destroy_buffer(&scratch);
        goto fail;
    }
    vk_destroy_buffer(&scratch);

    vk_destroy_acceleration_structure(as);
    *as = created;
    return true;

fail:
    vk_destroy_acceleration_structure(&created);
    return false;
}

bool vk_build_world_tlas(void)
{
    if (!vk.raytracing_active || !vk.world.rt_blas.handle)
        return true;

    VkAccelerationStructureDeviceAddressInfoKHR address_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = vk.world.rt_blas.handle,
    };
    VkDeviceAddress blas_address =
        vk.GetAccelerationStructureDeviceAddressKHR(vk.device, &address_info);
    if (!blas_address) {
        Com_SetLastError("World BLAS has no device address");
        return false;
    }

    VkAccelerationStructureInstanceKHR instance = {
        .transform = { .matrix = {
            { 1.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f, 0.0f },
        } },
        .instanceCustomIndex = 0,
        .mask = 0xff,
        .instanceShaderBindingTableRecordOffset = 0,
        .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
        .accelerationStructureReference = blas_address,
    };
    VkBufferUsageFlags instance_usage =
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!vk_upload_buffer(&vk.rt_instance_buffer, &instance, sizeof(instance),
                          instance_usage))
        return false;

    VkAccelerationStructureGeometryKHR geometry = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry.instances = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .arrayOfPointers = VK_FALSE,
            .data.deviceAddress =
                vk_buffer_device_address(vk.rt_instance_buffer.buffer),
        },
    };
    uint32_t primitive_count = 1;
    VkAccelerationStructureBuildGeometryInfoKHR build = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry,
    };
    VkAccelerationStructureBuildSizesInfoKHR sizes = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    vk.GetAccelerationStructureBuildSizesKHR(
        vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &build, &primitive_count, &sizes);

    vk_acceleration_structure_t created = { 0 };
    vk_buffer_t scratch = { 0 };
    if (!sizes.accelerationStructureSize || !sizes.buildScratchSize) {
        Com_SetLastError("Vulkan returned empty TLAS build sizes");
        goto fail;
    }
    if (!vk_create_buffer(sizes.accelerationStructureSize,
                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &created.storage.buffer, &created.storage.memory))
        goto fail;
    created.storage.size = sizes.accelerationStructureSize;
    VkAccelerationStructureCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = created.storage.buffer,
        .size = sizes.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
    };
    VkResult result = vk.CreateAccelerationStructureKHR(
        vk.device, &create_info, NULL, &created.handle);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkCreateAccelerationStructureKHR(TLAS)", result);
        goto fail;
    }
    if (!vk_create_buffer(sizes.buildScratchSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &scratch.buffer, &scratch.memory))
        goto fail;
    scratch.size = sizes.buildScratchSize;
    build.dstAccelerationStructure = created.handle;
    build.scratchData.deviceAddress = vk_buffer_device_address(scratch.buffer);
    if (!build.scratchData.deviceAddress) {
        Com_SetLastError("TLAS scratch buffer has no device address");
        goto fail;
    }
    VkAccelerationStructureBuildRangeInfoKHR range = {
        .primitiveCount = primitive_count,
    };
    const VkAccelerationStructureBuildRangeInfoKHR *ranges[] = { &range };
    VkCommandBuffer cmd;
    if (!vk_begin_immediate(&cmd))
        goto fail;
    vk.CmdBuildAccelerationStructuresKHR(cmd, 1, &build, ranges);
    if (!vk_end_immediate(cmd))
        goto fail;
    vk_destroy_buffer(&scratch);

    vk_destroy_acceleration_structure(&vk.rt_tlas);
    vk.rt_tlas = created;
    VkWriteDescriptorSetAccelerationStructureKHR as_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &vk.rt_tlas.handle,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = &as_write,
        .dstSet = vk.rt_descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
    };
    vk.UpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    return true;

fail:
    vk_destroy_buffer(&scratch);
    vk_destroy_acceleration_structure(&created);
    return false;
}
#endif

#if USE_VULKAN_RAYTRACING
typedef struct {
    uint32_t ao_samples;
    uint32_t emissive_samples;
    uint32_t atlas_width;
    uint32_t seed;
} vk_rt_ao_push_t;

static bool vk_create_rt_ao_pipeline(void)
{
    if (vk.rt_ao_pipeline && vk.rt_ao_render_pass &&
        vk.rt_ao_pipeline_layout)
        return true;
    if (vk.rt_ao_pipeline) {
        vk.DestroyPipeline(vk.device, vk.rt_ao_pipeline, NULL);
        vk.rt_ao_pipeline = VK_NULL_HANDLE;
    }
    if (vk.rt_ao_pipeline_layout) {
        vk.DestroyPipelineLayout(vk.device, vk.rt_ao_pipeline_layout, NULL);
        vk.rt_ao_pipeline_layout = VK_NULL_HANDLE;
    }
    if (vk.rt_ao_render_pass) {
        vk.DestroyRenderPass(vk.device, vk.rt_ao_render_pass, NULL);
        vk.rt_ao_render_pass = VK_NULL_HANDLE;
    }

    VkAttachmentDescription attachment = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
    };
    VkSubpassDependency dependencies[] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        },
        {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        },
    };
    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = q_countof(dependencies),
        .pDependencies = dependencies,
    };
    VkResult result = vk.CreateRenderPass(vk.device, &render_pass_info, NULL,
                                          &vk.rt_ao_render_pass);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateRenderPass(rt_ao)", result);

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .size = sizeof(vk_rt_ao_push_t),
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &vk.rt_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    result = vk.CreatePipelineLayout(vk.device, &layout_info, NULL,
                                     &vk.rt_ao_pipeline_layout);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreatePipelineLayout(rt_ao)", result);

    VkShaderModule vert = vk_create_shader_module(vk_rt_ao_bake_vert_spv,
                                                  sizeof(vk_rt_ao_bake_vert_spv));
    VkShaderModule frag = vk_create_shader_module(vk_rt_ao_bake_frag_spv,
                                                  sizeof(vk_rt_ao_bake_frag_spv));
    if (!vert || !frag) {
        if (vert)
            vk.DestroyShaderModule(vk.device, vert, NULL);
        if (frag)
            vk.DestroyShaderModule(vk.device, frag, NULL);
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main",
        },
    };
    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(vk_rt_bake_vertex_t),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attributes[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_rt_bake_vertex_t, position),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_rt_bake_vertex_t, normal),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_rt_bake_vertex_t, uv),
        },
        {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32G32_UINT,
            .offset = offsetof(vk_rt_bake_vertex_t, rt_data),
        },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = q_countof(attributes),
        .pVertexAttributeDescriptions = attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                          VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = q_countof(dynamic_states),
        .pDynamicStates = dynamic_states,
    };
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .pDynamicState = &dynamic,
        .layout = vk.rt_ao_pipeline_layout,
        .renderPass = vk.rt_ao_render_pass,
    };
    result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                        &pipeline_info, NULL,
                                        &vk.rt_ao_pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines(rt_ao)", result);
    return true;
}

bool vk_world_face_is_inline(const bsp_t *bsp, const mface_t *face)
{
    for (int i = 1; i < bsp->nummodels; i++) {
        const mmodel_t *model = &bsp->models[i];
        if (face >= model->firstface && face < model->firstface + model->numfaces)
            return true;
    }
    return false;
}

void vk_update_rt_ao_descriptor(void)
{
    VkImageView view = vk.world.rt_ao_ready ? vk.world.rt_ao_texture.view :
        vk.world.pixel_lightmap_texture.view;
    if (!vk.rt_descriptor_set || !view || !vk.postprocess_sampler)
        return;

    VkDescriptorImageInfo image_info = {
        .sampler = vk.postprocess_sampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = vk.rt_descriptor_set,
        .dstBinding = 2,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
    };
    vk.UpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
}

static void vk_rt_static_image_barrier(VkCommandBuffer cmd, VkImage image,
                                       VkImageLayout old_layout,
                                       VkImageLayout new_layout,
                                       VkAccessFlags src_access,
                                       VkAccessFlags dst_access,
                                       VkPipelineStageFlags src_stage,
                                       VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vk.CmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL,
                          1, &barrier);
}

static bool vk_read_rt_static_stats(uint32_t width, uint32_t height)
{
    VkDeviceSize size = (VkDeviceSize)width * height * 4;
    vk_buffer_t readback = { 0 };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    void *mapped = NULL;
    bool ok = false;

    if (!size || !vk.world.rt_ao_texture.image ||
        !vk_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &readback.buffer, &readback.memory))
        return false;
    readback.size = size;

    if (!vk_begin_immediate(&cmd))
        goto out;
    vk_rt_static_image_barrier(cmd, vk.world.rt_ao_texture.image,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy = {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .imageExtent = { width, height, 1 },
    };
    vk.CmdCopyImageToBuffer(cmd, vk.world.rt_ao_texture.image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            readback.buffer, 1, &copy);
    vk_rt_static_image_barrier(cmd, vk.world.rt_ao_texture.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    if (!vk_end_immediate(cmd)) {
        cmd = VK_NULL_HANDLE;
        goto out;
    }
    cmd = VK_NULL_HANDLE;

    VkResult result = vk.MapMemory(vk.device, readback.memory, 0, size, 0,
                                   &mapped);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkMapMemory(RT static readback)", result);
        goto out;
    }

    const byte *pixels = mapped;
    uint64_t pixel_count = (uint64_t)width * height;
    uint32_t coverage = 0, occluded = 0, emissive = 0;
    byte ao_peak = 0, emissive_peak = 0;
    for (uint64_t i = 0; i < pixel_count; i++, pixels += 4) {
        if (!pixels[3])
            continue;
        coverage++;
        // Alpha zero is reserved for uncovered atlas space. Valid AO uses
        // 1..255 and is decoded back to 0..1 in the world shader.
        byte ao = pixels[3] - 1;
        if (ao) {
            occluded++;
            ao_peak = max(ao_peak, ao);
        }
        byte peak = max(pixels[0], max(pixels[1], pixels[2]));
        if (peak) {
            emissive++;
            emissive_peak = max(emissive_peak, peak);
        }
    }
    vk.world.rt_coverage_texels = coverage;
    vk.world.rt_occluded_texels = occluded;
    vk.world.rt_emissive_texels = emissive;
    vk.world.rt_ao_peak = ao_peak;
    vk.world.rt_emissive_peak = emissive_peak;
    ok = true;

out:
    if (mapped)
        vk.UnmapMemory(vk.device, readback.memory);
    if (cmd)
        vk.FreeCommandBuffers(vk.device, vk.command_pool, 1, &cmd);
    vk_destroy_buffer(&readback);
    return ok;
}

bool vk_bake_world_rt_ao(const bsp_t *bsp,
                                const vk_world_face_t *faces,
                                uint32_t face_count,
                                const vk_vertex_t *vertices,
                                const uint32_t *indices,
                                const float *lmuv_data)
{
    vk.world.rt_ao_ready = false;
    vk.world.rt_ao_samples = 0;
    vk.world.rt_emissive_samples = 0;
    vk.world.rt_bake_msec = 0;
    vk.world.rt_coverage_texels = 0;
    vk.world.rt_occluded_texels = 0;
    vk.world.rt_emissive_texels = 0;
    vk.world.rt_ao_peak = 0;
    vk.world.rt_emissive_peak = 0;
    if (!vk.raytracing_active || !vk.rt_tlas.handle || !bsp || !faces ||
        !face_count || !vertices || !indices || !lmuv_data ||
        !vk.world.pixel_lightmap_texture.width ||
        !vk.world.pixel_lightmap_texture.height) {
        Com_SetLastError("Pixel-lightmap data is unavailable for RT static lighting");
        return false;
    }
    uint64_t bake_start = vk_time_usec();

    VkFormatProperties properties;
    vk.GetPhysicalDeviceFormatProperties(vk.physical_device,
                                         VK_FORMAT_R8G8B8A8_UNORM, &properties);
    VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((properties.optimalTilingFeatures & required) != required) {
        Com_SetLastError("RGBA8 RT lighting atlas is not supported as a sampled color target");
        return false;
    }

    uint32_t atlas_width = min(vk.world.pixel_lightmap_texture.width, 2048u);
    uint32_t atlas_height = min(vk.world.pixel_lightmap_texture.height, 2048u);
    uint32_t bake_vertex_count = 0;
    uint32_t bake_index_count = 0;
    uint64_t valid_texels = 0;
    uint64_t lit_texels = 0;
    double area_scale = ((double)atlas_width /
                         vk.world.pixel_lightmap_texture.width) *
                        ((double)atlas_height /
                         vk.world.pixel_lightmap_texture.height);
    for (uint32_t i = 0; i < face_count; i++) {
        const vk_world_face_t *face = &faces[i];
        if (!face->face || !face->pixel_lm_w || !face->pixel_lm_h ||
            (face->face->drawflags & SURF_TRANS_MASK) ||
            vk_world_face_is_inline(bsp, face->face))
            continue;
        bake_vertex_count += face->edge_count;
        bake_index_count += face->index_count;
        valid_texels += max((uint64_t)((double)face->pixel_lm_w *
                                      face->pixel_lm_h * area_scale), 1u);
        if (face->rt_light_count)
            lit_texels += max((uint64_t)((double)face->pixel_lm_w *
                                        face->pixel_lm_h * area_scale), 1u);
    }
    if (!bake_vertex_count || !bake_index_count || !valid_texels) {
        Com_SetLastError("No lightmapped opaque world triangles for RT static lighting");
        return false;
    }

    uint32_t ao_samples = (uint32_t)(4194304ull / valid_texels);
    ao_samples = Q_clip(ao_samples, 1u, 4u);
    uint32_t emissive_samples = lit_texels ?
        Q_clip((uint32_t)(4194304ull / lit_texels), 1u, 2u) : 0u;
    vk_rt_bake_vertex_t *bake_vertices =
        Z_Mallocz((size_t)bake_vertex_count * sizeof(*bake_vertices));
    uint32_t *bake_indices =
        Z_Malloc((size_t)bake_index_count * sizeof(*bake_indices));
    uint32_t dst_vertex = 0;
    uint32_t dst_index = 0;

    for (uint32_t i = 0; i < face_count; i++) {
        const vk_world_face_t *face = &faces[i];
        if (!face->face || !face->pixel_lm_w || !face->pixel_lm_h ||
            (face->face->drawflags & SURF_TRANS_MASK) ||
            vk_world_face_is_inline(bsp, face->face))
            continue;

        vec3_t normal;
        VectorCopy(face->face->plane->normal, normal);
        if (face->face->drawflags & DSURF_PLANEBACK)
            VectorNegate(normal, normal);
        VectorNormalize(normal);
        float center_uv[2] = { 0.0f, 0.0f };
        for (uint32_t j = 0; j < face->edge_count; j++) {
            uint32_t source = face->first_vertex + j;
            center_uv[0] += lmuv_data[source * 2 + 0];
            center_uv[1] += lmuv_data[source * 2 + 1];
        }
        center_uv[0] /= face->edge_count;
        center_uv[1] /= face->edge_count;
        float min_radius = 1.0e30f;
        for (uint32_t j = 0; j < face->edge_count; j++) {
            uint32_t source = face->first_vertex + j;
            float dx = (lmuv_data[source * 2 + 0] - center_uv[0]) * atlas_width;
            float dy = (lmuv_data[source * 2 + 1] - center_uv[1]) * atlas_height;
            min_radius = min(min_radius, sqrtf(dx * dx + dy * dy));
        }
        float expansion = 1.0f + 1.0f / max(min_radius, 4.0f);
        expansion = min(expansion, 1.25f);
        uint32_t base_vertex = dst_vertex;
        for (uint32_t j = 0; j < face->edge_count; j++) {
            uint32_t source = face->first_vertex + j;
            vk_rt_bake_vertex_t *dst = &bake_vertices[dst_vertex++];
            for (int axis = 0; axis < 3; axis++) {
                dst->position[axis] = face->center[axis] +
                    (vertices[source].position[axis] - face->center[axis]) *
                    expansion;
                dst->normal[axis] = normal[axis];
            }
            dst->uv[0] = center_uv[0] +
                (lmuv_data[source * 2 + 0] - center_uv[0]) * expansion;
            dst->uv[1] = center_uv[1] +
                (lmuv_data[source * 2 + 1] - center_uv[1]) * expansion;
            dst->rt_data[0] = face->rt_light_offset;
            dst->rt_data[1] = face->rt_light_count;
        }
        for (uint32_t j = 0; j < face->index_count; j++) {
            uint32_t source = indices[face->first_index + j];
            if (source < face->first_vertex ||
                source >= face->first_vertex + face->edge_count)
                continue;
            bake_indices[dst_index++] = base_vertex +
                source - face->first_vertex;
        }
    }

    vk_mesh_t bake_mesh = { 0 };
    bool ok = dst_vertex && dst_index &&
        vk_upload_buffer(&bake_mesh.vertices, bake_vertices,
                         (VkDeviceSize)dst_vertex * sizeof(*bake_vertices),
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
        vk_upload_buffer(&bake_mesh.indices, bake_indices,
                         (VkDeviceSize)dst_index * sizeof(*bake_indices),
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    bake_mesh.vertex_count = ok ? dst_vertex : 0;
    bake_mesh.index_count = ok ? dst_index : 0;
    Z_Free(bake_indices);
    Z_Free(bake_vertices);
    if (!ok)
        goto fail;

    if (!vk_create_rt_ao_pipeline() ||
        !vk_create_color_target(&vk.world.rt_ao_texture, atlas_width,
                                atlas_height, VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        goto fail;

    VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = vk.rt_ao_render_pass,
        .attachmentCount = 1,
        .pAttachments = &vk.world.rt_ao_texture.view,
        .width = atlas_width,
        .height = atlas_height,
        .layers = 1,
    };
    VkResult result = vk.CreateFramebuffer(vk.device, &framebuffer_info, NULL,
                                           &vk.world.rt_ao_framebuffer);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkCreateFramebuffer(rt_ao)", result);
        goto fail;
    }

    VkCommandBuffer cmd;
    if (!vk_begin_immediate(&cmd))
        goto fail;
    VkClearValue clear = {
        .color = { .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } }
    };
    VkRenderPassBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk.rt_ao_render_pass,
        .framebuffer = vk.world.rt_ao_framebuffer,
        .renderArea.extent = { atlas_width, atlas_height },
        .clearValueCount = 1,
        .pClearValues = &clear,
    };
    VkViewport viewport = {
        .width = atlas_width,
        .height = atlas_height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = { .extent = { atlas_width, atlas_height } };
    VkDeviceSize offset = 0;
    vk.CmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vk.CmdSetViewport(cmd, 0, 1, &viewport);
    vk.CmdSetScissor(cmd, 0, 1, &scissor);
    vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.rt_ao_pipeline);
    vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             vk.rt_ao_pipeline_layout, 0, 1,
                             &vk.rt_descriptor_set, 0, NULL);
    vk.CmdBindVertexBuffers(cmd, 0, 1, &bake_mesh.vertices.buffer, &offset);
    vk.CmdBindIndexBuffer(cmd, bake_mesh.indices.buffer, 0,
                          VK_INDEX_TYPE_UINT32);
    vk_rt_ao_push_t push = {
        .ao_samples = ao_samples,
        .emissive_samples = emissive_samples,
        .atlas_width = atlas_width,
        .seed = 0x51f15e5du,
    };
    vk.CmdPushConstants(cmd, vk.rt_ao_pipeline_layout,
                        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, dst_index, 1, 0, 0, 0);
    vk.CmdEndRenderPass(cmd);
    if (!vk_end_immediate(cmd))
        goto fail;

    if (!vk_read_rt_static_stats(atlas_width, atlas_height))
        goto fail;
    if (!vk.world.rt_coverage_texels) {
        Com_SetLastError("RT static atlas contains no rasterized texels");
        goto fail;
    }

    vk_destroy_mesh(&bake_mesh);
    vk.world.rt_ao_samples = ao_samples;
    vk.world.rt_emissive_samples = emissive_samples;
    vk.world.rt_bake_msec = (uint32_t)((vk_time_usec() - bake_start + 500) /
                                       1000);
    vk.world.rt_ao_ready = true;
    vk_update_rt_ao_descriptor();
    double ray_count = ((double)valid_texels * ao_samples +
                        (double)lit_texels * emissive_samples) / 1000000.0;
    Com_Printf("Vulkan RT static lighting: baked %ux%u RGBA atlas, "
               "AO %u + emissive %u samples/texel, %u sources, "
               "%u triangles, %.2fM rays, %.1f MiB, %u ms; "
               "coverage %u, AO %u peak %u, emissive %u peak %u\n",
               atlas_width, atlas_height, ao_samples, emissive_samples,
               vk.world.surface_light_count, dst_index / 3, ray_count,
               (double)atlas_width * atlas_height * 4.0 / (1024.0 * 1024.0),
               vk.world.rt_bake_msec, vk.world.rt_coverage_texels,
               vk.world.rt_occluded_texels, vk.world.rt_ao_peak,
               vk.world.rt_emissive_texels, vk.world.rt_emissive_peak);
    return true;

fail:
    vk_destroy_mesh(&bake_mesh);
    if (vk.world.rt_ao_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.world.rt_ao_framebuffer, NULL);
        vk.world.rt_ao_framebuffer = VK_NULL_HANDLE;
    }
    vk_destroy_texture_resource(&vk.world.rt_ao_texture);
    vk.world.rt_ao_samples = 0;
    vk.world.rt_emissive_samples = 0;
    vk.world.rt_bake_msec = 0;
    vk.world.rt_coverage_texels = 0;
    vk.world.rt_occluded_texels = 0;
    vk.world.rt_emissive_texels = 0;
    vk.world.rt_ao_peak = 0;
    vk.world.rt_emissive_peak = 0;
    vk.world.rt_ao_ready = false;
    vk_update_rt_ao_descriptor();
    return false;
}
#endif

#if USE_VULKAN_RAYTRACING
static bool vk_device_has_extensions(const char *const *required,
                                     uint32_t required_count)
{
    uint32_t count = 0;
    VkResult result = vk.EnumerateDeviceExtensionProperties(
        vk.physical_device, NULL, &count, NULL);
    if (result != VK_SUCCESS || !count)
        return false;

    VkExtensionProperties *available = Z_Malloc(sizeof(*available) * count);
    result = vk.EnumerateDeviceExtensionProperties(
        vk.physical_device, NULL, &count, available);
    if (result != VK_SUCCESS) {
        Z_Free(available);
        return false;
    }

    bool found_all = true;
    for (uint32_t i = 0; i < required_count; i++) {
        bool found = false;
        for (uint32_t j = 0; j < count; j++) {
            if (!strcmp(required[i], available[j].extensionName)) {
                found = true;
                break;
            }
        }
        if (!found) {
            found_all = false;
            break;
        }
    }

    Z_Free(available);
    return found_all;
}

void vk_probe_raytracing(void)
{
    vk.raytracing_supported = false;
    vk.raytracing_active = false;
    vk.raytracing_quad_sharing = false;
    vk.raytracing_reason[0] = '\0';

    if (vk.instance_api_version < VK_API_VERSION_1_2 ||
        !vk.GetPhysicalDeviceFeatures2 || !vk.GetPhysicalDeviceProperties2) {
        Q_strlcpy(vk.raytracing_reason, "Vulkan 1.2 is unavailable",
                  sizeof(vk.raytracing_reason));
        return;
    }

    const char *required[] = {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    };
    if (!vk_device_has_extensions(required, q_countof(required))) {
        Q_strlcpy(vk.raytracing_reason, "required device extensions are missing",
                  sizeof(vk.raytracing_reason));
        return;
    }

    VkPhysicalDeviceRayQueryFeaturesKHR ray_query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
    };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &ray_query,
    };
    VkPhysicalDeviceBufferDeviceAddressFeatures buffer_address = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .pNext = &acceleration,
    };
    VkPhysicalDeviceDescriptorIndexingFeatures descriptors = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .pNext = &buffer_address,
    };
    VkPhysicalDeviceFeatures2 features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &descriptors,
    };
    vk.GetPhysicalDeviceFeatures2(vk.physical_device, &features);

    VkPhysicalDeviceDescriptorIndexingProperties descriptor_props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES,
    };
    VkPhysicalDeviceSubgroupProperties subgroup_props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
    };
    descriptor_props.pNext = &subgroup_props;
    VkPhysicalDeviceProperties2 properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &descriptor_props,
    };
    vk.GetPhysicalDeviceProperties2(vk.physical_device, &properties);

    vk.raytracing_quad_sharing =
        (subgroup_props.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) &&
        (subgroup_props.supportedOperations & VK_SUBGROUP_FEATURE_BASIC_BIT) &&
        (subgroup_props.supportedOperations & VK_SUBGROUP_FEATURE_QUAD_BIT);

    if (!ray_query.rayQuery || !acceleration.accelerationStructure ||
        !buffer_address.bufferDeviceAddress ||
        !descriptors.runtimeDescriptorArray ||
        !descriptors.shaderSampledImageArrayNonUniformIndexing ||
        !descriptors.descriptorBindingPartiallyBound ||
        !descriptors.descriptorBindingSampledImageUpdateAfterBind ||
        descriptor_props.maxDescriptorSetUpdateAfterBindSampledImages < MAX_RIMAGES) {
        Q_strlcpy(vk.raytracing_reason, "required ray-query features or limits are missing",
                  sizeof(vk.raytracing_reason));
        return;
    }

    vk.raytracing_supported = true;
    if (vk_raytracing && vk_raytracing->integer)
        vk.raytracing_active = true;
    else
        Q_strlcpy(vk.raytracing_reason, "disabled by vk_raytracing",
                  sizeof(vk.raytracing_reason));
}
#endif

#endif // USE_VULKAN
