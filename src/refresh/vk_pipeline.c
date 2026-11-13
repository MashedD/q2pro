/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan render pass, shader modules and graphics pipelines.

#include "vk_local.h"
#include "vk_spirv.h"

#if USE_VULKAN

bool vk_create_render_pass(void)
{
    bool multisampled = vk.sample_count != VK_SAMPLE_COUNT_1_BIT;
    uint32_t color_count = vk.mrt_bloom ? 2 : 1;
    VkAttachmentDescription attachments[5] = {
        {
            .format = vk.swapchain_format,
            .samples = vk.sample_count,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE :
                                      VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = multisampled ? VK_IMAGE_LAYOUT_UNDEFINED :
                                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = vk.swapchain_format,
            .samples = vk.sample_count,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE :
                                      VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = multisampled ? VK_IMAGE_LAYOUT_UNDEFINED :
                                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = vk.depth_format,
            .samples = vk.sample_count,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = vk_shadow_stencil_enabled() ?
                VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
        {
            .format = vk.swapchain_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = vk.swapchain_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
    };

    VkAttachmentReference color_refs[2] = {
        { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { .attachment = 1, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
    };
    VkAttachmentReference depth_ref = {
        .attachment = vk.mrt_bloom ? 2 : 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference resolve_refs[2] = {
        { .attachment = vk.mrt_bloom ? 3 : 2,
          .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { .attachment = 4, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = color_count,
        .pColorAttachments = color_refs,
        .pResolveAttachments = multisampled ? resolve_refs : NULL,
        .pDepthStencilAttachment = &depth_ref,
    };

    uint32_t attachment_count = vk.mrt_bloom ?
        (multisampled ? 5 : 3) : (multisampled ? 3 : 2);
    if (!vk.mrt_bloom) {
        attachments[1] = attachments[2];
        if (multisampled)
            attachments[2] = attachments[3];
    }

    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = attachment_count,
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };

    VkResult result = vk.CreateRenderPass(vk.device, &create_info, NULL, &vk.render_pass);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateRenderPass", result);

    VkAttachmentDescription bloom_attachment = {
        .format = vk.swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference bloom_color = {
        .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription bloom_subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &bloom_color,
    };
    VkRenderPassCreateInfo bloom_info = create_info;
    bloom_info.attachmentCount = 1;
    bloom_info.pAttachments = &bloom_attachment;
    bloom_info.pSubpasses = &bloom_subpass;
    result = vk.CreateRenderPass(vk.device, &bloom_info, NULL, &vk.bloom_render_pass);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateRenderPass", result);

    return true;
}

VkShaderModule vk_create_shader_module(const uint32_t *code, size_t code_size)
{
    VkShaderModule module;
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size,
        .pCode = code,
    };

    VkResult result = vk.CreateShaderModule(vk.device, &create_info, NULL, &module);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkCreateShaderModule", result);
        return VK_NULL_HANDLE;
    }

    return module;
}

bool vk_validate_pixel_lightmap_shaders(void)
{
    if (vk_pixel_lightmap_mode() < 1)
        return true;

    VkShaderModule vert = vk_create_shader_module(vk_world_pixel_vert_spv,
                                                  sizeof(vk_world_pixel_vert_spv));
    VkShaderModule frag = vk_create_shader_module(vk_world_pixel_frag_spv,
                                                  sizeof(vk_world_pixel_frag_spv));
    VkShaderModule alpha = vk_create_shader_module(vk_world_pixel_alpha_frag_spv,
                                                   sizeof(vk_world_pixel_alpha_frag_spv));
    VkShaderModule glow = vk_create_shader_module(vk_world_pixel_glow_frag_spv,
                                                  sizeof(vk_world_pixel_glow_frag_spv));
    VkShaderModule glow_alpha = vk_create_shader_module(
        vk_world_pixel_glow_alpha_frag_spv,
        sizeof(vk_world_pixel_glow_alpha_frag_spv));
    bool ok = vert && frag && alpha && glow && glow_alpha;

    if (vert)
        vk.DestroyShaderModule(vk.device, vert, NULL);
    if (frag)
        vk.DestroyShaderModule(vk.device, frag, NULL);
    if (alpha)
        vk.DestroyShaderModule(vk.device, alpha, NULL);
    if (glow)
        vk.DestroyShaderModule(vk.device, glow, NULL);
    if (glow_alpha)
        vk.DestroyShaderModule(vk.device, glow_alpha, NULL);

    return ok;
}

bool vk_create_pixel_world_pipeline_layout(void)
{
    if (vk_pixel_lightmap_mode() < 2)
        return true;
    if (sizeof(vk_world_pixel_push_t) >
        vk.physical_device_properties.limits.maxPushConstantsSize) {
        Com_WPrintf("Vulkan pixel lightmaps mode 2 exceeds push constant limit; using mode 1\n");
        Cvar_Set("vk_pixel_lightmaps", "1");
        return true;
    }

    VkDescriptorSetLayout set_layouts[] = {
        vk.texture_set_layout,
        vk.texture_set_layout,
        vk.texture_set_layout,
#if USE_VULKAN_RAYTRACING
        vk.raytracing_active ? vk.rt_set_layout : vk.texture_set_layout,
#endif
    };
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(vk_world_pixel_push_t),
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = q_countof(set_layouts),
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    VkResult result = vk.CreatePipelineLayout(vk.device, &layout_info, NULL,
                                              &vk.pixel_world_pipeline_layout);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreatePipelineLayout(pixel_world)", result);

    return true;
}

bool vk_create_rect_pipeline_ex(VkPipeline *pipeline,
                                       const uint32_t *vert_spv,
                                       size_t vert_size)
{
    VkShaderModule vert = vk_create_shader_module(vert_spv, vert_size);
    if (!vert)
        return false;

    VkShaderModule frag = vk_create_shader_module(vk_rect_frag_spv, sizeof(vk_rect_frag_spv));
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
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

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = vk.swapchain_extent.width,
        .height = vk.swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = vk.swapchain_extent,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
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
        .rasterizationSamples = vk.sample_count,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment[2] = { [0] = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    } };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = vk.mrt_bloom ? 2 : 1,
        .pAttachments = color_blend_attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
    };

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .layout = vk.rect_pipeline_layout,
        .renderPass = vk.render_pass,
        .subpass = 0,
    };

    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                  &create_info, NULL,
                                                  pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

bool vk_create_rect_pipeline(void)
{
    return vk_create_rect_pipeline_ex(&vk.rect_pipeline, vk_rect_vert_spv,
                                      sizeof(vk_rect_vert_spv));
}

bool vk_create_texture_pipeline_ex(VkPipeline *pipeline,
                                          const uint32_t *frag_spv,
                                          size_t frag_size,
                                          bool additive,
                                          VkExtent2D extent,
                                          bool postprocess)
{
    VkShaderModule vert = vk_create_shader_module(vk_tex_vert_spv,
                                                  sizeof(vk_tex_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag = vk_create_shader_module(frag_spv, frag_size);
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
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
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = extent.width,
        .height = extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = extent,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
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
        .rasterizationSamples = postprocess ? VK_SAMPLE_COUNT_1_BIT : vk.sample_count,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment[2] = { [0] = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    } };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = !postprocess && vk.mrt_bloom ? 2 : 1,
        .pAttachments = color_blend_attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
    };

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .layout = vk.rect_pipeline_layout,
        .renderPass = postprocess ? vk.bloom_render_pass : vk.render_pass,
        .subpass = 0,
    };

    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                  &create_info, NULL,
                                                  pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

bool vk_create_texture_pipeline(void)
{
    return vk_create_texture_pipeline_ex(&vk.texture_pipeline,
                                          vk_tex_frag_spv,
                                          sizeof(vk_tex_frag_spv),
                                          false,
                                          vk.swapchain_extent,
                                          false);
}

bool vk_create_ssr_pipeline(void)
{
    if (!vk.ssr_pipeline_layout)
        return true;

    VkShaderModule vert = vk_create_shader_module(vk_ssr_vert_spv,
                                                   sizeof(vk_ssr_vert_spv));
    VkShaderModule frag = vk_create_shader_module(vk_ssr_frag_spv,
                                                   sizeof(vk_ssr_frag_spv));
    if (!vert || !frag) {
        if (vert) vk.DestroyShaderModule(vk.device, vert, NULL);
        if (frag) vk.DestroyShaderModule(vk.device, frag, NULL);
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
    };
    VkPipelineVertexInputStateCreateInfo vertex = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport viewport = { .width = max(vk.swapchain_extent.width / 2, 1),
                            .height = max(vk.swapchain_extent.height / 2, 1),
                            .maxDepth = 1.0f };
    VkRect2D scissor = { .extent = { max(vk.swapchain_extent.width / 2, 1),
                                     max(vk.swapchain_extent.height / 2, 1) } };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport,
        .scissorCount = 1, .pScissors = &scissor,
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
    VkPipelineColorBlendAttachmentState attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    };
    VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages), .pStages = stages,
        .pVertexInputState = &vertex, .pInputAssemblyState = &assembly,
        .pViewportState = &viewport_state, .pRasterizationState = &raster,
        .pMultisampleState = &multisample, .pDepthStencilState = &depth,
        .pColorBlendState = &blend, .layout = vk.ssr_pipeline_layout,
        .renderPass = vk.bloom_render_pass,
    };
    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                  &info, NULL, &vk.ssr_pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    if (result != VK_SUCCESS) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
        return false;
    }

    frag = vk_create_shader_module(vk_ssr_resolve_frag_spv,
                                   sizeof(vk_ssr_resolve_frag_spv));
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
        return false;
    }
    stages[1].module = frag;
    viewport.width = vk.swapchain_extent.width;
    viewport.height = vk.swapchain_extent.height;
    scissor.extent = vk.swapchain_extent;
    result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                         &info, NULL,
                                         &vk.ssr_resolve_pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);
    return result == VK_SUCCESS;
}

