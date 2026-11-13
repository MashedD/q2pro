/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan post-processing: bloom, screen-space reflections, waterwarp and the scene composite.

#include "vk_local.h"

#if USE_VULKAN

// Blur sigma in bloom texels. OpenGL applies gl_bloom_sigma, scaled to the
// view height, in its quarter-resolution target; scale it so the glow has
// the same size on screen whatever vk_bloom_downsample is.
float vk_bloom_blur_sigma(void)
{
    float sigma = vk_bloom_sigma ? Cvar_ClampValue(vk_bloom_sigma, 1.0f, 25.0f) : 4.0f;

    sigma *= max((float)vk.fd.height, 1.0f) / 1080.0f;
    return sigma * 4.0f / (float)vk_bloom_downsample_value();
}

uint32_t vk_bloom_downsample_value(void)
{
    int value = vk_bloom_downsample ? Cvar_ClampInteger(vk_bloom_downsample, 1, 16) : 4;

    return max(value, 1);
}

bool vk_bloom_requested(void)
{
    return gl_bloom && gl_bloom->integer > 0;
}

// Bloom draws into two color attachments with different blend states, which
// needs the independentBlend feature.
bool vk_bloom_supported(void)
{
    static bool warned;

    if (vk.physical_device_features.independentBlend)
        return true;
    if (!warned) {
        Com_WPrintf("Vulkan bloom needs the independentBlend feature, which "
                    "this GPU lacks; gl_bloom is ignored\n");
        warned = true;
    }
    return false;
}

static bool vk_bloom_enabled(void)
{
    return gl_bloom && gl_bloom->integer > 0 && vk.mrt_bloom &&
        vk.scene_framebuffer &&
        vk.bloom_framebuffer && vk.blur_framebuffer &&
        vk.scene_texture.descriptor_set &&
        vk.bloom_source_texture.descriptor_set &&
        vk.bloom_texture.descriptor_set && vk.blur_texture.descriptor_set;
}

bool vk_bloom_enabled_for_frame(void)
{
    return vk_bloom_enabled() && vk.fd_valid &&
        !(vk.fd.rdflags & RDF_NOWORLDMODEL);
}

bool vk_waterwarp_enabled_for_frame(void)
{
    return vk_waterwarp && vk_waterwarp->integer > 0 && vk.fd_valid &&
        (vk.fd.rdflags & RDF_UNDERWATER) && vk.scene_framebuffer &&
        vk.scene_texture.descriptor_set;
}

bool vk_ssr_enabled_for_frame(void)
{
#if USE_VULKAN_RAYTRACING
    return vk.ssr_ready && vk.raytracing_active && vk_raytracing &&
        vk_raytracing->integer && vk_rt_reflections &&
        vk_rt_reflections->value > 0.001f && vk.fd_valid &&
        (!vk_rt_debug || vk_rt_debug->integer < 8 ||
         vk_rt_debug->integer == 18) &&
        !(vk.fd.rdflags & RDF_NOWORLDMODEL);
#else
    return false;
#endif
}

static void vk_draw_texture_rect_sized(VkPipeline pipeline,
                                       const vk_texture_t *texture,
                                       const vec4_t color,
                                       int x, int y, int w, int h,
                                       uint32_t screen_w,
                                       uint32_t screen_h,
                                       const vec4_t uv)
{
    if (!vk.render_pass_active || !pipeline || !texture->descriptor_set ||
        w <= 0 || h <= 0)
        return;

    vk_draw_push_t push = {
        .rect = { x, y, w, h },
        .color = { color[0], color[1], color[2], color[3] },
        .screen = { screen_w, screen_h },
        .uv = { uv[0], uv[1], uv[2], uv[3] },
    };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
    c.trisDrawn += 2;
    c.batchesDrawn2D++;
    c.picsDrawn2D++;
}

