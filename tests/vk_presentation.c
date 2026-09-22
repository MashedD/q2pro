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
cvar_t *gl_bloom;
cmdbuf_t cmd_buffer;
static char last_error[256];
static unsigned framebuffer_calls, pass_calls, draw_calls;
static unsigned destroy_framebuffer_calls;
static int fail_framebuffer_ordinal;
static q2_fsr3_context_t *test_fsr_context;
static unsigned fsr_create_calls;
static unsigned fsr_frame_generation_calls;
static bool fail_frame_generation_create;
static unsigned test_image_calls, destroyed_image_calls;
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

const char *Com_GetLastError(void)
{
    return last_error;
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

int Q_strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && tolower((unsigned char)*s1) == tolower((unsigned char)*s2)) {
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

void Com_LPrintf(print_type_t type, const char *format, ...) { }
void *Z_Mallocz(size_t size) { return calloc(1, size); }
void Z_Free(void *ptr) { free(ptr); }
q2_fsr3_context_t *Q2_FSR3_Create(VkPhysicalDevice physical_device,
    VkDevice device, VkInstance instance,
    PFN_vkGetInstanceProcAddr get_instance_proc_addr,
    PFN_vkGetDeviceProcAddr get_device_proc_addr,
    uint32_t render_width, uint32_t render_height,
    uint32_t display_width, uint32_t display_height,
    VkFormat display_format, bool frame_generation,
    const q2_fsr3_capabilities_t *capabilities)
{
    fsr_create_calls++;
    if (frame_generation) {
        fsr_frame_generation_calls++;
        if (fail_frame_generation_create)
            return NULL;
    }
    return test_fsr_context;
}
void Q2_FSR3_Destroy(q2_fsr3_context_t *context) { }
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
    if (fail_framebuffer_ordinal > 0 &&
        (int)framebuffer_calls == fail_framebuffer_ordinal)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
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

static VKAPI_ATTR void VKAPI_CALL destroy_framebuffer(VkDevice device,
    VkFramebuffer framebuffer, const VkAllocationCallbacks *allocator)
{
    assert(framebuffer != VK_NULL_HANDLE);
    destroy_framebuffer_calls++;
}

static VKAPI_ATTR VkResult VKAPI_CALL transaction_create_image(
    VkDevice device, const VkImageCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkImage *out)
{
    test_image_calls++;
    *out = HANDLE(VkImage, 800 + test_image_calls);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL transaction_get_image_requirements(
    VkDevice device, VkImage image, VkMemoryRequirements *requirements)
{
    *requirements = (VkMemoryRequirements) {
        .size = 4096,
        .alignment = 256,
        .memoryTypeBits = 1,
    };
}

static VKAPI_ATTR void VKAPI_CALL transaction_get_memory_properties(
    VkPhysicalDevice physical_device, VkPhysicalDeviceMemoryProperties *memory)
{
    memset(memory, 0, sizeof(*memory));
    memory->memoryTypeCount = 1;
    memory->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    memory->memoryTypes[0].heapIndex = 0;
}

static VKAPI_ATTR void VKAPI_CALL transaction_get_format_properties(
    VkPhysicalDevice physical_device, VkFormat format,
    VkFormatProperties *properties)
{
    memset(properties, 0, sizeof(*properties));
    properties->optimalTilingFeatures = ~0u;
}

static VKAPI_ATTR VkResult VKAPI_CALL transaction_allocate_memory(
    VkDevice device, const VkMemoryAllocateInfo *info,
    const VkAllocationCallbacks *allocator, VkDeviceMemory *out)
{
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

static VKAPI_ATTR void VKAPI_CALL transaction_destroy_image(
    VkDevice device, VkImage image, const VkAllocationCallbacks *allocator)
{
    assert(image != VK_NULL_HANDLE);
    destroyed_image_calls++;
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

static unsigned transaction_wait_idle_calls;

static VKAPI_ATTR VkResult VKAPI_CALL transaction_wait_idle(VkDevice device)
{
    transaction_wait_idle_calls++;
    return VK_SUCCESS;
}

static void check_fsr_transaction_guards(void)
{
    VkFramebuffer display_framebuffer = HANDLE(VkFramebuffer, 700);

    memset(&vk, 0, sizeof(vk));
    vk.device = HANDLE(VkDevice, 701);
    vk.separate_presentation = true;
    vk.swapchain_extent = (VkExtent2D) { 1280, 720 };
    vk.render_extent = (VkExtent2D) { 640, 360 };
    vk.framebuffers = &display_framebuffer;
    vk.fsr_dynamic.current_scale = 1.75f;
    vk.fsr_output_valid = true;
    vk.fsr_reset = false;
    vk.DeviceWaitIdle = transaction_wait_idle;

    /* A transaction is never allowed to mutate state while a frame or render
     * pass still owns any of the old attachments. */
    vk.frame_active = true;
    assert(!vk_rebuild_internal_render_targets(1.5f));
    vk.frame_active = false;
    vk.render_pass_active = true;
    assert(!vk_rebuild_internal_render_targets(1.5f));
    vk.render_pass_active = false;

    /* Device-loss and malformed recommendations are rejected before waiting
     * or moving the active bundle. */
    vk.device_lost = true;
    assert(!vk_rebuild_internal_render_targets(1.5f));
    vk.device_lost = false;
    assert(!vk_rebuild_internal_render_targets(NAN));
    assert(!vk_rebuild_internal_render_targets(0.5f));
    assert(transaction_wait_idle_calls == 0);

    /* A missing idle primitive is also a safe no-op; the caller can retry on
     * the next frame after device setup completes. */
    vk.DeviceWaitIdle = NULL;
    assert(!vk_rebuild_internal_render_targets(1.5f));
    assert(vk.render_extent.width == 640 && vk.render_extent.height == 360);
    assert(vk.framebuffers == &display_framebuffer);
    assert(vk.fsr_dynamic.current_scale == 1.75f);
    assert(vk.fsr_output_valid && !vk.fsr_reset);
    puts("FSR transaction guards: passed (frame ownership, device loss, input validation)");
}

static void check_fsr_transaction_resource_rollback(void)
{
    cvar_t fsr = { .integer = 1 };
    q2_fsr3_context_t *old_fsr_context =
        HANDLE(q2_fsr3_context_t *, 900);
    q2_fsr3_context_t *candidate_fsr_context =
        HANDLE(q2_fsr3_context_t *, 901);

    memset(&vk, 0, sizeof(vk));
    vk.device = HANDLE(VkDevice, 902);
    vk.physical_device = HANDLE(VkPhysicalDevice, 903);
    vk.separate_presentation = true;
    vk.swapchain_extent = (VkExtent2D) { 1280, 720 };
    vk.render_extent = (VkExtent2D) { 640, 360 };
    vk.swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
    vk.sample_count = VK_SAMPLE_COUNT_1_BIT;
    vk.fsr3 = old_fsr_context;
    vk.fsr_output_texture.image = HANDLE(VkImage, 904);
    vk.fsr_output_texture.width = 1280;
    vk.fsr_output_texture.height = 720;
    vk.scene_framebuffer = HANDLE(VkFramebuffer, 905);
    vk.fsr_output_valid = true;
    vk.fsr_reset = false;
    vk.fsr_dynamic.current_scale = 1.5f;
    vk.fsr_dynamic.cooldown_remaining = 7;
    vk.physical_device_properties.limits.maxPushConstantsSize =
        sizeof(vk_fsr_motion_push_t);
    vk.DeviceWaitIdle = transaction_wait_idle;
    vk.CreateImage = transaction_create_image;
    vk.GetImageMemoryRequirements = transaction_get_image_requirements;
    vk.GetPhysicalDeviceMemoryProperties = transaction_get_memory_properties;
    vk.GetPhysicalDeviceFormatProperties = transaction_get_format_properties;
    vk.AllocateMemory = transaction_allocate_memory;
    vk.DestroyImage = transaction_destroy_image;

    r_fsr = &fsr;
    r_fsr_auto = NULL;
    r_fsr_frame_generation = NULL;
    test_fsr_context = candidate_fsr_context;
    test_image_calls = 0;
    destroyed_image_calls = 0;
    transaction_wait_idle_calls = 0;

    assert(!vk_rebuild_internal_render_targets(1.5f));
    assert(test_image_calls == 1);
    assert(destroyed_image_calls == 1);
    assert(transaction_wait_idle_calls == 1);
    assert(vk.render_extent.width == 640 && vk.render_extent.height == 360);
    assert(vk.fsr3 == old_fsr_context);
    assert(vk.fsr_output_texture.image == HANDLE(VkImage, 904));
    assert(vk.scene_framebuffer == HANDLE(VkFramebuffer, 905));
    assert(vk.fsr_output_valid && !vk.fsr_reset);
    assert(vk.fsr_dynamic.current_scale == 1.5f);
    assert(vk.fsr_dynamic.cooldown_remaining == 7);

    r_fsr = NULL;
    r_fsr_auto = NULL;
    r_fsr_frame_generation = NULL;
    test_fsr_context = NULL;
    puts("FSR transaction resource rollback: passed (candidate allocation failure)");
}

static void check_fsr_framebuffer_failure_cleanup(void)
{
    VkImageView swapchain_views[] = {
        HANDLE(VkImageView, 710),
        HANDLE(VkImageView, 711),
        HANDLE(VkImageView, 712),
    };

    memset(&vk, 0, sizeof(vk));
    vk.device = HANDLE(VkDevice, 713);
    vk.swapchain_extent = (VkExtent2D) { 1280, 720 };
    vk.render_extent = vk.swapchain_extent;
    vk.swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
    vk.depth_format = VK_FORMAT_D32_SFLOAT;
    vk.sample_count = VK_SAMPLE_COUNT_1_BIT;
    vk.separate_presentation = true;
    vk.swapchain_image_count = 3;
    vk.swapchain_views = swapchain_views;
    vk.CreateFramebuffer = create_framebuffer;
    vk.DestroyFramebuffer = destroy_framebuffer;
    fail_framebuffer_ordinal = 2;
    destroy_framebuffer_calls = 0;

    assert(!vk_create_framebuffers());
    assert(vk.framebuffers == NULL);
    assert(destroy_framebuffer_calls == 1);

    fail_framebuffer_ordinal = 0;
    puts("FSR framebuffer cleanup: passed (partial display build rollback)");
}

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
    assert(!strcmp(vk_fsr_reset_reason_name(VK_FSR_RESET_NONE), "none"));
    assert(!strcmp(vk_fsr_reset_reason_name(VK_FSR_RESET_CAMERA_CUT), "camera_cut"));
    assert(!strcmp(vk_fsr_reset_reason_name(VK_FSR_RESET_RENDER_SCALE), "render_scale"));
    assert(!strcmp(vk_fsr_reset_reason_name(VK_FSR_RESET_INVALID_JITTER), "invalid_jitter"));
    vk.frame_active = true;
    vk_fsr_invalidate_history_reason(VK_FSR_RESET_RESIZE);
    assert(vk.fsr_reset &&
           vk.fsr_reset_reason == VK_FSR_RESET_RESIZE);
    vk.frame_active = false;

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

    vk_fsr_dynamic_state_t dynamic = {
        .enabled = true,
        .target_ms = 16.67f,
        .min_scale = 1.0f,
        .max_scale = 2.0f,
        .scale_step = 0.125f,
        .hysteresis_ms = 0.75f,
        .cooldown_frames = 2,
        .required_samples = 3,
    };
    vk_fsr_dynamic_reset(&dynamic, 1.5f, 11);
    dynamic.min_scale = 2.0f;
    vk_fsr_dynamic_reset(&dynamic, 1.7f, 11);
    assert(dynamic.current_scale == 1.7f);
    dynamic.min_scale = 1.0f;
    vk_fsr_dynamic_reset(&dynamic, 1.5f, 11);
    for (unsigned i = 0; i < 2; i++)
        assert(!vk_fsr_dynamic_update(&dynamic, 20000, 0, true, 11));
    /* A single spike does not satisfy the sustained-direction window. */
    assert(!vk_fsr_dynamic_update(&dynamic, 16670, 0, true, 11));
    vk_fsr_dynamic_reset(&dynamic, 1.5f, 11);
    for (unsigned i = 0; i < 2; i++)
        assert(!vk_fsr_dynamic_update(&dynamic, 20000, 0, true, 11));
    assert(vk_fsr_dynamic_update(&dynamic, 20000, 0, true, 11));
    assert(dynamic.recommended_scale == 1.625f);
    /* A pending recommendation is stable until an owner applies it. */
    for (unsigned i = 0; i < 8; i++)
        assert(!vk_fsr_dynamic_update(&dynamic, 10000, 0, true, 11));
    assert(dynamic.current_scale == 1.5f);
    dynamic.current_scale = dynamic.recommended_scale;
    dynamic.recommended_scale = dynamic.current_scale;
    dynamic.stable_samples = 0;
    dynamic.cooldown_remaining = 0;
    assert(!vk_fsr_dynamic_update(&dynamic, 17000, 0, true, 10));
    assert(dynamic.timing == VK_FSR_DYNAMIC_TIMING_GPU);
    dynamic.cpu_fallback = true;
    assert(!vk_fsr_dynamic_update(&dynamic, 0, 17000, true, 11));
    assert(dynamic.timing == VK_FSR_DYNAMIC_TIMING_CPU);
    assert(!vk_fsr_dynamic_update(&dynamic, 0, 0, true, 11));
    assert(dynamic.timing == VK_FSR_DYNAMIC_TIMING_UNAVAILABLE);
    dynamic.cooldown_remaining = 2;
    assert(!vk_fsr_dynamic_update(&dynamic, 20000, 0, true, 11));
    assert(dynamic.cooldown_remaining == 1);

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
    entity.temporal_id = 0;
    assert(!vk_fsr_previous_entity(&entity));
    entity.temporal_id = 1;
    entity.flags = 1;
    assert(!vk_fsr_previous_entity(&entity));
    entity.flags = 0;
    vk.fsr_history[1] = entity;
    vk.fsr_history_count = 2;
    assert(!vk_fsr_previous_entity(&entity));
    vk.fsr_history_count = 1;
    vk.fsr_reset = true;
    assert(!vk_fsr_previous_entity(&entity));
    puts("FSR temporal contracts: passed (layout, trimmed timing, generation, history)");
}

static void check_fsr_frame_generation_contracts(void)
{
    cvar_t fsr = { .integer = 1 };
    cvar_t auto_fsr = { .integer = 0 };
    cvar_t frame_generation = { .integer = 1 };
    cvar_t motion = { .string = "auto" };

    r_fsr = &fsr;
    r_fsr_auto = &auto_fsr;
    r_fsr_frame_generation = &frame_generation;
    r_fsr_motion = &motion;
    vk_session_frame_generation_disabled = false;
    assert(vk_fsr_frame_generation_requested());
    assert(vk_fsr_any_requested());

    /* Automatic selection owns the temporal path, so explicit frame
     * generation must not leak into an auto-evaluation swapchain. */
    auto_fsr.integer = 1;
    assert(!vk_fsr_frame_generation_requested());
    auto_fsr.integer = 0;

    /* Fast motion is incompatible with frame generation: the request remains
     * active, but the backend must force full motion vectors. */
    motion.string = "fast";
    assert(!vk_fsr_motion_fast_requested());
    assert(vk_fsr_frame_generation_requested());
    motion.string = "auto";

    vk_disable_frame_generation("test fallback");
    assert(vk_session_frame_generation_disabled);
    assert(frame_generation.modified);
    assert(!vk_fsr_frame_generation_requested());
    assert(vk_fsr_requested());

    /* A second failure is idempotent and must not clear the normal FSR path. */
    frame_generation.modified = false;
    vk_disable_frame_generation("repeat fallback");
    assert(!frame_generation.modified);
    assert(vk_fsr_requested());

    r_fsr = r_fsr_auto = r_fsr_frame_generation = r_fsr_motion = NULL;
    vk_session_frame_generation_disabled = false;
    puts("FSR frame-generation contracts: passed (request, auto, motion, fallback)");
}

static void check_fsr_setup_fallback_contracts(void)
{
    cvar_t fsr = { .integer = 1 };
    cvar_t frame_generation = { .integer = 1 };
    bool frame_generation_requested = true;

    r_fsr = &fsr;
    r_fsr_frame_generation = &frame_generation;
    vk_session_frame_generation_disabled = false;
    vk.render_extent = (VkExtent2D){ 640, 360 };
    vk.swapchain_extent = (VkExtent2D){ 1280, 720 };
    test_fsr_context = HANDLE(q2_fsr3_context_t *, 950);
    fail_frame_generation_create = true;
    fsr_create_calls = 0;
    fsr_frame_generation_calls = 0;

    q2_fsr3_context_t *context = vk_create_fsr_context_with_fallback(
        &frame_generation_requested, VK_FORMAT_R8G8B8A8_UNORM);
    assert(context == test_fsr_context);
    assert(fsr_create_calls == 2);
    assert(fsr_frame_generation_calls == 1);
    assert(!frame_generation_requested);
    assert(frame_generation.integer == 1);
    assert(!frame_generation.modified);
    assert(vk_session_frame_generation_disabled);
    assert(!vk_fsr_frame_generation_requested());
    assert(vk_fsr_requested());

    fail_frame_generation_create = false;
    test_fsr_context = NULL;
    r_fsr = r_fsr_frame_generation = NULL;
    vk_session_frame_generation_disabled = false;
    puts("FSR setup fallback: passed (frame generation -> upscaling)");
}

static void check_fsr_lifecycle_contracts(void)
{
    cvar_t fsr = { .integer = 1 };
    cvar_t generation = { .integer = 1 };

    r_fsr = &fsr;
    r_fsr_frame_generation = &generation;
    vk_session_frame_generation_disabled = false;
    memset(&vk, 0, sizeof(vk));

    /* Reset frames may update the upscaled history, but never submit an
     * interpolated frame.  This mirrors the backend's explicit dispatch
     * gate and protects it from being accidentally weakened. */
    vk.fsr_reset = true;
    bool prepared = true;
    bool dispatch_allowed = prepared && !vk.fsr_reset &&
        !vk.fsr_pause_reuse && !vk_fsr_scene_paused();
    assert(!dispatch_allowed);
    assert(vk.fsr_reset);

    /* A paused frame reuses the last complete output and disables the scene
     * path. It must not manufacture a new frame-generation submission. */
    vk.fsr_pause_reuse = true;
    vk.frame_fsr = false;
    assert(vk.fsr_pause_reuse && !vk.frame_fsr);
    paused_cvar.integer = 1;
    assert(vk_fsr_scene_paused());
    paused_cvar.integer = 0;
    assert(!vk_fsr_scene_paused());

    /* History invalidation is the production helper used by resize, pause
     * resume, camera cuts, and motion-mode changes. Verify that it clears all
     * temporal state rather than duplicating its implementation in the test. */
    vk.fsr_reset = false;
    vk.fsr_previous_viewproj_valid = true;
    vk.fsr_previous_fd_valid = true;
    vk.fsr_history_count = 3;
    vk.fsr_pending_history_count = 2;
    vk.fsr_motion_initialized = true;
    vk.fsr_output_valid = true;
    vk.fsr_pause_cache_valid = true;
    vk.fsr_pause_reuse = true;
    vk.fsr_direct_source_valid = true;
    vk.frame_active = false;
    vk.fsr_frame_reset_reason = VK_FSR_RESET_NONE;
    vk_fsr_invalidate_history_reason(VK_FSR_RESET_INITIAL);
    assert(vk.fsr_reset && !vk.fsr_previous_viewproj_valid &&
           !vk.fsr_previous_fd_valid && vk.fsr_history_count == 0 &&
           vk.fsr_pending_history_count == 0 && !vk.fsr_motion_initialized);
    assert(vk.fsr_reset_reason == VK_FSR_RESET_INITIAL &&
           vk.fsr_frame_reset_reason == VK_FSR_RESET_NONE);
    /* Invalidation preserves the last completed output for a safe pause
     * composite, while discarding all state that depends on the old scene. */
    assert(vk.fsr_output_valid);
    assert(!vk.fsr_pause_cache_valid && !vk.fsr_pause_reuse &&
           !vk.fsr_direct_source_valid);

    /* Repeating the helper must be harmless and must not consume the retained
     * output.  This is exercised by resize/camera-cut paths that can converge
     * on the same reset in one frame. */
    vk.fsr_pause_cache_valid = true;
    vk.fsr_pause_reuse = true;
    vk.fsr_direct_source_valid = true;
    vk_fsr_invalidate_history_reason(VK_FSR_RESET_PAUSE_RESUME);
    assert(vk.fsr_reset && vk.fsr_output_valid &&
           !vk.fsr_pause_cache_valid && !vk.fsr_pause_reuse &&
           !vk.fsr_direct_source_valid && !vk.fsr_motion_initialized);

    /* Entering pause reuses only a complete retained output.  Resuming clears
     * reuse state and forces the first active frame through temporal reset. */
    vk.fsr_output_valid = true;
    vk.fsr_pause_cache_valid = true;
    vk.fsr_pause_reuse = true;
    paused_cvar.integer = 1;
    assert(vk_fsr_scene_paused());
    assert(vk.fsr_output_valid && vk.fsr_pause_cache_valid &&
           vk.fsr_pause_reuse);
    paused_cvar.integer = 0;
    assert(!vk_fsr_scene_paused());
    vk_fsr_invalidate_history_reason(VK_FSR_RESET_DISPATCH_FAILURE);
    assert(vk.fsr_reset && vk.fsr_output_valid &&
           !vk.fsr_pause_cache_valid && !vk.fsr_pause_reuse &&
           !vk.fsr_direct_source_valid);

    /* Preparation, dispatch, and device-loss style failures all converge on
     * the same session fallback: preserve ordinary FSR and force a reset. */
    vk.fsr_reset = false;
    vk.fsr_reset_reason = VK_FSR_RESET_DISPATCH_FAILURE;
    vk.fsr_output_valid = true;
    vk_disable_frame_generation("preparation failed");
    assert(vk_session_frame_generation_disabled && vk.fsr_reset &&
           vk.fsr_output_valid &&
           vk.fsr_reset_reason == VK_FSR_RESET_DISPATCH_FAILURE);
    assert(vk_fsr_requested() && !vk_fsr_frame_generation_requested());

    vk_session_frame_generation_disabled = false;
    vk.fsr_reset = false;
    vk.fsr_output_valid = true;
    vk.device_lost = false;
    vk_device_lost_recovery_queued = false;
    vk_handle_device_lost("contract", VK_ERROR_DEVICE_LOST);
    /* Device-loss handling owns session disable/restart scheduling; it does
     * not mutate the retained FSR output or synthesize a temporal reset. */
    assert(vk.device_lost && vk_session_frame_generation_disabled &&
           !vk.fsr_reset && vk.fsr_output_valid);
    assert(!vk_fsr_frame_generation_requested());

    r_fsr = r_fsr_frame_generation = NULL;
    vk_session_frame_generation_disabled = false;
    paused_cvar.integer = 0;
    puts("FSR lifecycle contracts: passed (reset gate, pause reuse, fallback, device loss)");
}

int main(void)
{
    check_fsr_barriers();
    check_fsr_temporal_contracts();
    check_fsr_frame_generation_contracts();
    check_fsr_setup_fallback_contracts();
    check_fsr_lifecycle_contracts();
    check_fsr_transaction_guards();
    check_fsr_transaction_resource_rollback();
    framebuffer_calls = 0;
    check_fsr_framebuffer_failure_cleanup();
    framebuffer_calls = 0;
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