bool vk_create_color3d_pipeline(VkPipeline *pipeline, bool depth_test,
                                       bool depth_write, bool blend, bool additive,
                                       VkPrimitiveTopology topology)
{
    VkShaderModule vert = vk_create_shader_module(vk_color3d_vert_spv,
                                                  sizeof(vk_color3d_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag = vk_create_shader_module(vk_color3d_frag_spv,
                                                  sizeof(vk_color3d_frag_spv));
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
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
        .stride = sizeof(vk_vertex_t),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attributes[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, position),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_vertex_t, color),
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
        .topology = topology,
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = vk.swapchain_extent.width,
        .height = vk.swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = vk.swapchain_extent,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
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
        .rasterizationSamples = vk.sample_count,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment[2] = { [0] = {
        .blendEnable = blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE :
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = additive ? VK_BLEND_FACTOR_ONE :
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    } };
    if (pipeline == &vk.beam_bloom_pipeline ||
        pipeline == &vk.beam_add_bloom_pipeline) {
        color_blend_attachment[1] = color_blend_attachment[0];
        color_blend_attachment[0].colorWriteMask = 0;
    }
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = vk.mrt_bloom ? 2 : 1,
        .pAttachments = color_blend_attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_test,
        .depthWriteEnable = depth_write,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &vk_3d_dynamic_state,
        .layout = vk.rect_pipeline_layout,
        .renderPass = vk.render_pass,
        .subpass = 0,
    };

    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                 &create_info, NULL,
                                                 pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

bool vk_create_world_pipeline(VkPipeline *pipeline, bool depth_test,
                                     bool depth_write, bool blend,
                                     bool alpha_test, bool additive,
                                     bool glowmap, bool smooth_dlights,
                                     bool color_write)
{
    VkShaderModule vert = (smooth_dlights || glowmap) ?
        vk_create_shader_module(vk_world_lit_vert_spv,
                                sizeof(vk_world_lit_vert_spv)) :
        vk_create_shader_module(vk_world_vert_spv,
                                sizeof(vk_world_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag;
    if (smooth_dlights && alpha_test) {
        frag = vk_create_shader_module(vk_world_lit_alpha_frag_spv,
                                       sizeof(vk_world_lit_alpha_frag_spv));
#if USE_VULKAN_RAYTRACING
    } else if (smooth_dlights && vk.raytracing_active) {
        frag = vk_create_shader_module(vk_world_lit_rt_frag_spv,
                                       sizeof(vk_world_lit_rt_frag_spv));
#endif
    } else if (smooth_dlights) {
        frag = vk_create_shader_module(vk_world_lit_frag_spv,
                                       sizeof(vk_world_lit_frag_spv));
    } else if (glowmap) {
        frag = vk_create_shader_module(vk_world_glow_frag_spv,
                                       sizeof(vk_world_glow_frag_spv));
    } else if (alpha_test) {
        frag = vk_create_shader_module(vk_world_alpha_frag_spv,
                                       sizeof(vk_world_alpha_frag_spv));
    } else {
        frag = vk_create_shader_module(vk_world_frag_spv,
                                       sizeof(vk_world_frag_spv));
    }
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
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
        .stride = sizeof(vk_vertex_t),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attributes[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, position),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_vertex_t, color),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_vertex_t, uv),
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
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = vk.swapchain_extent.width,
        .height = vk.swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = vk.swapchain_extent,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
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
        .rasterizationSamples = vk.sample_count,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment[2] = { [0] = {
        .blendEnable = blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = additive ?
            VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = additive ?
            VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = color_write ?
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT : 0,
    } };
    if (pipeline == &vk.world_glow_pipeline ||
        pipeline == &vk.sprite_bloom_pipeline) {
        color_blend_attachment[1] = color_blend_attachment[0];
        color_blend_attachment[1].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT;
        color_blend_attachment[0].colorWriteMask = 0;
    } else if (pipeline == &vk.world_pipeline ||
               pipeline == &vk.world_alpha_pipeline) {
        color_blend_attachment[1].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = vk.mrt_bloom ? 2 : 1,
        .pAttachments = color_blend_attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_test,
        .depthWriteEnable = depth_write,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &vk_3d_dynamic_state,
        .layout = vk.rect_pipeline_layout,
        .renderPass = vk.render_pass,
        .subpass = 0,
    };

    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                 &create_info, NULL,
                                                 pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

bool vk_create_pixel_world_pipeline(VkPipeline *pipeline, bool alpha_test,
                                           bool glowmap, bool fast_path,
                                           bool ray_query)
{
    if (vk_pixel_lightmap_mode() < 2)
        return true;
    if (!vk.pixel_world_pipeline_layout) {
        Com_SetLastError("No Vulkan pixel world pipeline layout");
        return false;
    }

    VkShaderModule vert;
#if USE_VULKAN_RAYTRACING
    if (ray_query)
        vert = vk_create_shader_module(vk_world_pixel_rt_vert_spv,
                                       sizeof(vk_world_pixel_rt_vert_spv));
    else
#endif
        vert = vk_create_shader_module(vk_world_pixel_vert_spv,
                                       sizeof(vk_world_pixel_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag;
#if USE_VULKAN_RAYTRACING
    if (ray_query && glowmap && alpha_test)
        frag = vk_create_shader_module(vk_world_pixel_rt_glow_alpha_frag_spv,
                                       sizeof(vk_world_pixel_rt_glow_alpha_frag_spv));
    else if (ray_query && glowmap && vk.raytracing_quad_sharing)
        frag = vk_create_shader_module(vk_world_pixel_rt_quad_glow_frag_spv,
                                       sizeof(vk_world_pixel_rt_quad_glow_frag_spv));
    else if (ray_query && glowmap)
        frag = vk_create_shader_module(vk_world_pixel_rt_glow_frag_spv,
                                       sizeof(vk_world_pixel_rt_glow_frag_spv));
    else if (ray_query && alpha_test)
        frag = vk_create_shader_module(vk_world_pixel_rt_alpha_frag_spv,
                                       sizeof(vk_world_pixel_rt_alpha_frag_spv));
    else if (ray_query && vk.raytracing_quad_sharing)
        frag = vk_create_shader_module(vk_world_pixel_rt_quad_frag_spv,
                                       sizeof(vk_world_pixel_rt_quad_frag_spv));
    else if (ray_query)
        frag = vk_create_shader_module(vk_world_pixel_rt_frag_spv,
                                       sizeof(vk_world_pixel_rt_frag_spv));
    else
#else
    (void)ray_query;
#endif
    if (glowmap && alpha_test)
        frag = vk_create_shader_module(vk_world_pixel_glow_alpha_frag_spv,
                                       sizeof(vk_world_pixel_glow_alpha_frag_spv));
    else if (glowmap)
        frag = vk_create_shader_module(vk_world_pixel_glow_frag_spv,
                                       sizeof(vk_world_pixel_glow_frag_spv));
    else if (alpha_test)
        frag = vk_create_shader_module(vk_world_pixel_alpha_frag_spv,
                                       sizeof(vk_world_pixel_alpha_frag_spv));
    else
        frag = vk_create_shader_module(vk_world_pixel_frag_spv,
                                       sizeof(vk_world_pixel_frag_spv));
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
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
    VkBool32 fast_value = fast_path ? VK_TRUE : VK_FALSE;
    VkSpecializationMapEntry fast_entry = {
        .constantID = 0,
        .offset = 0,
        .size = sizeof(fast_value),
    };
    VkSpecializationInfo fast_info = {
        .mapEntryCount = 1,
        .pMapEntries = &fast_entry,
        .dataSize = sizeof(fast_value),
        .pData = &fast_value,
    };
    stages[1].pSpecializationInfo = &fast_info;
    VkVertexInputBindingDescription bindings[] = {
        {
            .binding = 0,
            .stride = sizeof(vk_vertex_t),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
        {
            .binding = 1,
            .stride = sizeof(float) * 2,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
        {
            .binding = 2,
            .stride = 8,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
    };
    VkVertexInputAttributeDescription attributes[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, position),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_vertex_t, color),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_vertex_t, uv),
        },
        {
            .location = 3,
            .binding = 1,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = 0,
        },
        {
            .location = 4,
            .binding = 2,
            .format = VK_FORMAT_R32G32_UINT,
            .offset = 0,
        },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = ray_query ? 3 : 2,
        .pVertexBindingDescriptions = bindings,
        .vertexAttributeDescriptionCount = ray_query ? 5 : 4,
        .pVertexAttributeDescriptions = attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport viewport = {
        .width = vk.swapchain_extent.width,
        .height = vk.swapchain_extent.height,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .extent = vk.swapchain_extent,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
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
        .rasterizationSamples = vk.sample_count,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment[2] = { [0] = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    } };
    if (vk.mrt_bloom) {
        color_blend_attachment[1].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = vk.mrt_bloom ? 2 : 1,
        .pAttachments = color_blend_attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &vk_3d_dynamic_state,
        .layout = vk.pixel_world_pipeline_layout,
        .renderPass = vk.render_pass,
        .subpass = 0,
    };
    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                 &create_info, NULL, pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines(pixel_world)", result);

    return true;
}

bool vk_create_alias_pipeline(VkPipeline *pipeline, bool depth_write,
                                     bool blend, bool color_write,
                                     bool alpha_test,
                                     bool color_only,
                                     VkCullModeFlags cull_mode,
                                     VkPolygonMode polygon_mode,
                                     VkPrimitiveTopology topology,
                                     VkCompareOp depth_compare,
                                     bool depth_bias_enable,
                                     float depth_bias_constant,
                                     float depth_bias_slope,
                                     VkPipelineDepthStencilStateCreateInfo *stencil_state)
{
    VkShaderModule vert = color_only ?
        vk_create_shader_module(vk_alias_shadow_vert_spv,
                                sizeof(vk_alias_shadow_vert_spv)) :
        vk_create_shader_module(vk_alias_vert_spv,
                                sizeof(vk_alias_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag = color_only ?
        vk_create_shader_module(vk_alias_shadow_frag_spv,
                                sizeof(vk_alias_shadow_frag_spv)) :
        alpha_test ?
        vk_create_shader_module(vk_alias_alpha_frag_spv,
                                sizeof(vk_alias_alpha_frag_spv)) :
        vk_create_shader_module(vk_alias_frag_spv,
                                sizeof(vk_alias_frag_spv));
    if (!frag) {
        vk.DestroyShaderModule(vk.device, vert, NULL);
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
    VkVertexInputBindingDescription bindings[] = {
        {
            .binding = 0,
            .stride = sizeof(vk_vertex_t),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
        {
            .binding = 1,
            .stride = sizeof(vk_vertex_t),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
    };
    VkVertexInputAttributeDescription attributes[] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, position),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(vk_vertex_t, color),
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(vk_vertex_t, uv),
        },
        {
            .location = 3,
            .binding = 1,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, position),
        },
        {
            .location = 4,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, normal),
        },
        {
            .location = 5,
            .binding = 1,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(vk_vertex_t, normal),
        },
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = q_countof(bindings),
        .pVertexBindingDescriptions = bindings,
        .vertexAttributeDescriptionCount = q_countof(attributes),
        .pVertexAttributeDescriptions = attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology,
    };
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = vk.swapchain_extent.width,
        .height = vk.swapchain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = vk.swapchain_extent,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = polygon_mode,
        .cullMode = cull_mode,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
        .depthBiasEnable = depth_bias_enable,
        .depthBiasConstantFactor = depth_bias_constant,
        .depthBiasSlopeFactor = depth_bias_slope,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = vk.sample_count,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment[2] = { [0] = {
        .blendEnable = blend,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = color_write ?
            (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT) : 0,
    } };
    if (pipeline == &vk.alias_bloom_pipeline ||
        pipeline == &vk.alias_shell_bloom_pipeline) {
        color_blend_attachment[1] = color_blend_attachment[0];
        color_blend_attachment[0].colorWriteMask = 0;
    } else if (pipeline == &vk.alias_pipeline ||
               pipeline == &vk.alias_alpha_pipeline) {
        // Opaque alias fragments replace any bloom left by geometry drawn
        // earlier. Zero/zero blending writes black without requiring a
        // separate fragment shader; alpha-tested holes still discard.
        color_blend_attachment[1].blendEnable = VK_TRUE;
        color_blend_attachment[1].srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment[1].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment[1].colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment[1].alphaBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment[1].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = vk.mrt_bloom ? 2 : 1,
        .pAttachments = color_blend_attachment,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil;
    if (stencil_state) {
        depth_stencil = *stencil_state;
    } else {
        depth_stencil = (VkPipelineDepthStencilStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = depth_write,
            .depthCompareOp = depth_compare,
        };
    }
    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = q_countof(stages),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &vk_3d_dynamic_state,
        .layout = vk.rect_pipeline_layout,
        .renderPass = vk.render_pass,
        .subpass = 0,
    };

    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                 &create_info, NULL,
                                                 pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

VkPipelineDepthStencilStateCreateInfo vk_shadow_depth_stencil_state(void)
{
    VkStencilOpState stencil = {
        .failOp = VK_STENCIL_OP_KEEP,
        .passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP,
        .depthFailOp = VK_STENCIL_OP_KEEP,
        .compareOp = VK_COMPARE_OP_EQUAL,
        .compareMask = 0xff,
        .writeMask = 0xff,
        .reference = 0,
    };

    return (VkPipelineDepthStencilStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .stencilTestEnable = vk_shadow_stencil_enabled(),
        .front = stencil,
        .back = stencil,
    };
}

#endif // USE_VULKAN