void vk_draw_refdef_texture(VkPipeline pipeline,
                                   const vk_texture_t *texture,
                                   const vec4_t color)
{
    vec4_t uv = { 0.0f, 0.0f, 1.0f, 1.0f };

    if (vk.fd_valid) {
        float scale_x = texture->width / max((float)vk.swapchain_extent.width, 1.0f);
        float scale_y = texture->height / max((float)vk.swapchain_extent.height, 1.0f);
        float tex_w = max((float)texture->width, 1.0f);
        float tex_h = max((float)texture->height, 1.0f);

        uv[0] = (vk.fd.x * scale_x) / tex_w;
        uv[1] = (vk.fd.y * scale_y) / tex_h;
        uv[2] = ((vk.fd.x + vk.fd.width) * scale_x) / tex_w;
        uv[3] = ((vk.fd.y + vk.fd.height) * scale_y) / tex_h;
        vk_draw_texture_rect_sized(pipeline, texture, color,
                                   vk.fd.x, vk.fd.y,
                                   vk.fd.width, vk.fd.height,
                                   vk.swapchain_extent.width,
                                   vk.swapchain_extent.height, uv);
    } else {
        vk_draw_texture_rect_sized(pipeline, texture, color,
                                   0, 0,
                                   vk.swapchain_extent.width,
                                   vk.swapchain_extent.height,
                                   vk.swapchain_extent.width,
                                   vk.swapchain_extent.height, uv);
    }
}

void vk_composite_scene_texture(void)
{
    vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
    VkPipeline pipeline = vk.scene_pipeline ? vk.scene_pipeline : vk.texture_pipeline;

    if (vk.frame_waterwarp) {
        color[0] = vk.fd.time;
        pipeline = vk.waterwarp_pipeline;
    }

    vk_draw_refdef_texture(pipeline, &vk.scene_texture, color);
}

void vk_draw_fullscreen_texture_sized(VkPipeline pipeline,
                                             const vk_texture_t *texture,
                                             const vec4_t color,
                                             uint32_t width, uint32_t height)
{
    if (!vk.render_pass_active || !pipeline || !texture->descriptor_set)
        return;

    vk_draw_push_t push = {
        .rect = { 0, 0, width, height },
        .color = { color[0], color[1], color[2], color[3] },
        .screen = { width, height },
        .uv = { 0.0f, 0.0f, 1.0f, 1.0f },
    };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];

    vk_bind_pipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vk_bind_texture_descriptor(cmd, texture->descriptor_set);
    vk_push_constants(cmd, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
    c.trisDrawn += 2;
    c.batchesDrawn2D++;
    c.picsDrawn2D++;
}

static void vk_draw_bloom_only_entities(const refdef_t *fd)
{
    bool old = vk.drawing_bloom;

    vk.drawing_bloom = true;
    vk_set_3d_viewport(fd);
    vk_draw_entities(fd, VK_ENTITY_BLOOM_ONLY);
    vk.drawing_bloom = old;
}

static void vk_draw_bloom_beams(const refdef_t *fd)
{
    bool old = vk.drawing_bloom;

    vk.drawing_bloom = true;
    vk_set_3d_viewport(fd);
    vk_draw_entities(fd, VK_ENTITY_BEAM);
    vk.drawing_bloom = old;
}

static void vk_draw_bloom_source_entities(const refdef_t *fd)
{
    bool old = vk.drawing_bloom;

    vk.drawing_bloom = true;
    vk_set_3d_viewport(fd);
    vk_draw_entities(fd, VK_ENTITY_BLOOM_SOURCE);
    vk.drawing_bloom = old;
}

