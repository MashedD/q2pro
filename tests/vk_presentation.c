/* GPU-free contract tests for the real Vulkan framebuffer/presentation code.
 * Run with: sh tests/vk_presentation.sh build-lin64
 * Unused backend sections are discarded at link time; Vulkan calls below
 * record their arguments rather than using a driver. */
#include "../src/refresh/vk_backend.c"
#include <assert.h>

#define HANDLE(type, n) ((type)(uintptr_t)(n))

statCounters_t c;
cvar_t paused_cvar;
cvar_t *cl_paused = &paused_cvar;
cvar_t *sv_paused;
cmdbuf_t cmd_buffer;
static char last_error[256];
static unsigned framebuffer_calls, pass_calls, draw_calls;
static VkRenderPass last_pass;
static VkExtent2D last_extent;
static VkViewport last_viewport;
static vk_draw_push_t last_push;
static VkPipeline last_pipeline;
static bool pipeline_blends;
static uint32_t pipeline_colors;
static VkAttachmentDescription clear_attachment;
static VkSubpassDependency clear_dependency;

void Com_SetLastError(const char *message)
{
    snprintf(last_error, sizeof(last_error), "%s", message);
}

char *va(const char *format, ...)
{
    static char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

void Com_LPrintf(print_type_t type, const char *format, ...) { }
void *Z_Mallocz(size_t size) { return calloc(1, size); }
bool SCR_ParseColor(const char *text, color_t *color) { return false; }
void Cbuf_AddText(cmdbuf_t *buf, const char *text) { }
void Cvar_SetByVar(cvar_t *var, const char *value, from_t from) { abort(); }
int Cvar_ClampInteger(cvar_t *var, int lo, int hi)
{
    return Q_clip(var->integer, lo, hi);
}
float Cvar_ClampValue(cvar_t *var, float lo, float hi)
{
    return Q_clipf(var->value, lo, hi);
}

static VKAPI_ATTR VkResult VKAPI_CALL create_render_pass(VkDevice device,
    const VkRenderPassCreateInfo *info, const VkAllocationCallbacks *allocator,
    VkRenderPass *out)
{
    if (out == &vk.bloom_render_pass || out == &vk.presentation_load_pass) {
        assert(info->attachmentCount == 1);
        assert(info->pSubpasses[0].colorAttachmentCount == 1);
        assert(!info->pSubpasses[0].pDepthStencilAttachment);
        assert(info->dependencyCount == 1);
        if (out == &vk.bloom_render_pass) {
            clear_attachment = info->pAttachments[0];
            clear_dependency = info->pDependencies[0];
            assert(clear_attachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
        } else {
            VkAttachmentDescription load = info->pAttachments[0];
            assert(load.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
            load.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            assert(!memcmp(&clear_attachment, &load, sizeof(load)));
            assert(!memcmp(&clear_dependency, info->pDependencies, sizeof(clear_dependency)));
        }
    }
    *out = HANDLE(VkRenderPass, out == &vk.render_pass ? 3 :
        out == &vk.bloom_render_pass ? 4 : out == &vk.presentation_load_pass ? 5 : 6);
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL create_framebuffer(VkDevice device,
    const VkFramebufferCreateInfo *info, const VkAllocationCallbacks *allocator,
    VkFramebuffer *out)
{
    framebuffer_calls++;
    assert(info->width == vk.swapchain_extent.width);
    assert(info->height == vk.swapchain_extent.height);
    if (vk.separate_presentation) {
        assert(info->renderPass == vk.bloom_render_pass);
        assert(info->attachmentCount == 1);
        assert(info->pAttachments[0] == vk.swapchain_views[0]);
    } else {
        assert(info->renderPass == vk.render_pass);
        bool msaa = vk.sample_count != VK_SAMPLE_COUNT_1_BIT;
        assert(info->attachmentCount == (vk.mrt_bloom ?
                                        (msaa ? 5u : 3u) : (msaa ? 3u : 2u)));
    }
    *out = HANDLE(VkFramebuffer, 20 + framebuffer_calls);
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL create_shader(VkDevice device,
    const VkShaderModuleCreateInfo *info, const VkAllocationCallbacks *allocator,
    VkShaderModule *out)
{
    *out = HANDLE(VkShaderModule, 40);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL destroy_shader(VkDevice device,
    VkShaderModule shader, const VkAllocationCallbacks *allocator) { }

static VKAPI_ATTR VkResult VKAPI_CALL create_pipeline(VkDevice device,
    VkPipelineCache cache, uint32_t count, const VkGraphicsPipelineCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkPipeline *out)
{
    assert(count == 1);
    assert(info->renderPass == (vk.separate_presentation ?
                               vk.bloom_render_pass : vk.render_pass));
    pipeline_colors = info->pColorBlendState->attachmentCount;
    pipeline_blends = info->pColorBlendState->pAttachments[0].blendEnable;
    assert(pipeline_colors == (!vk.separate_presentation && vk.mrt_bloom ? 2u : 1u));
    assert(info->pMultisampleState->rasterizationSamples == vk.sample_count);
    *out = HANDLE(VkPipeline, 50);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL begin_pass(VkCommandBuffer cmd,
    const VkRenderPassBeginInfo *info, VkSubpassContents contents)
{
    pass_calls++;
    last_pass = info->renderPass;
    last_extent = info->renderArea.extent;
    assert(info->clearValueCount == (last_pass == vk.render_pass ?
                                   (vk.mrt_bloom ? 3u : 2u) : 1u));
}

static VKAPI_ATTR void VKAPI_CALL end_pass(VkCommandBuffer cmd) { }
static VKAPI_ATTR void VKAPI_CALL viewport(VkCommandBuffer cmd,
    uint32_t first, uint32_t count, const VkViewport *value)
{
    assert(count == 1);
    last_viewport = *value;
}
static VKAPI_ATTR void VKAPI_CALL scissor(VkCommandBuffer cmd,
    uint32_t first, uint32_t count, const VkRect2D *value) { }
static VKAPI_ATTR void VKAPI_CALL bind_pipeline(VkCommandBuffer cmd,
    VkPipelineBindPoint point, VkPipeline pipeline) { last_pipeline = pipeline; }
static VKAPI_ATTR void VKAPI_CALL bind_descriptor(VkCommandBuffer cmd,
    VkPipelineBindPoint point, VkPipelineLayout layout, uint32_t first,
    uint32_t count, const VkDescriptorSet *sets, uint32_t offsets,
    const uint32_t *values) { }
static VKAPI_ATTR void VKAPI_CALL push_constants(VkCommandBuffer cmd,
    VkPipelineLayout layout, VkShaderStageFlags stages, uint32_t offset,
    uint32_t size, const void *data)
{
    assert(size == sizeof(last_push));
    memcpy(&last_push, data, size);
}
static VKAPI_ATTR void VKAPI_CALL record_draw(VkCommandBuffer cmd, uint32_t vertices,
    uint32_t instances, uint32_t first_vertex, uint32_t first_instance)
{
    assert(vertices == 6);
    draw_calls++;
}

static void check_configuration(uint32_t width, uint32_t height, bool bloom,
                                float scale)
{
    bool separate = scale > 1;
    VkImageView view = HANDLE(VkImageView, 1);
    VkCommandBuffer command = HANDLE(VkCommandBuffer, 2);
    memset(&vk, 0, sizeof(vk));
    vk.swapchain_extent = (VkExtent2D) { width, height };
    vk.render_extent = (VkExtent2D) {
        (uint32_t)(width / scale) / 2 * 2,
        (uint32_t)(height / scale) / 2 * 2,
    };
    vk.swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
    vk.depth_format = VK_FORMAT_D32_SFLOAT;
    vk.sample_count = VK_SAMPLE_COUNT_1_BIT;
    vk.separate_presentation = separate;
    vk.mrt_bloom = bloom;
    vk.bloom_source_texture.width = vk.render_extent.width;
    vk.bloom_source_texture.height = vk.render_extent.height;
    vk.swapchain_image_count = 1;
    vk.swapchain_views = &view;
    vk.command_buffers = &command;
    vk.CreateRenderPass = create_render_pass;
    vk.CreateFramebuffer = create_framebuffer;
    vk.CreateShaderModule = create_shader;
    vk.DestroyShaderModule = destroy_shader;
    vk.CreateGraphicsPipelines = create_pipeline;
    vk.CmdBeginRenderPass = begin_pass;
    vk.CmdEndRenderPass = end_pass;
    vk.CmdSetViewport = viewport;
    vk.CmdSetScissor = scissor;
    vk.CmdBindPipeline = bind_pipeline;
    vk.CmdBindDescriptorSets = bind_descriptor;
    vk.CmdPushConstants = push_constants;
    vk.CmdDraw = record_draw;

    assert(vk_create_render_pass());
    assert(vk_create_framebuffers());
    assert(vk_create_rect_pipeline());
    assert(pipeline_blends);
    assert(vk_create_texture_pipeline());
    assert(pipeline_blends);
    assert(vk_create_texture_pipeline_ex(&vk.presentation_pipeline,
        vk_tex_frag_spv, sizeof(vk_tex_frag_spv), VK_TEXTURE_OPAQUE,
        vk.swapchain_extent, separate, false));
    assert(!pipeline_blends);

    if (separate) {
        /* A menu-only frame stays entirely on the display target. */
        vk_begin_presentation(true);
        assert(last_pass == vk.bloom_render_pass);
        assert(last_extent.width == width && last_extent.height == height);
        assert(last_viewport.width == width && last_viewport.height == height);
        unsigned before = pass_calls;
        VKR_SetScale(0.5f);
        assert(pass_calls == before);

        /* A submitted scene changes the physical target, then resumes LOAD. */
        vk_begin_scene_view();
        assert(last_pass == vk.render_pass);
        assert(last_extent.width == vk.render_extent.width);
        assert(last_extent.height == vk.render_extent.height);
        vk_begin_presentation(false);
        assert(last_pass == vk.presentation_load_pass);
        assert(vk.active_render_pass == vk.presentation_load_pass);
        assert(vk.active_target_extent.width == width);

        vk_texture_t texture = {
            .width = width, .height = height,
            .descriptor_set = HANDLE(VkDescriptorSet, 60),
        };
        vk.fd_valid = true;
        vk.fd = (refdef_t) { .x = 0, .y = 0, .width = width, .height = height };
        vk_composite_presentation_texture(&texture);
        assert(last_pipeline == vk.presentation_pipeline);
        assert(last_push.rect[2] == width && last_push.rect[3] == height);
        assert(last_push.uv[0] == 0 && last_push.uv[2] == 1);

        /* Reduced viewsize and player previews preserve the surrounding UI.
         * Both full-size FSR output and reduced fallback have identical UVs. */
        vk.fd.x = width / 4;
        vk.fd.y = height / 4;
        vk.fd.width = width / 2;
        vk.fd.height = height / 2;
        for (int fallback = 0; fallback < 2; fallback++) {
            texture.width = fallback ? vk.render_extent.width : width;
            texture.height = fallback ? vk.render_extent.height : height;
            vk_composite_presentation_texture(&texture);
            assert(last_push.rect[0] == width / 4);
            assert(last_push.rect[2] == width / 2);
            assert(fabsf(last_push.uv[0] - 0.25f) < 0.00001f);
            assert(fabsf(last_push.uv[2] - 0.75f) < 0.00001f);
        }
    }
    free(vk.framebuffers);
    vk.framebuffers = NULL;

    if (!separate) {
        vk.sample_count = VK_SAMPLE_COUNT_4_BIT;
        assert(vk_create_render_pass());
        assert(vk_create_framebuffers());
        assert(vk_create_rect_pipeline());
        assert(vk_create_texture_pipeline());
        free(vk.framebuffers);
        vk.framebuffers = NULL;
        vk.sample_count = VK_SAMPLE_COUNT_1_BIT;
    }

    if (bloom) {
        vk.bloom_source_texture.width--;
        unsigned before = framebuffer_calls;
        assert(!vk_create_framebuffers());
        assert(framebuffer_calls == before);
        assert(strstr(last_error, "exceeds bloom attachment"));
    }

    /* Explicitly reject the original cropped-attachment configuration. */
    vk.separate_presentation = false;
    vk.render_extent.width = width * 2 / 3;
    unsigned before = framebuffer_calls;
    assert(!vk_create_framebuffers());
    assert(framebuffer_calls == before);
    assert(strstr(last_error, "exceeds scene attachments"));
}

static unsigned barrier_calls, barrier_images;
static VKAPI_ATTR void VKAPI_CALL record_barriers(VkCommandBuffer cmd,
    VkPipelineStageFlags src, VkPipelineStageFlags dst, VkDependencyFlags flags,
    uint32_t memory_count, const VkMemoryBarrier *memory,
    uint32_t buffer_count, const VkBufferMemoryBarrier *buffers,
    uint32_t count, const VkImageMemoryBarrier *images)
{
    assert(cmd == HANDLE(VkCommandBuffer, 99));
    assert(src && dst && count && count <= 4);
    assert(!memory_count && !buffer_count);
    barrier_calls++;
    barrier_images += count;
    for (unsigned i = 0; i < count; i++) {
        assert(images[i].srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
        assert(images[i].dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
        assert(images[i].srcAccessMask & VK_ACCESS_SHADER_WRITE_BIT);
        assert(images[i].dstAccessMask & VK_ACCESS_SHADER_READ_BIT);
    }
}

static void check_fsr_barriers(void)
{
    vk.CmdPipelineBarrier = record_barriers;
    vk_image_barrier_batch_t batch = { .cmd = HANDLE(VkCommandBuffer, 99) };
    VkImageSubresourceRange range = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .levelCount = 1, .layerCount = 1 };
    for (unsigned i = 0; i < 5; i++)
        assert(vk_image_barrier_batch_add(&batch, HANDLE(VkImage, i + 1), range,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT));
    assert(barrier_calls == 1 && barrier_images == 4 && batch.count == 1);
    vk_image_barrier_batch_submit(batch.cmd, &batch);
    assert(barrier_calls == 2 && barrier_images == 5 && !batch.count);
    vk_image_barrier_batch_submit(batch.cmd, &batch);
    assert(barrier_calls == 2);
    assert(!vk_image_barrier_batch_add(&batch, HANDLE(VkImage, 1), range,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
    puts("FSR barrier contracts: passed (GENERAL dependencies, batch overflow, empty batch)");
}

static void check_fsr_temporal_contracts(void)
{
    _Static_assert(offsetof(vk_fsr_motion_push_t, previous_mvp) == 64, "GLSL layout");
    _Static_assert(offsetof(vk_fsr_motion_push_t, previous_backlerp) == 136, "GLSL layout");
    _Static_assert(offsetof(vk_fsr_motion_push_t, jitter) == 144, "GLSL layout");
    _Static_assert(offsetof(vk_fsr_motion_push_t, viewport) == 160, "GLSL layout");
    _Static_assert(sizeof(vk_fsr_motion_push_t) == 176, "GLSL layout");
    vk.render_extent = (VkExtent2D) { 852, 480 };
    vk.swapchain_extent = (VkExtent2D) { 1280, 720 };
    vk.frame_fsr = true;
    vk.fsr_jitter[0] = 0.25f;
    vk.fsr_jitter[1] = -0.375f;
    refdef_t fd = { .x = 128, .y = 72, .width = 1024, .height = 576 };
    vk_fsr_motion_push_t motion = { 0 };
    vk_fsr_motion_parameters(&motion, &fd);
    assert(motion.viewport[0] == 85 && motion.viewport[1] == 48);
    assert(motion.viewport[2] == 682 && motion.viewport[3] == 384);
    mat4_t jittered, unjittered;
    vk_projection_matrix_internal(jittered, 90, 60, 0, true);
    vk_projection_matrix_internal(unjittered, 90, 60, 0, false);
    /* Scene projection and motion raster coverage must have identical jitter.
     * A point at view-space z=-1 has clip w=1. Velocity excludes this offset. */
    assert(fabsf(-(jittered[8] - unjittered[8]) - motion.jitter[0]) < 1e-6f);
    assert(fabsf(-(jittered[9] - unjittered[9]) - motion.jitter[1]) < 1e-6f);
    assert(fabsf(motion.motion_scale[0] * 2 * vk.render_extent.width - 682) < 1e-4f);
    vk.frame_fsr = false;
    uint64_t samples[90];
    for (unsigned i = 0; i < q_countof(samples); i++)
        samples[i] = i < 9 ? 1 : i >= 81 ? 1000000 : 1000;
    assert(vk_fsr_auto_average(samples, 90) == 1000);
    assert(vk_fsr_auto_is_faster(1000, 950));
    assert(!vk_fsr_auto_is_faster(1000, 951));
    assert(!vk_fsr_auto_is_faster(0, 0));

    cvar_t enabled = { .integer = 1 };
    r_fsr = r_fsr_auto = &enabled;
    vk.fsr_auto_phase = VK_FSR_AUTO_WARMUP;
    vk.fsr_auto_quality = VK_FSR_QUALITY;
    vk.fsr_auto_generation = 7;
    vk.fsr_auto_recreate = false;
    vk.fsr_auto_warmup = 15;
    vk.fsr_auto_samples = 0;
    vk_fsr_auto_update(1000, 2000, 6, true);
    vk_fsr_auto_update(1000, 2000, 7, false);
    assert(vk.fsr_auto_warmup == 15);
    for (unsigned i = 0; i < 105; i++)
        vk_fsr_auto_update(1000, 2000, 7, true);
    assert(vk.fsr_auto_native_usec == 2000);
    assert(vk.fsr_auto_phase == VK_FSR_AUTO_TRIAL);
    assert(vk.fsr_auto_quality == VK_FSR_QUALITY);
    assert(vk.fsr_auto_generation == 8 && vk.fsr_auto_recreate);
    vk.fsr_auto_recreate = false;
    for (unsigned i = 0; i < 105; i++)
        vk_fsr_auto_update(1901, 1000, 8, true);
    assert(vk.fsr_auto_phase == VK_FSR_AUTO_REJECTED);
    assert(vk.fsr_auto_quality == VK_FSR_QUALITY);
    r_fsr = r_fsr_auto = NULL;

    entity_t entity = { .temporal_id = 1, .temporal_generation = 3, .model = 1 };
    vk.fsr_reset = false;
    vk.fsr_previous_fd_valid = true;
    vk.fsr_history_count = 1;
    vk.fsr_history[0] = entity;
    assert(vk_fsr_previous_entity(&entity) == &vk.fsr_history[0]);
    entity.temporal_generation++;
    assert(!vk_fsr_previous_entity(&entity));
    entity.temporal_generation--;
    entity.origin[0] = 129;
    assert(!vk_fsr_previous_entity(&entity));
    entity.origin[0] = 0;
    vk.fsr_history[1] = entity;
    vk.fsr_history_count = 2;
    assert(!vk_fsr_previous_entity(&entity));
    vk.fsr_history_count = 1;
    vk.fsr_reset = true;
    assert(!vk_fsr_previous_entity(&entity));
    puts("FSR temporal contracts: passed (layout, trimmed timing, generation, history)");
}

int main(void)
{
    check_fsr_barriers();
    check_fsr_temporal_contracts();
    paused_cvar.integer = 0;
    assert(!vk_fsr_scene_paused());
    paused_cvar.integer = 1;
    assert(vk_fsr_scene_paused());
    paused_cvar.integer = 2;
    assert(vk_fsr_scene_paused());

    const float scales[] = { 1, 1.5f, 1.7f, 2, 3 };
    for (int bloom = 0; bloom < 2; bloom++) {
        for (size_t i = 0; i < q_countof(scales); i++) {
            check_configuration(2560, 1440, bloom, scales[i]);
            check_configuration(1280, 720, bloom, scales[i]);
        }
    }
    assert(framebuffer_calls == 24 && draw_calls == 48);
    puts("Vulkan presentation contracts: passed (20 configurations + 4 MSAA variants)");
    return 0;
}