static void vk_render_ssr(void)
{
    if (!vk.frame_ssr || !vk.fd_valid)
        return;

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    if (vk.render_pass_active) {
        vk.CmdEndRenderPass(cmd);
        vk.render_pass_active = false;
    }
    vk_transition_scene(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vk_transition_color_target(cmd, &vk.bloom_source_texture,
                               &vk.bloom_source_layout,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_ACCESS_SHADER_READ_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vk_transition_depth(cmd, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vk_transition_color_target(cmd, &vk.ssr_texture, &vk.ssr_layout,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    const VkClearColorValue black = { .float32 = { 0, 0, 0, 0 } };
    uint32_t width = max(vk.ssr_texture.width, 1);
    uint32_t height = max(vk.ssr_texture.height, 1);
    vk_begin_render_pass_sized(vk.bloom_render_pass, vk.ssr_framebuffer,
                               black, width, height);

    mat4_t proj, view;
    vk_projection_matrix(proj, vk.fd.fov_x, vk.fd.fov_y, vk.fd.rdflags);
    vk_view_matrix(view, &vk.fd);
    vk_ssr_push_t push = {
        .projection = { proj[0], proj[5], proj[10], proj[14] },
        .control = {
            vk_rt_reflections ? Cvar_ClampValue(vk_rt_reflections, 0.0f, 1.0f) : 0.0f,
            vk_rt_debug && ((vk_rt_debug->integer >= 4 &&
                vk_rt_debug->integer <= 7) ||
                vk_rt_debug->integer == 18 ||
                vk_rt_debug->integer == 20) ?
                (float)vk_rt_debug->integer : 0.0f,
            1.0f / max((float)vk.scene_texture.width, 1.0f),
            1.0f / max((float)vk.scene_texture.height, 1.0f),
        },
        .view_up = {
            view[8], view[9], view[10],
            vk_rt_reflection_glints ?
                Cvar_ClampValue(vk_rt_reflection_glints, 0.0f, 1.0f) : 0.55f,
        },
    };
    vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.ssr_pipeline);
    vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             vk.ssr_pipeline_layout, 0, 1,
                             &vk.ssr_descriptor_set, 0, NULL);
    vk.CmdPushConstants(cmd, vk.ssr_pipeline_layout,
                        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
    c.trisDrawn += 2;
    vk.CmdEndRenderPass(cmd);
    vk.render_pass_active = false;
    vk_reset_bind_cache();
    vk_transition_color_target(cmd, &vk.ssr_texture, &vk.ssr_layout,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_ACCESS_SHADER_READ_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vk_transition_color_target(cmd, &vk.ssr_resolve_texture,
                               &vk.ssr_resolve_layout,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    width = max(vk.ssr_resolve_texture.width, 1);
    height = max(vk.ssr_resolve_texture.height, 1);
    vk_begin_render_pass_sized(vk.bloom_render_pass,
                               vk.ssr_resolve_framebuffer,
                               black, width, height);
    vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                       vk.ssr_resolve_pipeline);
    vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             vk.ssr_pipeline_layout, 0, 1,
                             &vk.ssr_resolve_descriptor_set, 0, NULL);
    // The march shader needs inverse scene dimensions in control.zw, while
    // the resolve derives its texel size from the input texture. Reuse the
    // now-free control.z slot without expanding the push-constant layout.
    push.control[2] = vk_rt_reflection_dispersion ?
        Cvar_ClampValue(vk_rt_reflection_dispersion, 0.0f, 1.0f) : 0.6f;
    vk.CmdPushConstants(cmd, vk.ssr_pipeline_layout,
                        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
    c.trisDrawn += 2;
    vk.CmdEndRenderPass(cmd);
    vk.render_pass_active = false;
    vk_reset_bind_cache();
    vk_transition_color_target(cmd, &vk.ssr_resolve_texture,
                               &vk.ssr_resolve_layout,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_ACCESS_SHADER_READ_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    // The main render pass, which composites the reflections, expects the
    // depth buffer back in attachment layout.
    vk_transition_depth(cmd, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
}

static void vk_composite_ssr(void)
{
    if (!vk.frame_ssr)
        return;
    const vec4_t white = { 1, 1, 1, 1 };
    vk_draw_refdef_texture(vk.texture_pipeline,
                          &vk.ssr_resolve_texture, white);
}

void vk_finish_postprocess_scene(void)
{
    if (!vk.frame_bloom && !vk.frame_waterwarp && !vk.frame_ssr)
        return;

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    bool bloom = vk.frame_bloom;
    bool waterwarp = vk.frame_waterwarp;

    // Entities that only feed bloom are drawn while the scene pass is still
    // open. Screen-space reflections end that pass, and drawing them later
    // lost highlight glows, shell glows and beam bloom with ray tracing on.
    // They write only the bloom target, which the reflections don't read.
    if (bloom && vk.render_pass_active && vk.fd_valid) {
        vk_draw_bloom_source_entities(&vk.fd);
        vk_draw_bloom_beams(&vk.fd);
        vk_draw_bloom_only_entities(&vk.fd);
    }

    vk_render_ssr();

    if (bloom) {
        const vec4_t white = { 1.0f, 1.0f, 1.0f, 1.0f };
        const VkClearColorValue black = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
        uint32_t bloom_w = max(vk.bloom_texture.width, 1);
        uint32_t bloom_h = max(vk.bloom_texture.height, 1);
        vec4_t downscale_step = {
            1.0f / (float)max(vk.bloom_source_texture.width, 1),
            1.0f / (float)max(vk.bloom_source_texture.height, 1),
            0.0f,
            1.0f,
        };
        float sigma = vk_bloom_blur_sigma();
        int iterations = (vk_showbloom && vk_showbloom->integer >= 2) ? 0 :
            (gl_bloom ? Cvar_ClampInteger(gl_bloom, 1, 8) : 1);
        vec4_t blur_x = {
            1.0f / (float)bloom_w,
            0.0f,
            sigma,
            1.0f,
        };
        vec4_t blur_y = {
            0.0f,
            1.0f / (float)bloom_h,
            sigma,
            1.0f,
        };

        if (vk.render_pass_active) {
            vk.CmdEndRenderPass(cmd);
            vk.render_pass_active = false;
        }

        vk_transition_scene(cmd,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        vk_transition_color_target(cmd, &vk.bloom_source_texture, &vk.bloom_source_layout,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   VK_ACCESS_SHADER_READ_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        vk_transition_color_target(cmd, &vk.bloom_texture, &vk.bloom_layout,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        vk_begin_render_pass_sized(vk.bloom_render_pass, vk.bloom_framebuffer,
                                   black, bloom_w, bloom_h);
        vk_draw_fullscreen_texture_sized(vk.bloom_downscale_pipeline,
                                         &vk.bloom_source_texture, downscale_step,
                                         bloom_w, bloom_h);
        vk.CmdEndRenderPass(cmd);
        vk.render_pass_active = false;

        for (int i = 0; i < iterations; i++) {
            vk_transition_color_target(cmd, &vk.bloom_texture, &vk.bloom_layout,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                       VK_ACCESS_SHADER_READ_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            vk_transition_color_target(cmd, &vk.blur_texture, &vk.blur_layout,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            vk_begin_render_pass_sized(vk.bloom_render_pass, vk.blur_framebuffer,
                                       black, bloom_w, bloom_h);
            vk_draw_fullscreen_texture_sized(vk.bloom_blur_pipeline,
                                             &vk.bloom_texture, blur_x,
                                             bloom_w, bloom_h);
            vk.CmdEndRenderPass(cmd);
            vk.render_pass_active = false;

            vk_transition_color_target(cmd, &vk.blur_texture, &vk.blur_layout,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                       VK_ACCESS_SHADER_READ_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            vk_transition_color_target(cmd, &vk.bloom_texture, &vk.bloom_layout,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            vk_begin_render_pass_sized(vk.bloom_render_pass, vk.bloom_framebuffer,
                                       black, bloom_w, bloom_h);
            vk_draw_fullscreen_texture_sized(vk.bloom_blur_pipeline,
                                             &vk.blur_texture, blur_y,
                                             bloom_w, bloom_h);
            vk.CmdEndRenderPass(cmd);
            vk.render_pass_active = false;
        }

        vk_transition_color_target(cmd, &vk.bloom_texture, &vk.bloom_layout,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   VK_ACCESS_SHADER_READ_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        vk_transition_color_target(cmd, &vk.bloom_source_texture,
                                   &vk.bloom_source_layout,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        vk_begin_render_pass(vk.render_pass, vk.framebuffers[vk.current_image],
                             vk_frame_clear_color());
        if (vk_showbloom && vk_showbloom->integer) {
            vk_draw_refdef_texture(vk.texture_pipeline, &vk.bloom_texture, white);
        } else {
            vk_composite_scene_texture();
            vk_draw_refdef_texture(vk.bloom_add_pipeline, &vk.bloom_texture, white);
            vk_composite_ssr();
        }
    }

    if (!bloom && (waterwarp || vk.frame_ssr)) {
        if (vk.render_pass_active) {
            vk.CmdEndRenderPass(cmd);
            vk.render_pass_active = false;
        }

        vk_transition_scene(cmd,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        vk_transition_color_target(cmd, &vk.bloom_source_texture,
                                   &vk.bloom_source_layout,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        vk_begin_render_pass(vk.render_pass, vk.framebuffers[vk.current_image],
                             vk_frame_clear_color());
        vk_composite_scene_texture();
        vk_composite_ssr();
    }

    vk.frame_bloom = false;
    vk.frame_waterwarp = false;
    vk.frame_ssr = false;
}

#endif // USE_VULKAN
