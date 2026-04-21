/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "shared/shared.h"
#include "common/bsp.h"
#include "common/common.h"
#include "common/zone.h"
#include "client/client.h"
#include "client/video.h"
#include "images.h"
#include "refresh/refresh.h"
#include "system/system.h"
#include "vk_backend.h"

#if USE_VULKAN

#define VK_MAX_INSTANCE_EXTENSIONS 16

static const uint32_t vk_rect_vert_spv[] =
#include "vk_rect_vert_spv.h"
;

static const uint32_t vk_rect_frag_spv[] =
#include "vk_rect_frag_spv.h"
;

static const uint32_t vk_tex_vert_spv[] =
#include "vk_tex_vert_spv.h"
;

static const uint32_t vk_tex_frag_spv[] =
#include "vk_tex_frag_spv.h"
;

static const uint32_t vk_color3d_vert_spv[] =
#include "vk_color3d_vert_spv.h"
;

static const uint32_t vk_color3d_frag_spv[] =
#include "vk_color3d_frag_spv.h"
;

static const uint32_t vk_world_vert_spv[] =
#include "vk_world_vert_spv.h"
;

static const uint32_t vk_world_frag_spv[] =
#include "vk_world_frag_spv.h"
;

typedef struct {
    float rect[4];
    float color[4];
    float screen[2];
    float uv[4];
} vk_draw_push_t;

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkDescriptorSet descriptor_set;
    uint32_t width;
    uint32_t height;
} vk_texture_t;

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
} vk_buffer_t;

typedef struct {
    float position[3];
    float color[4];
    float uv[2];
} vk_vertex_t;

typedef struct {
    vk_buffer_t vertices;
    vk_buffer_t indices;
    uint32_t index_count;
} vk_mesh_t;

typedef struct {
    uint32_t first_index;
    uint32_t index_count;
    uint32_t texture_index;
} vk_world_batch_t;

typedef struct {
    uint32_t first_vertex;
    uint32_t edge_count;
    uint32_t texture_index;
} vk_world_face_t;

typedef struct {
    bsp_t *cache;
    vk_mesh_t mesh;
    vk_world_batch_t *batches;
    uint32_t batch_count;
} vk_world_t;

typedef struct {
    float rect[4];
    float color[4];
    float screen[2];
} vk_rect_push_t;

typedef struct {
    mat4_t mvp;
    float color[4];
} vk_color3d_push_t;

typedef struct {
    uint32_t graphics_family;
    uint32_t present_family;
    bool has_graphics;
    bool has_present;
} vk_queue_families_t;

typedef struct {
    void *library;

    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
    PFN_vkCreateInstance CreateInstance;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;

    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkCreateDevice CreateDevice;
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
    PFN_vkGetPhysicalDeviceFormatProperties GetPhysicalDeviceFormatProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR;
    PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties;

    PFN_vkDestroyDevice DestroyDevice;
    PFN_vkGetDeviceQueue GetDeviceQueue;
    PFN_vkDeviceWaitIdle DeviceWaitIdle;
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR;
    PFN_vkCreateImageView CreateImageView;
    PFN_vkDestroyImageView DestroyImageView;
    PFN_vkCreateRenderPass CreateRenderPass;
    PFN_vkDestroyRenderPass DestroyRenderPass;
    PFN_vkCreateFramebuffer CreateFramebuffer;
    PFN_vkDestroyFramebuffer DestroyFramebuffer;
    PFN_vkCreateShaderModule CreateShaderModule;
    PFN_vkDestroyShaderModule DestroyShaderModule;
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool CreateDescriptorPool;
    PFN_vkDestroyDescriptorPool DestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets;
    PFN_vkFreeDescriptorSets FreeDescriptorSets;
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets;
    PFN_vkCreateSampler CreateSampler;
    PFN_vkDestroySampler DestroySampler;
    PFN_vkCreatePipelineLayout CreatePipelineLayout;
    PFN_vkDestroyPipelineLayout DestroyPipelineLayout;
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines;
    PFN_vkDestroyPipeline DestroyPipeline;
    PFN_vkCreateCommandPool CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkFreeCommandBuffers FreeCommandBuffers;
    PFN_vkResetCommandBuffer ResetCommandBuffer;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkCmdBeginRenderPass CmdBeginRenderPass;
    PFN_vkCmdEndRenderPass CmdEndRenderPass;
    PFN_vkCmdClearAttachments CmdClearAttachments;
    PFN_vkCmdBindPipeline CmdBindPipeline;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets;
    PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers;
    PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer;
    PFN_vkCmdPushConstants CmdPushConstants;
    PFN_vkCmdDraw CmdDraw;
    PFN_vkCmdDrawIndexed CmdDrawIndexed;
    PFN_vkCreateBuffer CreateBuffer;
    PFN_vkDestroyBuffer DestroyBuffer;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkCreateImage CreateImage;
    PFN_vkDestroyImage DestroyImage;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;
    PFN_vkAllocateMemory AllocateMemory;
    PFN_vkFreeMemory FreeMemory;
    PFN_vkBindBufferMemory BindBufferMemory;
    PFN_vkBindImageMemory BindImageMemory;
    PFN_vkMapMemory MapMemory;
    PFN_vkUnmapMemory UnmapMemory;
    PFN_vkCmdCopyBuffer CmdCopyBuffer;
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage;
    PFN_vkCreateSemaphore CreateSemaphore;
    PFN_vkDestroySemaphore DestroySemaphore;
    PFN_vkCreateFence CreateFence;
    PFN_vkDestroyFence DestroyFence;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkResetFences ResetFences;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkQueueWaitIdle QueueWaitIdle;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR;
    PFN_vkQueuePresentKHR QueuePresentKHR;

    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkCommandPool command_pool;
    VkDescriptorSetLayout texture_set_layout;
    VkDescriptorPool descriptor_pool;
    VkSampler sampler;
    VkPipelineLayout rect_pipeline_layout;
    VkPipeline rect_pipeline;
    VkPipeline texture_pipeline;
    VkPipeline color3d_pipeline;
    VkPipeline world_pipeline;
    VkSwapchainKHR swapchain;
    VkRenderPass render_pass;
    VkFormat swapchain_format;
    VkFormat depth_format;
    VkExtent2D swapchain_extent;
    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;
    VkImage *swapchain_images;
    VkImageView *swapchain_views;
    VkFramebuffer *framebuffers;
    VkImageLayout *swapchain_layouts;
    VkCommandBuffer *command_buffers;
    uint32_t swapchain_image_count;
    vk_queue_families_t queues;
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence frame_fence;
    uint32_t current_image;
    bool frame_active;
    bool render_pass_active;
    float scale;
    color_t color;
    bool color_set;
    clipRect_t clip;
    bool clip_set;
    vk_texture_t raw_texture;
    vk_mesh_t test_triangle;
    vk_world_t world;
    vk_texture_t textures[MAX_RIMAGES];
} vk_state_t;

static vk_state_t vk;
static cvar_t *vk_show_test_triangle;
static cvar_t *vk_world_textures;

static bool vk_upload_texture(image_t *image, byte *pic);
static void vk_destroy_texture(image_t *image);
static bool vk_upload_texture_data(vk_texture_t *texture, uint32_t width,
                                   uint32_t height, const void *pixels);
static void vk_destroy_texture_resource(vk_texture_t *texture);
static bool vk_create_swapchain(int width, int height);
static bool vk_create_test_triangle(void);
static void vk_destroy_mesh(vk_mesh_t *mesh);
static void vk_free_world(void);
static void vk_load_world(const char *name);

static void vk_upload_image(image_t *image, byte *pic)
{
    if (!vk_upload_texture(image, pic)) {
        Com_WPrintf("Couldn't upload Vulkan texture %s: %s\n",
                    image->name, Com_GetLastError());
    }
}

static void vk_unload_image(image_t *image)
{
    vk_destroy_texture(image);
}

static const image_upload_t vk_image_upload = {
    .load = vk_upload_image,
    .unload = vk_unload_image,
};

static bool vk_fail_result(const char *what, VkResult result)
{
    Com_SetLastError(va("%s failed: Vulkan error %d", what, result));
    return false;
}

static uint32_t vk_find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory;

    vk.GetPhysicalDeviceMemoryProperties(vk.physical_device, &memory);

    for (uint32_t i = 0; i < memory.memoryTypeCount; i++) {
        if ((type_bits & BIT(i)) &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return UINT32_MAX;
}

static bool vk_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags properties,
                             VkBuffer *buffer, VkDeviceMemory *memory)
{
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult result = vk.CreateBuffer(vk.device, &buffer_info, NULL, buffer);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateBuffer", result);

    VkMemoryRequirements req;
    vk.GetBufferMemoryRequirements(vk.device, *buffer, &req);

    uint32_t memory_type = vk_find_memory_type(req.memoryTypeBits, properties);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("No suitable Vulkan buffer memory type");
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_type,
    };
    result = vk.AllocateMemory(vk.device, &alloc_info, NULL, memory);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateMemory", result);

    result = vk.BindBufferMemory(vk.device, *buffer, *memory, 0);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkBindBufferMemory", result);

    return true;
}

static bool vk_create_texture_image(uint32_t width, uint32_t height,
                                    VkImage *image, VkDeviceMemory *memory)
{
#if USE_BGRA
    const VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
#else
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
#endif

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult result = vk.CreateImage(vk.device, &image_info, NULL, image);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImage", result);

    VkMemoryRequirements req;
    vk.GetImageMemoryRequirements(vk.device, *image, &req);

    uint32_t memory_type = vk_find_memory_type(req.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("No suitable Vulkan image memory type");
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_type,
    };
    result = vk.AllocateMemory(vk.device, &alloc_info, NULL, memory);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateMemory", result);

    result = vk.BindImageMemory(vk.device, *image, *memory, 0);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkBindImageMemory", result);

    return true;
}

static VkFormat vk_choose_depth_format(void)
{
    static const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM,
    };

    for (size_t i = 0; i < q_countof(candidates); i++) {
        VkFormatProperties props;

        vk.GetPhysicalDeviceFormatProperties(vk.physical_device,
                                             candidates[i], &props);
        if (props.optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return candidates[i];
        }
    }

    return VK_FORMAT_UNDEFINED;
}

static bool vk_begin_immediate(VkCommandBuffer *cmd)
{
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkResult result = vk.AllocateCommandBuffers(vk.device, &alloc_info, cmd);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateCommandBuffers", result);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    result = vk.BeginCommandBuffer(*cmd, &begin_info);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkBeginCommandBuffer", result);

    return true;
}

static bool vk_end_immediate(VkCommandBuffer cmd)
{
    VkResult result = vk.EndCommandBuffer(cmd);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkEndCommandBuffer", result);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    result = vk.QueueSubmit(vk.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkQueueSubmit", result);

    result = vk.QueueWaitIdle(vk.graphics_queue);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkQueueWaitIdle", result);

    vk.FreeCommandBuffers(vk.device, vk.command_pool, 1, &cmd);
    return true;
}

static void vk_destroy_buffer(vk_buffer_t *buffer)
{
    if (!vk.device)
        return;

    if (buffer->buffer)
        vk.DestroyBuffer(vk.device, buffer->buffer, NULL);
    if (buffer->memory)
        vk.FreeMemory(vk.device, buffer->memory, NULL);

    memset(buffer, 0, sizeof(*buffer));
}

static bool vk_upload_buffer(vk_buffer_t *dst, const void *data,
                             VkDeviceSize size, VkBufferUsageFlags usage)
{
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkBuffer device_buffer = VK_NULL_HANDLE;
    VkDeviceMemory device_memory = VK_NULL_HANDLE;

    if (!data || !size)
        return true;

    if (!vk_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &staging, &staging_memory))
        goto fail;

    void *mapped;
    VkResult result = vk.MapMemory(vk.device, staging_memory, 0, size, 0, &mapped);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkMapMemory", result);
        goto fail;
    }
    memcpy(mapped, data, size);
    vk.UnmapMemory(vk.device, staging_memory);

    if (!vk_create_buffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &device_buffer, &device_memory))
        goto fail;

    VkCommandBuffer cmd;
    if (!vk_begin_immediate(&cmd))
        goto fail;

    VkBufferCopy copy = {
        .size = size,
    };
    vk.CmdCopyBuffer(cmd, staging, device_buffer, 1, &copy);

    if (!vk_end_immediate(cmd))
        goto fail;

    vk_destroy_buffer(dst);
    dst->buffer = device_buffer;
    dst->memory = device_memory;
    dst->size = size;

    vk.DestroyBuffer(vk.device, staging, NULL);
    vk.FreeMemory(vk.device, staging_memory, NULL);
    return true;

fail:
    if (staging)
        vk.DestroyBuffer(vk.device, staging, NULL);
    if (staging_memory)
        vk.FreeMemory(vk.device, staging_memory, NULL);
    if (device_buffer)
        vk.DestroyBuffer(vk.device, device_buffer, NULL);
    if (device_memory)
        vk.FreeMemory(vk.device, device_memory, NULL);
    return false;
}

static void vk_destroy_mesh(vk_mesh_t *mesh)
{
    vk_destroy_buffer(&mesh->vertices);
    vk_destroy_buffer(&mesh->indices);
    mesh->index_count = 0;
}

static void vk_free_world(void)
{
    vk_destroy_mesh(&vk.world.mesh);
    if (vk.world.batches) {
        Z_Free(vk.world.batches);
        vk.world.batches = NULL;
    }
    vk.world.batch_count = 0;

    if (vk.world.cache) {
        BSP_Free(vk.world.cache);
        vk.world.cache = NULL;
    }
}

static bool vk_upload_mesh(vk_mesh_t *mesh, const vk_vertex_t *vertices,
                           uint32_t vertex_count, const uint32_t *indices,
                           uint32_t index_count)
{
    vk_mesh_t uploaded = { 0 };

    if (!vertex_count || !index_count)
        return true;

    if (!vk_upload_buffer(&uploaded.vertices, vertices,
                          sizeof(*vertices) * vertex_count,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
        goto fail;
    if (!vk_upload_buffer(&uploaded.indices, indices,
                          sizeof(*indices) * index_count,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
        goto fail;

    uploaded.index_count = index_count;
    vk_destroy_mesh(mesh);
    *mesh = uploaded;
    return true;

fail:
    vk_destroy_mesh(&uploaded);
    return false;
}

static void vk_texture_barrier(VkCommandBuffer cmd, VkImage image,
                               VkImageLayout old_layout, VkImageLayout new_layout,
                               VkAccessFlags src_access, VkAccessFlags dst_access,
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

    vk.CmdPipelineBarrier(cmd, src_stage, dst_stage,
                          0, 0, NULL, 0, NULL, 1, &barrier);
}

static bool vk_upload_texture_data(vk_texture_t *texture, uint32_t width,
                                   uint32_t height, const void *pixels)
{
    VkDeviceSize upload_size = (VkDeviceSize)width * height * 4;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;

    if (!pixels || !width || !height)
        return true;

    if (!vk_create_buffer(upload_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &staging, &staging_memory))
        goto fail;

    void *mapped;
    VkResult result = vk.MapMemory(vk.device, staging_memory, 0, upload_size, 0, &mapped);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkMapMemory", result);
        goto fail;
    }
    memcpy(mapped, pixels, upload_size);
    vk.UnmapMemory(vk.device, staging_memory);

    vk_destroy_texture_resource(texture);

    if (!vk_create_texture_image(width, height,
                                 &texture->image, &texture->memory))
        goto fail;

    VkCommandBuffer cmd;
    if (!vk_begin_immediate(&cmd))
        goto fail;

    vk_texture_barrier(cmd, texture->image,
                       VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       0,
                       VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copy = {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageExtent = {
            .width = width,
            .height = height,
            .depth = 1,
        },
    };
    vk.CmdCopyBufferToImage(cmd, staging, texture->image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    vk_texture_barrier(cmd, texture->image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_ACCESS_SHADER_READ_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    if (!vk_end_immediate(cmd))
        goto fail;

#if USE_BGRA
    const VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
#else
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
#endif
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    result = vk.CreateImageView(vk.device, &view_info, NULL, &texture->view);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkCreateImageView", result);
        goto fail;
    }

    VkDescriptorSetAllocateInfo descriptor_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk.descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vk.texture_set_layout,
    };
    result = vk.AllocateDescriptorSets(vk.device, &descriptor_info,
                                       &texture->descriptor_set);
    if (result != VK_SUCCESS) {
        vk_fail_result("vkAllocateDescriptorSets", result);
        goto fail;
    }

    VkDescriptorImageInfo image_info = {
        .sampler = vk.sampler,
        .imageView = texture->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = texture->descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
    };
    vk.UpdateDescriptorSets(vk.device, 1, &write, 0, NULL);

    texture->width = width;
    texture->height = height;

    if (staging)
        vk.DestroyBuffer(vk.device, staging, NULL);
    if (staging_memory)
        vk.FreeMemory(vk.device, staging_memory, NULL);
    return true;

fail:
    if (staging)
        vk.DestroyBuffer(vk.device, staging, NULL);
    if (staging_memory)
        vk.FreeMemory(vk.device, staging_memory, NULL);
    vk_destroy_texture_resource(texture);
    return false;
}

static bool vk_upload_texture(image_t *image, byte *pic)
{
    uintptr_t first = (uintptr_t)r_images;
    uintptr_t last = (uintptr_t)(r_images + MAX_RIMAGES);
    uintptr_t ptr = (uintptr_t)image;
    if (ptr < first || ptr >= last || !pic)
        return true;

    uint32_t index = image - r_images;
    vk_texture_t *texture = &vk.textures[index];

    if (!vk_upload_texture_data(texture, image->upload_width,
                                image->upload_height, pic)) {
        image->texnum = 0;
        return false;
    }

    image->texnum = index;
    image->sl = 0;
    image->sh = 1;
    image->tl = 0;
    image->th = 1;

    return true;
}

static void vk_destroy_texture_resource(vk_texture_t *texture)
{
    if (!vk.device)
        return;

    if (!texture->image && !texture->view && !texture->memory)
        return;

    if (vk.DeviceWaitIdle)
        vk.DeviceWaitIdle(vk.device);

    if (texture->descriptor_set && vk.descriptor_pool)
        vk.FreeDescriptorSets(vk.device, vk.descriptor_pool, 1,
                              &texture->descriptor_set);
    if (texture->view)
        vk.DestroyImageView(vk.device, texture->view, NULL);
    if (texture->image)
        vk.DestroyImage(vk.device, texture->image, NULL);
    if (texture->memory)
        vk.FreeMemory(vk.device, texture->memory, NULL);

    memset(texture, 0, sizeof(*texture));
}

static void vk_destroy_texture(image_t *image)
{
    unsigned index = image->texnum;
    if (!index || index >= MAX_RIMAGES)
        return;

    vk_destroy_texture_resource(&vk.textures[index]);
    image->texnum = image->texnum2 = 0;
}

static bool vk_load_global(void)
{
#ifdef _WIN32
    const char *libname = "vulkan-1.dll";
#else
    const char *libname = "libvulkan.so.1";
#endif

    vk.GetInstanceProcAddr = Sys_LoadLibrary(libname, "vkGetInstanceProcAddr", &vk.library);
    if (!vk.GetInstanceProcAddr) {
        Com_SetLastError(va("Couldn't load Vulkan loader %s: %s", libname, Com_GetLastError()));
        return false;
    }

    vk.CreateInstance = (PFN_vkCreateInstance)
        vk.GetInstanceProcAddr(NULL, "vkCreateInstance");
    vk.EnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)
        vk.GetInstanceProcAddr(NULL, "vkEnumerateInstanceExtensionProperties");

    if (!vk.CreateInstance || !vk.EnumerateInstanceExtensionProperties) {
        Com_SetLastError("Vulkan loader is missing required global entry points");
        return false;
    }

    return true;
}

static bool vk_load_instance(void)
{
#define LOAD(name) \
    do { \
        vk.name = (PFN_vk##name)vk.GetInstanceProcAddr(vk.instance, "vk" #name); \
        if (!vk.name) { \
            Com_SetLastError("Vulkan instance is missing vk" #name); \
            return false; \
        } \
    } while (0)

    LOAD(DestroyInstance);
    LOAD(CreateDevice);
    LOAD(GetDeviceProcAddr);
    LOAD(DestroySurfaceKHR);
    LOAD(EnumeratePhysicalDevices);
    LOAD(GetPhysicalDeviceProperties);
    LOAD(GetPhysicalDeviceMemoryProperties);
    LOAD(GetPhysicalDeviceFormatProperties);
    LOAD(GetPhysicalDeviceQueueFamilyProperties);
    LOAD(GetPhysicalDeviceSurfaceSupportKHR);
    LOAD(GetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD(GetPhysicalDeviceSurfaceFormatsKHR);
    LOAD(GetPhysicalDeviceSurfacePresentModesKHR);
    LOAD(EnumerateDeviceExtensionProperties);

#undef LOAD

    return true;
}

static bool vk_load_device(void)
{
#define LOAD(name) \
    do { \
        vk.name = (PFN_vk##name)vk.GetDeviceProcAddr(vk.device, "vk" #name); \
        if (!vk.name) { \
            Com_SetLastError("Vulkan device is missing vk" #name); \
            return false; \
        } \
    } while (0)

    LOAD(DestroyDevice);
    LOAD(GetDeviceQueue);
    LOAD(DeviceWaitIdle);
    LOAD(CreateSwapchainKHR);
    LOAD(DestroySwapchainKHR);
    LOAD(GetSwapchainImagesKHR);
    LOAD(CreateImageView);
    LOAD(DestroyImageView);
    LOAD(CreateRenderPass);
    LOAD(DestroyRenderPass);
    LOAD(CreateFramebuffer);
    LOAD(DestroyFramebuffer);
    LOAD(CreateShaderModule);
    LOAD(DestroyShaderModule);
    LOAD(CreateDescriptorSetLayout);
    LOAD(DestroyDescriptorSetLayout);
    LOAD(CreateDescriptorPool);
    LOAD(DestroyDescriptorPool);
    LOAD(AllocateDescriptorSets);
    LOAD(FreeDescriptorSets);
    LOAD(UpdateDescriptorSets);
    LOAD(CreateSampler);
    LOAD(DestroySampler);
    LOAD(CreatePipelineLayout);
    LOAD(DestroyPipelineLayout);
    LOAD(CreateGraphicsPipelines);
    LOAD(DestroyPipeline);
    LOAD(CreateCommandPool);
    LOAD(DestroyCommandPool);
    LOAD(AllocateCommandBuffers);
    LOAD(FreeCommandBuffers);
    LOAD(ResetCommandBuffer);
    LOAD(BeginCommandBuffer);
    LOAD(EndCommandBuffer);
    LOAD(CmdPipelineBarrier);
    LOAD(CmdBeginRenderPass);
    LOAD(CmdEndRenderPass);
    LOAD(CmdClearAttachments);
    LOAD(CmdBindPipeline);
    LOAD(CmdBindDescriptorSets);
    LOAD(CmdBindVertexBuffers);
    LOAD(CmdBindIndexBuffer);
    LOAD(CmdPushConstants);
    LOAD(CmdDraw);
    LOAD(CmdDrawIndexed);
    LOAD(CreateBuffer);
    LOAD(DestroyBuffer);
    LOAD(GetBufferMemoryRequirements);
    LOAD(CreateImage);
    LOAD(DestroyImage);
    LOAD(GetImageMemoryRequirements);
    LOAD(AllocateMemory);
    LOAD(FreeMemory);
    LOAD(BindBufferMemory);
    LOAD(BindImageMemory);
    LOAD(MapMemory);
    LOAD(UnmapMemory);
    LOAD(CmdCopyBuffer);
    LOAD(CmdCopyBufferToImage);
    LOAD(CreateSemaphore);
    LOAD(DestroySemaphore);
    LOAD(CreateFence);
    LOAD(DestroyFence);
    LOAD(WaitForFences);
    LOAD(ResetFences);
    LOAD(QueueSubmit);
    LOAD(QueueWaitIdle);
    LOAD(AcquireNextImageKHR);
    LOAD(QueuePresentKHR);

#undef LOAD

    return true;
}

static bool vk_create_instance(void)
{
    const char *extensions[VK_MAX_INSTANCE_EXTENSIONS];
    uint32_t extension_count = 0;
    VkResult result;

    if (!vid->get_vk_instance_extensions ||
        !vid->get_vk_instance_extensions(&extension_count, extensions, q_countof(extensions))) {
        Com_SetLastError("Video driver can't provide Vulkan instance extensions");
        return false;
    }

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = PRODUCT,
        .applicationVersion = VK_MAKE_VERSION(REVISION, 0, 0),
        .pEngineName = PRODUCT,
        .engineVersion = VK_MAKE_VERSION(REVISION, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
    };

    result = vk.CreateInstance(&create_info, NULL, &vk.instance);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateInstance", result);

    return vk_load_instance();
}

static bool vk_create_surface(void)
{
    if (!vid->create_vk_surface) {
        Com_SetLastError("Video driver can't create a Vulkan surface");
        return false;
    }

    return vid->create_vk_surface(vk.instance, &vk.surface);
}

static bool vk_has_swapchain_extension(VkPhysicalDevice device)
{
    uint32_t count = 0;
    VkResult result;

    result = vk.EnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    if (result != VK_SUCCESS || !count)
        return false;

    VkExtensionProperties *extensions = Z_Malloc(sizeof(*extensions) * count);
    result = vk.EnumerateDeviceExtensionProperties(device, NULL, &count, extensions);
    if (result != VK_SUCCESS) {
        Z_Free(extensions);
        return false;
    }

    bool found = false;
    for (uint32_t i = 0; i < count; i++) {
        if (!strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            found = true;
            break;
        }
    }

    Z_Free(extensions);
    return found;
}

static vk_queue_families_t vk_find_queues(VkPhysicalDevice device)
{
    vk_queue_families_t found = { 0 };
    uint32_t count = 0;

    vk.GetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    if (!count)
        return found;

    VkQueueFamilyProperties *families = Z_Malloc(sizeof(*families) * count);
    vk.GetPhysicalDeviceQueueFamilyProperties(device, &count, families);

    for (uint32_t i = 0; i < count; i++) {
        VkBool32 present = VK_FALSE;

        if (!found.has_graphics && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            found.graphics_family = i;
            found.has_graphics = true;
        }

        if (vk.GetPhysicalDeviceSurfaceSupportKHR(device, i, vk.surface, &present) == VK_SUCCESS &&
            present && !found.has_present) {
            found.present_family = i;
            found.has_present = true;
        }

        if (found.has_graphics && found.has_present)
            break;
    }

    Z_Free(families);
    return found;
}

static bool vk_pick_physical_device(void)
{
    uint32_t count = 0;
    VkResult result;

    result = vk.EnumeratePhysicalDevices(vk.instance, &count, NULL);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkEnumeratePhysicalDevices", result);
    if (!count) {
        Com_SetLastError("No Vulkan physical devices found");
        return false;
    }

    VkPhysicalDevice *devices = Z_Malloc(sizeof(*devices) * count);
    result = vk.EnumeratePhysicalDevices(vk.instance, &count, devices);
    if (result != VK_SUCCESS) {
        Z_Free(devices);
        return vk_fail_result("vkEnumeratePhysicalDevices", result);
    }

    for (uint32_t i = 0; i < count; i++) {
        vk_queue_families_t queues = vk_find_queues(devices[i]);
        if (queues.has_graphics && queues.has_present && vk_has_swapchain_extension(devices[i])) {
            vk.physical_device = devices[i];
            vk.queues = queues;
            break;
        }
    }

    Z_Free(devices);

    if (!vk.physical_device) {
        Com_SetLastError("No suitable Vulkan physical device found");
        return false;
    }

    VkPhysicalDeviceProperties props;
    vk.GetPhysicalDeviceProperties(vk.physical_device, &props);
    Com_Printf("Using Vulkan device: %s\n", props.deviceName);
    return true;
}

static bool vk_create_device(void)
{
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_infos[2];
    uint32_t queue_info_count = 0;
    const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    queue_infos[queue_info_count++] = (VkDeviceQueueCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.queues.graphics_family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };

    if (vk.queues.present_family != vk.queues.graphics_family) {
        queue_infos[queue_info_count++] = (VkDeviceQueueCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vk.queues.present_family,
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
    }

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queue_info_count,
        .pQueueCreateInfos = queue_infos,
        .enabledExtensionCount = q_countof(extensions),
        .ppEnabledExtensionNames = extensions,
    };

    VkResult result = vk.CreateDevice(vk.physical_device, &create_info, NULL, &vk.device);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateDevice", result);

    if (!vk_load_device())
        return false;

    vk.GetDeviceQueue(vk.device, vk.queues.graphics_family, 0, &vk.graphics_queue);
    vk.GetDeviceQueue(vk.device, vk.queues.present_family, 0, &vk.present_queue);
    return true;
}

static bool vk_create_frame_resources(void)
{
    VkDescriptorSetLayoutBinding sampler_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &sampler_binding,
    };
    VkResult result = vk.CreateDescriptorSetLayout(vk.device, &set_layout_info,
                                                   NULL, &vk.texture_set_layout);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateDescriptorSetLayout", result);

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = MAX_RIMAGES + 1,
    };
    VkDescriptorPoolCreateInfo pool_info_desc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = MAX_RIMAGES + 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    result = vk.CreateDescriptorPool(vk.device, &pool_info_desc,
                                     NULL, &vk.descriptor_pool);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateDescriptorPool", result);

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxLod = 0.0f,
    };
    result = vk.CreateSampler(vk.device, &sampler_info, NULL, &vk.sampler);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateSampler", result);

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = max(sizeof(vk_draw_push_t), sizeof(vk_color3d_push_t)),
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &vk.texture_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    result = vk.CreatePipelineLayout(vk.device, &layout_info,
                                     NULL, &vk.rect_pipeline_layout);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreatePipelineLayout", result);

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk.queues.graphics_family,
    };
    result = vk.CreateCommandPool(vk.device, &pool_info, NULL, &vk.command_pool);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateCommandPool", result);

    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    result = vk.CreateSemaphore(vk.device, &semaphore_info, NULL, &vk.image_available);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateSemaphore", result);
    result = vk.CreateSemaphore(vk.device, &semaphore_info, NULL, &vk.render_finished);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateSemaphore", result);

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    result = vk.CreateFence(vk.device, &fence_info, NULL, &vk.frame_fence);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateFence", result);

    return true;
}

static VkSurfaceFormatKHR vk_choose_surface_format(const VkSurfaceFormatKHR *formats,
                                                   uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return formats[i];
        }
    }

    return formats[0];
}

static VkPresentModeKHR vk_choose_present_mode(const VkPresentModeKHR *modes,
                                               uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            return modes[i];
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D vk_choose_extent(const VkSurfaceCapabilitiesKHR *caps,
                                   int width, int height)
{
    if (caps->currentExtent.width != UINT32_MAX)
        return caps->currentExtent;

    VkExtent2D extent = {
        .width = Q_clip(width, caps->minImageExtent.width, caps->maxImageExtent.width),
        .height = Q_clip(height, caps->minImageExtent.height, caps->maxImageExtent.height),
    };

    return extent;
}

static void vk_destroy_swapchain(void)
{
    if (vk.device && vk.DeviceWaitIdle)
        vk.DeviceWaitIdle(vk.device);

    vk.render_pass_active = false;
    vk.frame_active = false;

    if (vk.command_buffers) {
        vk.FreeCommandBuffers(vk.device, vk.command_pool,
                              vk.swapchain_image_count, vk.command_buffers);
        Z_Free(vk.command_buffers);
        vk.command_buffers = NULL;
    }

    if (vk.rect_pipeline) {
        vk.DestroyPipeline(vk.device, vk.rect_pipeline, NULL);
        vk.rect_pipeline = VK_NULL_HANDLE;
    }

    if (vk.texture_pipeline) {
        vk.DestroyPipeline(vk.device, vk.texture_pipeline, NULL);
        vk.texture_pipeline = VK_NULL_HANDLE;
    }

    if (vk.color3d_pipeline) {
        vk.DestroyPipeline(vk.device, vk.color3d_pipeline, NULL);
        vk.color3d_pipeline = VK_NULL_HANDLE;
    }

    if (vk.world_pipeline) {
        vk.DestroyPipeline(vk.device, vk.world_pipeline, NULL);
        vk.world_pipeline = VK_NULL_HANDLE;
    }

    if (vk.framebuffers) {
        for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
            if (vk.framebuffers[i])
                vk.DestroyFramebuffer(vk.device, vk.framebuffers[i], NULL);
        }
        Z_Free(vk.framebuffers);
        vk.framebuffers = NULL;
    }

    if (vk.depth_view) {
        vk.DestroyImageView(vk.device, vk.depth_view, NULL);
        vk.depth_view = VK_NULL_HANDLE;
    }

    if (vk.depth_image) {
        vk.DestroyImage(vk.device, vk.depth_image, NULL);
        vk.depth_image = VK_NULL_HANDLE;
    }

    if (vk.depth_memory) {
        vk.FreeMemory(vk.device, vk.depth_memory, NULL);
        vk.depth_memory = VK_NULL_HANDLE;
    }

    if (vk.render_pass) {
        vk.DestroyRenderPass(vk.device, vk.render_pass, NULL);
        vk.render_pass = VK_NULL_HANDLE;
    }

    if (vk.swapchain_views) {
        for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
            if (vk.swapchain_views[i])
                vk.DestroyImageView(vk.device, vk.swapchain_views[i], NULL);
        }
        Z_Free(vk.swapchain_views);
        vk.swapchain_views = NULL;
    }

    if (vk.swapchain_images) {
        Z_Free(vk.swapchain_images);
        vk.swapchain_images = NULL;
    }

    if (vk.swapchain_layouts) {
        Z_Free(vk.swapchain_layouts);
        vk.swapchain_layouts = NULL;
    }

    if (vk.swapchain) {
        vk.DestroySwapchainKHR(vk.device, vk.swapchain, NULL);
        vk.swapchain = VK_NULL_HANDLE;
    }

    vk.swapchain_image_count = 0;
}

static bool vk_recreate_swapchain(void)
{
    if (!vk.device)
        return false;

    int width = r_config.width;
    int height = r_config.height;

    vk_destroy_swapchain();
    if (!vk_create_swapchain(width, height)) {
        Com_EPrintf("Couldn't recreate Vulkan swapchain: %s\n", Com_GetLastError());
        return false;
    }

    return true;
}

static bool vk_allocate_swapchain_commands(void)
{
    vk.command_buffers = Z_Malloc(sizeof(*vk.command_buffers) * vk.swapchain_image_count);

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = vk.swapchain_image_count,
    };

    VkResult result = vk.AllocateCommandBuffers(vk.device, &alloc_info, vk.command_buffers);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateCommandBuffers", result);

    return true;
}

static bool vk_create_render_pass(void)
{
    VkAttachmentDescription attachments[] = {
        {
            .format = vk.swapchain_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = vk.depth_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };

    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference depth_ref = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = &depth_ref,
    };

    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = q_countof(attachments),
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };

    VkResult result = vk.CreateRenderPass(vk.device, &create_info, NULL, &vk.render_pass);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateRenderPass", result);

    return true;
}

static bool vk_create_depth_resources(void)
{
    if (vk.depth_format == VK_FORMAT_UNDEFINED) {
        Com_SetLastError("No supported Vulkan depth format");
        return false;
    }

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = vk.depth_format,
        .extent = {
            .width = vk.swapchain_extent.width,
            .height = vk.swapchain_extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkResult result = vk.CreateImage(vk.device, &image_info, NULL, &vk.depth_image);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImage", result);

    VkMemoryRequirements req;
    vk.GetImageMemoryRequirements(vk.device, vk.depth_image, &req);

    uint32_t memory_type = vk_find_memory_type(req.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("No suitable Vulkan depth memory type");
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_type,
    };
    result = vk.AllocateMemory(vk.device, &alloc_info, NULL, &vk.depth_memory);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateMemory", result);

    result = vk.BindImageMemory(vk.device, vk.depth_image, vk.depth_memory, 0);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkBindImageMemory", result);

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk.depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = vk.depth_format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    result = vk.CreateImageView(vk.device, &view_info, NULL, &vk.depth_view);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImageView", result);

    return true;
}

static bool vk_create_framebuffers(void)
{
    vk.framebuffers = Z_Mallocz(sizeof(*vk.framebuffers) * vk.swapchain_image_count);

    for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
        VkImageView attachments[] = { vk.swapchain_views[i], vk.depth_view };
        VkFramebufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vk.render_pass,
            .attachmentCount = q_countof(attachments),
            .pAttachments = attachments,
            .width = vk.swapchain_extent.width,
            .height = vk.swapchain_extent.height,
            .layers = 1,
        };

        VkResult result = vk.CreateFramebuffer(vk.device, &create_info, NULL, &vk.framebuffers[i]);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateFramebuffer", result);
    }

    return true;
}

static VkShaderModule vk_create_shader_module(const uint32_t *code, size_t code_size)
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

static bool vk_create_rect_pipeline(void)
{
    VkShaderModule vert = vk_create_shader_module(vk_rect_vert_spv, sizeof(vk_rect_vert_spv));
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
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
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
                                                 &vk.rect_pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

static bool vk_create_texture_pipeline(void)
{
    VkShaderModule vert = vk_create_shader_module(vk_tex_vert_spv, sizeof(vk_tex_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag = vk_create_shader_module(vk_tex_frag_spv, sizeof(vk_tex_frag_spv));
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
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
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
                                                 &vk.texture_pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

static bool vk_create_color3d_pipeline(void)
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
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
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
        .layout = vk.rect_pipeline_layout,
        .renderPass = vk.render_pass,
        .subpass = 0,
    };

    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                 &create_info, NULL,
                                                 &vk.color3d_pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

static bool vk_create_world_pipeline(void)
{
    VkShaderModule vert = vk_create_shader_module(vk_world_vert_spv,
                                                  sizeof(vk_world_vert_spv));
    if (!vert)
        return false;

    VkShaderModule frag = vk_create_shader_module(vk_world_frag_spv,
                                                  sizeof(vk_world_frag_spv));
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
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
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
        .layout = vk.rect_pipeline_layout,
        .renderPass = vk.render_pass,
        .subpass = 0,
    };

    VkResult result = vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1,
                                                 &create_info, NULL,
                                                 &vk.world_pipeline);
    vk.DestroyShaderModule(vk.device, frag, NULL);
    vk.DestroyShaderModule(vk.device, vert, NULL);

    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateGraphicsPipelines", result);

    return true;
}

static bool vk_create_swapchain(int width, int height)
{
    VkSurfaceCapabilitiesKHR caps;
    VkResult result = vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical_device,
                                                                 vk.surface, &caps);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);

    uint32_t format_count = 0;
    result = vk.GetPhysicalDeviceSurfaceFormatsKHR(vk.physical_device, vk.surface,
                                                   &format_count, NULL);
    if (result != VK_SUCCESS || !format_count)
        return vk_fail_result("vkGetPhysicalDeviceSurfaceFormatsKHR", result);

    VkSurfaceFormatKHR *formats = Z_Malloc(sizeof(*formats) * format_count);
    result = vk.GetPhysicalDeviceSurfaceFormatsKHR(vk.physical_device, vk.surface,
                                                   &format_count, formats);
    if (result != VK_SUCCESS) {
        Z_Free(formats);
        return vk_fail_result("vkGetPhysicalDeviceSurfaceFormatsKHR", result);
    }
    VkSurfaceFormatKHR surface_format = vk_choose_surface_format(formats, format_count);
    Z_Free(formats);

    uint32_t present_count = 0;
    result = vk.GetPhysicalDeviceSurfacePresentModesKHR(vk.physical_device, vk.surface,
                                                        &present_count, NULL);
    if (result != VK_SUCCESS || !present_count)
        return vk_fail_result("vkGetPhysicalDeviceSurfacePresentModesKHR", result);

    VkPresentModeKHR *present_modes = Z_Malloc(sizeof(*present_modes) * present_count);
    result = vk.GetPhysicalDeviceSurfacePresentModesKHR(vk.physical_device, vk.surface,
                                                        &present_count, present_modes);
    if (result != VK_SUCCESS) {
        Z_Free(present_modes);
        return vk_fail_result("vkGetPhysicalDeviceSurfacePresentModesKHR", result);
    }
    VkPresentModeKHR present_mode = vk_choose_present_mode(present_modes, present_count);
    Z_Free(present_modes);

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount && image_count > caps.maxImageCount)
        image_count = caps.maxImageCount;

    VkExtent2D extent = vk_choose_extent(&caps, width, height);
    uint32_t queue_indices[] = { vk.queues.graphics_family, vk.queues.present_family };

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk.surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    if (vk.queues.graphics_family != vk.queues.present_family) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = q_countof(queue_indices);
        create_info.pQueueFamilyIndices = queue_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    result = vk.CreateSwapchainKHR(vk.device, &create_info, NULL, &vk.swapchain);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateSwapchainKHR", result);

    vk.swapchain_format = surface_format.format;
    vk.depth_format = vk_choose_depth_format();
    if (vk.depth_format == VK_FORMAT_UNDEFINED) {
        Com_SetLastError("No supported Vulkan depth format");
        return false;
    }
    vk.swapchain_extent = extent;

    result = vk.GetSwapchainImagesKHR(vk.device, vk.swapchain, &vk.swapchain_image_count, NULL);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkGetSwapchainImagesKHR", result);

    vk.swapchain_images = Z_Malloc(sizeof(*vk.swapchain_images) * vk.swapchain_image_count);
    result = vk.GetSwapchainImagesKHR(vk.device, vk.swapchain,
                                      &vk.swapchain_image_count, vk.swapchain_images);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkGetSwapchainImagesKHR", result);

    vk.swapchain_views = Z_Mallocz(sizeof(*vk.swapchain_views) * vk.swapchain_image_count);
    vk.swapchain_layouts = Z_Malloc(sizeof(*vk.swapchain_layouts) * vk.swapchain_image_count);
    for (uint32_t i = 0; i < vk.swapchain_image_count; i++)
        vk.swapchain_layouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;

    for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vk.swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = vk.swapchain_format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        result = vk.CreateImageView(vk.device, &view_info, NULL, &vk.swapchain_views[i]);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateImageView", result);
    }

    if (!vk_create_render_pass() ||
        !vk_create_rect_pipeline() ||
        !vk_create_texture_pipeline() ||
        !vk_create_color3d_pipeline() ||
        !vk_create_world_pipeline() ||
        !vk_create_depth_resources() ||
        !vk_create_framebuffers())
        return false;

    if (!vk_allocate_swapchain_commands())
        return false;

    Com_Printf("Vulkan swapchain: %ux%u, %u images\n",
               vk.swapchain_extent.width, vk.swapchain_extent.height,
               vk.swapchain_image_count);
    return true;
}

static void vk_transition_image(VkCommandBuffer cmd, uint32_t image_index,
                                VkImageLayout new_layout,
                                VkAccessFlags dst_access,
                                VkPipelineStageFlags dst_stage)
{
    VkImageSubresourceRange range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    VkPipelineStageFlags src_stage;
    VkAccessFlags src_access;

    if (vk.swapchain_layouts[image_index] == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    } else if (vk.swapchain_layouts[image_index] == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        src_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        src_access = 0;
    } else {
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        src_access = 0;
    }

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = vk.swapchain_layouts[image_index],
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = vk.swapchain_images[image_index],
        .subresourceRange = range,
    };

    vk.CmdPipelineBarrier(cmd, src_stage, dst_stage,
                          0, 0, NULL, 0, NULL, 1, &barrier);

    vk.swapchain_layouts[image_index] = new_layout;
}

static VkClearColorValue vk_color_to_clear(uint32_t color)
{
    color_t c = { .u32 = color };
    VkClearColorValue clear = {
        .float32 = {
            c.u8[0] / 255.0f,
            c.u8[1] / 255.0f,
            c.u8[2] / 255.0f,
            c.u8[3] / 255.0f,
        },
    };

    return clear;
}

static void vk_clear_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!vk.render_pass_active || !vk.rect_pipeline || w <= 0 || h <= 0)
        return;

    if (vk.scale != 0 && vk.scale != 1.0f) {
        x = Q_rint(x * vk.scale);
        y = Q_rint(y * vk.scale);
        w = Q_rint(w * vk.scale);
        h = Q_rint(h * vk.scale);
    }

    if (vk.clip_set) {
        int x2 = min(x + w, vk.clip.right);
        int y2 = min(y + h, vk.clip.bottom);
        x = max(x, vk.clip.left);
        y = max(y, vk.clip.top);
        w = x2 - x;
        h = y2 - y;
        if (w <= 0 || h <= 0)
            return;
    }

    int x2 = min(x + w, (int)vk.swapchain_extent.width);
    int y2 = min(y + h, (int)vk.swapchain_extent.height);
    x = max(x, 0);
    y = max(y, 0);
    w = x2 - x;
    h = y2 - y;
    if (w <= 0 || h <= 0)
        return;

    VkClearColorValue clear = vk_color_to_clear(color);
    vk_rect_push_t push = {
        .rect = { x, y, w, h },
        .color = {
            clear.float32[0],
            clear.float32[1],
            clear.float32[2],
            clear.float32[3],
        },
        .screen = {
            vk.swapchain_extent.width,
            vk.swapchain_extent.height,
        },
    };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];

    vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.rect_pipeline);
    vk.CmdPushConstants(cmd, vk.rect_pipeline_layout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
}

static void vk_draw_texture_resource(int x, int y, int w, int h,
                                     float s1, float t1, float s2, float t2,
                                     const vk_texture_t *texture)
{
    if (!vk.render_pass_active || !vk.texture_pipeline || w <= 0 || h <= 0)
        return;
    if (!texture->descriptor_set)
        return;

    if (vk.scale != 0 && vk.scale != 1.0f) {
        x = Q_rint(x * vk.scale);
        y = Q_rint(y * vk.scale);
        w = Q_rint(w * vk.scale);
        h = Q_rint(h * vk.scale);
    }

    float rw = w;
    float rh = h;
    if (vk.clip_set) {
        int x1 = x;
        int y1 = y;
        int x2 = x + w;
        int y2 = y + h;
        int nx1 = max(x1, vk.clip.left);
        int ny1 = max(y1, vk.clip.top);
        int nx2 = min(x2, vk.clip.right);
        int ny2 = min(y2, vk.clip.bottom);
        if (nx2 <= nx1 || ny2 <= ny1)
            return;

        float ds = s2 - s1;
        float dt = t2 - t1;
        s1 += ds * (nx1 - x1) / rw;
        s2 -= ds * (x2 - nx2) / rw;
        t1 += dt * (ny1 - y1) / rh;
        t2 -= dt * (y2 - ny2) / rh;
        x = nx1;
        y = ny1;
        w = nx2 - nx1;
        h = ny2 - ny1;
    }

    int x2i = min(x + w, (int)vk.swapchain_extent.width);
    int y2i = min(y + h, (int)vk.swapchain_extent.height);
    int nx = max(x, 0);
    int ny = max(y, 0);
    if (x2i <= nx || y2i <= ny)
        return;

    float ds = s2 - s1;
    float dt = t2 - t1;
    s1 += ds * (nx - x) / (float)w;
    s2 -= ds * (x + w - x2i) / (float)w;
    t1 += dt * (ny - y) / (float)h;
    t2 -= dt * (y + h - y2i) / (float)h;
    x = nx;
    y = ny;
    w = x2i - nx;
    h = y2i - ny;

    color_t c = { .u32 = vk.color_set ? vk.color.u32 : MakeColor(255, 255, 255, 255) };
    vk_draw_push_t push = {
        .rect = { x, y, w, h },
        .color = {
            c.u8[0] / 255.0f,
            c.u8[1] / 255.0f,
            c.u8[2] / 255.0f,
            c.u8[3] / 255.0f,
        },
        .screen = {
            vk.swapchain_extent.width,
            vk.swapchain_extent.height,
        },
        .uv = { s1, t1, s2, t2 },
    };
    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];

    vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.texture_pipeline);
    vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             vk.rect_pipeline_layout, 0, 1,
                             &texture->descriptor_set, 0, NULL);
    vk.CmdPushConstants(cmd, vk.rect_pipeline_layout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(push), &push);
    vk.CmdDraw(cmd, 6, 1, 0, 0);
}

static void vk_draw_texture_rect(int x, int y, int w, int h,
                                 float s1, float t1, float s2, float t2,
                                 qhandle_t pic)
{
    if (pic <= 0 || pic >= r_numImages)
        return;

    const image_t *image = IMG_ForHandle(pic);
    unsigned index = image->texnum;
    if (!index || index >= MAX_RIMAGES)
        return;

    vk_draw_texture_resource(x, y, w, h, s1, t1, s2, t2,
                             &vk.textures[index]);
}

static void vk_projection_matrix(mat4_t m, float fov_x, float fov_y)
{
    const float znear = 4.0f;
    const float zfar = 4096.0f;
    float xmax = tanf(fov_x * (M_PIf / 360.0f));
    float ymax = tanf(fov_y * (M_PIf / 360.0f));

    memset(m, 0, sizeof(mat4_t));
    m[0] = xmax ? 1.0f / xmax : 1.0f;
    m[5] = ymax ? 1.0f / ymax : 1.0f;
    m[10] = zfar / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (znear * zfar) / (znear - zfar);
}

static void vk_view_matrix(mat4_t matrix, const refdef_t *fd)
{
    vec3_t axis[3];

    AnglesToAxis(fd->viewangles, axis);

    memset(matrix, 0, sizeof(mat4_t));
    matrix[0] = -axis[1][0];
    matrix[4] = -axis[1][1];
    matrix[8] = -axis[1][2];
    matrix[12] = DotProduct(axis[1], fd->vieworg);

    matrix[1] = axis[2][0];
    matrix[5] = axis[2][1];
    matrix[9] = axis[2][2];
    matrix[13] = -DotProduct(axis[2], fd->vieworg);

    matrix[2] = -axis[0][0];
    matrix[6] = -axis[0][1];
    matrix[10] = -axis[0][2];
    matrix[14] = DotProduct(axis[0], fd->vieworg);

    matrix[15] = 1.0f;
}

static void vk_matrix_multiply(mat4_t out, const mat4_t a, const mat4_t b)
{
    mat4_t tmp;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i + j * 4] =
                a[i + 0 * 4] * b[0 + j * 4] +
                a[i + 1 * 4] * b[1 + j * 4] +
                a[i + 2 * 4] * b[2 + j * 4] +
                a[i + 3 * 4] * b[3 + j * 4];
        }
    }

    memcpy(out, tmp, sizeof(tmp));
}

static void vk_world_mvp(mat4_t out, const refdef_t *fd)
{
    mat4_t proj, view;

    vk_projection_matrix(proj, fd->fov_x, fd->fov_y);
    vk_view_matrix(view, fd);
    vk_matrix_multiply(out, proj, view);
}

static bool vk_create_test_triangle(void)
{
    static const vk_vertex_t vertices[] = {
        { { -24.0f, -16.0f, -96.0f }, { 1.0f, 0.15f, 0.10f, 1.0f }, { 0.0f, 0.0f } },
        { {  24.0f, -16.0f, -96.0f }, { 0.1f, 0.85f, 0.25f, 1.0f }, { 1.0f, 0.0f } },
        { {   0.0f,  24.0f, -96.0f }, { 0.1f, 0.35f, 1.00f, 1.0f }, { 0.5f, 1.0f } },
    };
    static const uint32_t indices[] = { 0, 1, 2 };

    if (!vk_upload_mesh(&vk.test_triangle, vertices, q_countof(vertices),
                        indices, q_countof(indices)))
        return false;

    return true;
}

static void vk_draw_mesh(const vk_mesh_t *mesh, const mat4_t mvp, const float color[4])
{
    if (!vk.render_pass_active || !vk.color3d_pipeline ||
        !mesh->vertices.buffer || !mesh->indices.buffer || !mesh->index_count)
        return;

    vk_color3d_push_t push;
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    memcpy(push.color, color, sizeof(push.color));

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.color3d_pipeline);
    vk.CmdBindVertexBuffers(cmd, 0, 1, &mesh->vertices.buffer, &offset);
    vk.CmdBindIndexBuffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk.CmdPushConstants(cmd, vk.rect_pipeline_layout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(push), &push);
    vk.CmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
}

static void vk_draw_world_mesh(const mat4_t mvp)
{
    const vk_mesh_t *mesh = &vk.world.mesh;

    if (!vk.render_pass_active || !vk.world_pipeline ||
        !mesh->vertices.buffer || !mesh->indices.buffer || !mesh->index_count ||
        !vk.world.batch_count)
        return;

    vk_color3d_push_t push;
    memcpy(push.mvp, mvp, sizeof(push.mvp));
    push.color[0] = 1.0f;
    push.color[1] = 1.0f;
    push.color[2] = 1.0f;
    push.color[3] = 1.0f;

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    VkDeviceSize offset = 0;

    vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.world_pipeline);
    vk.CmdBindVertexBuffers(cmd, 0, 1, &mesh->vertices.buffer, &offset);
    vk.CmdBindIndexBuffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vk.CmdPushConstants(cmd, vk.rect_pipeline_layout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(push), &push);

    for (uint32_t i = 0; i < vk.world.batch_count; i++) {
        const vk_world_batch_t *batch = &vk.world.batches[i];
        if (batch->texture_index >= MAX_RIMAGES)
            continue;

        const vk_texture_t *texture = &vk.textures[batch->texture_index];
        if (!texture->descriptor_set)
            continue;

        vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 vk.rect_pipeline_layout, 0, 1,
                                 &texture->descriptor_set, 0, NULL);
        vk.CmdDrawIndexed(cmd, batch->index_count, 1, batch->first_index, 0, 0);
    }
}

static void vk_surface_color(const mface_t *face, float color[4])
{
    uint32_t hash = FS_HashPath(face->texinfo->name, UINT32_MAX);
    float shade = 0.35f + fabsf(face->plane->normal[2]) * 0.35f;

    color[0] = (0.35f + ((hash >> 0) & 255) / 255.0f * 0.55f) * shade;
    color[1] = (0.35f + ((hash >> 8) & 255) / 255.0f * 0.55f) * shade;
    color[2] = (0.35f + ((hash >> 16) & 255) / 255.0f * 0.55f) * shade;
    color[3] = 1.0f;
}

static bool vk_face_is_drawable(mface_t *face)
{
    face->drawflags |= face->texinfo->c.flags & ~DSURF_PLANEBACK;

    if (face->numsurfedges < 3)
        return false;
    if (face->drawflags & SURF_SKY)
        return false;
    if (face->drawflags & SURF_NODRAW)
        return false;

    return true;
}

static bool vk_build_world_mesh(bsp_t *bsp)
{
    uint64_t vertex_count = 0;
    uint64_t index_count = 0;
    uint32_t face_count = 0;

    for (int i = 0; i < bsp->numfaces; i++) {
        mface_t *face = &bsp->faces[i];
        if (!vk_face_is_drawable(face))
            continue;
        if (!face->texinfo->image || !face->texinfo->image->texnum)
            continue;

        vertex_count += face->numsurfedges;
        index_count += (face->numsurfedges - 2) * 3;
        face_count++;
    }

    if (!vertex_count || !index_count)
        return true;
    if (vertex_count > UINT32_MAX || index_count > UINT32_MAX) {
        Com_SetLastError("Vulkan world mesh is too large");
        return false;
    }

    vk_vertex_t *vertices = Z_Malloc(sizeof(*vertices) * vertex_count);
    uint32_t *indices = Z_Malloc(sizeof(*indices) * index_count);
    vk_world_face_t *faces = Z_Malloc(sizeof(*faces) * face_count);
    uint32_t *texture_index_counts = Z_Mallocz(sizeof(*texture_index_counts) * MAX_RIMAGES);
    uint32_t v = 0;
    uint32_t face_index = 0;

    for (int i = 0; i < bsp->numfaces; i++) {
        mface_t *face = &bsp->faces[i];
        if (!vk_face_is_drawable(face))
            continue;
        image_t *image = face->texinfo->image;
        if (!image || !image->texnum)
            continue;

        uint32_t first = v;
        float color[4];
        vk_surface_color(face, color);
        float scale_s = image->width ? 1.0f / image->width : 1.0f;
        float scale_t = image->height ? 1.0f / image->height : 1.0f;

        for (int j = 0; j < face->numsurfedges; j++) {
            const msurfedge_t *surfedge = face->firstsurfedge + j;
            const medge_t *edge = bsp->edges + surfedge->edge;
            const mvertex_t *src = bsp->vertices + edge->v[surfedge->vert];

            VectorCopy(src->point, vertices[v].position);
            memcpy(vertices[v].color, color, sizeof(vertices[v].color));
            vertices[v].uv[0] = (DotProduct(src->point, face->texinfo->axis[0]) +
                                 face->texinfo->offset[0]) * scale_s;
            vertices[v].uv[1] = (DotProduct(src->point, face->texinfo->axis[1]) +
                                 face->texinfo->offset[1]) * scale_t;
            v++;
        }

        faces[face_index++] = (vk_world_face_t) {
            .first_vertex = first,
            .edge_count = face->numsurfedges,
            .texture_index = image->texnum,
        };
        texture_index_counts[image->texnum] += (face->numsurfedges - 2) * 3;
    }

    uint32_t batch_count = 0;
    for (uint32_t i = 0; i < MAX_RIMAGES; i++) {
        if (texture_index_counts[i])
            batch_count++;
    }

    vk_world_batch_t *batches = Z_Malloc(sizeof(*batches) * batch_count);
    uint32_t idx = 0;
    uint32_t batch = 0;

    for (uint32_t texture_index = 0; texture_index < MAX_RIMAGES; texture_index++) {
        uint32_t texture_index_count = texture_index_counts[texture_index];
        if (!texture_index_count)
            continue;

        batches[batch++] = (vk_world_batch_t) {
            .first_index = idx,
            .index_count = texture_index_count,
            .texture_index = texture_index,
        };

        for (uint32_t i = 0; i < face_index; i++) {
            const vk_world_face_t *face = &faces[i];
            if (face->texture_index != texture_index)
                continue;

            for (uint32_t j = 0; j < face->edge_count - 2; j++) {
                indices[idx++] = face->first_vertex;
                indices[idx++] = face->first_vertex + j + 1;
                indices[idx++] = face->first_vertex + j + 2;
            }
        }
    }

    bool ok = vk_upload_mesh(&vk.world.mesh, vertices, v, indices, idx);
    if (ok) {
        if (vk.world.batches)
            Z_Free(vk.world.batches);
        vk.world.batches = batches;
        vk.world.batch_count = batch;
        batches = NULL;
    }

    Z_Free(vertices);
    Z_Free(indices);
    Z_Free(faces);
    Z_Free(texture_index_counts);
    if (batches)
        Z_Free(batches);
    return ok;
}

static void vk_register_world_images(bsp_t *bsp)
{
    char buffer[MAX_QPATH];

    for (int i = 0; i < bsp->numtexinfo; i++) {
        mtexinfo_t *info = &bsp->texinfo[i];

        if (info->c.flags & (SURF_SKY | SURF_NODRAW)) {
            info->image = R_NOTEXTURE;
            continue;
        }

        imageflags_t flags = (info->c.flags & SURF_WARP) ? IF_TURBULENT : IF_NONE;
        Q_concat(buffer, sizeof(buffer), "textures/", info->name, ".wal");
        info->image = IMG_Find(buffer, IT_WALL, flags);
    }
}

static void vk_load_world(const char *name)
{
    char buffer[MAX_QPATH];
    bsp_t *bsp;
    int ret;

    if (!name || !*name)
        return;

    Q_concat(buffer, sizeof(buffer), "maps/", name, ".bsp");
    ret = BSP_Load(buffer, &bsp);
    if (!bsp)
        Com_Error(ERR_DROP, "%s: couldn't load %s: %s",
                  __func__, buffer, BSP_ErrorString(ret));

    if (vk.world.cache == bsp) {
        bsp->refcount--;
        return;
    }

    vk_free_world();
    vk.world.cache = bsp;

    vk_register_world_images(bsp);

    if (!vk_build_world_mesh(bsp))
        Com_WPrintf("Couldn't build Vulkan world mesh: %s\n", Com_GetLastError());
    else
        Com_DPrintf("Vulkan world mesh: %u indices, %u batches\n",
                    vk.world.mesh.index_count, vk.world.batch_count);
}

static void vk_draw_test_triangle(const refdef_t *fd)
{
    if (!vk.render_pass_active || !vk.color3d_pipeline ||
        !vk_show_test_triangle || !vk_show_test_triangle->integer)
        return;
    if (fd->rdflags & RDF_NOWORLDMODEL)
        return;

    mat4_t mvp;
    const float color[4] = { 0.1f, 0.9f, 0.55f, 1.0f };

    vk_projection_matrix(mvp, fd->fov_x, fd->fov_y);
    vk_draw_mesh(&vk.test_triangle, mvp, color);
}

bool VKR_Init(bool total)
{
    if (!total)
        return true;

    Com_Printf("------- VKR_Init -------\n");
    Com_Printf("Using video driver: %s\n", vid->name);

    vk_show_test_triangle = Cvar_Get("vk_show_test_triangle", "0", 0);
    vk_world_textures = Cvar_Get("vk_world_textures", "1", 0);

    if (!vid->init())
        return false;

    if (!vk_load_global() ||
        !vk_create_instance() ||
        !vk_create_surface() ||
        !vk_pick_physical_device() ||
        !vk_create_device() ||
        !vk_create_frame_resources()) {
        VKR_Shutdown(true);
        return false;
    }

    if (!vk_create_test_triangle())
        Com_WPrintf("Couldn't create Vulkan test triangle: %s\n", Com_GetLastError());

    r_registration_sequence = 1;
    IMG_Init();
    IMG_SetUploadBackend(&vk_image_upload);
    IMG_GetPalette();

    Com_Printf("------------------------\n");
    return true;
}

void VKR_Shutdown(bool total)
{
    if (!total)
        return;

    if (r_numImages) {
        IMG_FreeAll();
        IMG_Shutdown();
    }

    vk_destroy_texture_resource(&vk.raw_texture);

    vk_destroy_swapchain();

    if (vk.device && vk.DeviceWaitIdle)
        vk.DeviceWaitIdle(vk.device);

    if (vk.frame_fence) {
        vk.DestroyFence(vk.device, vk.frame_fence, NULL);
        vk.frame_fence = VK_NULL_HANDLE;
    }

    if (vk.render_finished) {
        vk.DestroySemaphore(vk.device, vk.render_finished, NULL);
        vk.render_finished = VK_NULL_HANDLE;
    }

    if (vk.image_available) {
        vk.DestroySemaphore(vk.device, vk.image_available, NULL);
        vk.image_available = VK_NULL_HANDLE;
    }

    if (vk.command_pool) {
        vk.DestroyCommandPool(vk.device, vk.command_pool, NULL);
        vk.command_pool = VK_NULL_HANDLE;
    }

    vk_free_world();
    vk_destroy_mesh(&vk.test_triangle);

    if (vk.sampler) {
        vk.DestroySampler(vk.device, vk.sampler, NULL);
        vk.sampler = VK_NULL_HANDLE;
    }

    if (vk.descriptor_pool) {
        vk.DestroyDescriptorPool(vk.device, vk.descriptor_pool, NULL);
        vk.descriptor_pool = VK_NULL_HANDLE;
    }

    if (vk.texture_set_layout) {
        vk.DestroyDescriptorSetLayout(vk.device, vk.texture_set_layout, NULL);
        vk.texture_set_layout = VK_NULL_HANDLE;
    }

    if (vk.rect_pipeline_layout) {
        vk.DestroyPipelineLayout(vk.device, vk.rect_pipeline_layout, NULL);
        vk.rect_pipeline_layout = VK_NULL_HANDLE;
    }

    if (vk.device && vk.DestroyDevice) {
        vk.DestroyDevice(vk.device, NULL);
        vk.device = VK_NULL_HANDLE;
    }

    if (vk.surface && vk.DestroySurfaceKHR) {
        vk.DestroySurfaceKHR(vk.instance, vk.surface, NULL);
        vk.surface = VK_NULL_HANDLE;
    }

    if (vk.instance && vk.DestroyInstance) {
        vk.DestroyInstance(vk.instance, NULL);
        vk.instance = VK_NULL_HANDLE;
    }

    if (vk.library) {
        Sys_FreeLibrary(vk.library);
        vk.library = NULL;
    }

    if (vid)
        vid->shutdown();

    memset(&vk, 0, sizeof(vk));
}

void VKR_BeginRegistration(const char *map)
{
    r_registration_sequence++;
    vk_load_world(map);
}

qhandle_t VKR_RegisterModel(const char *name)
{
    if (!name || !*name)
        return 0;
    if (*name == '*')
        return ~Q_atoi(name + 1);
    return 0;
}

qhandle_t VKR_RegisterImage(const char *name, imagetype_t type, imageflags_t flags)
{
    return IMG_RegisterImage(name, type, flags);
}

void VKR_SetSky(const char *name, float rotate, bool autorotate, const vec3_t axis)
{
}

void VKR_EndRegistration(void)
{
    IMG_FreeUnused();
}

void VKR_RenderFrame(const refdef_t *fd)
{
    if (!fd)
        return;

    if (!(fd->rdflags & RDF_NOWORLDMODEL) && vk.world.mesh.index_count) {
        mat4_t mvp;

        vk_world_mvp(mvp, fd);
        if (vk_world_textures && vk_world_textures->integer) {
            vk_draw_world_mesh(mvp);
        } else {
            const float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            vk_draw_mesh(&vk.world.mesh, mvp, color);
        }
    }

    vk_draw_test_triangle(fd);
}

void VKR_LightPoint(const vec3_t origin, vec3_t light)
{
    VectorClear(light);
}

void VKR_ClearColor(void)
{
    vk.color_set = false;
}

void VKR_SetAlpha(float alpha)
{
    if (vk.color_set)
        vk.color.u8[3] = Q_clip(alpha, 0.0f, 1.0f) * 255;
}

void VKR_SetColor(uint32_t color)
{
    vk.color.u32 = color;
    vk.color_set = true;
}

void VKR_SetClipRect(const clipRect_t *clip)
{
    if (clip) {
        vk.clip = *clip;
        vk.clip_set = true;
    } else {
        vk.clip_set = false;
    }
}

float VKR_ClampScale(cvar_t *var)
{
    if (!var)
        return 1.0f;

    if (var->value)
        return 1.0f / Cvar_ClampValue(var, 1.0f, 10.0f);

    return 1.0f;
}

void VKR_SetScale(float scale)
{
    vk.scale = scale;
}

void VKR_DrawChar(int x, int y, int flags, int ch, qhandle_t font)
{
    if ((ch & 127) == 32)
        return;

    if (flags & UI_ALTCOLOR)
        ch |= 0x80;
    if (flags & UI_XORCOLOR)
        ch ^= 0x80;

    float s = (ch & 15) * 0.0625f;
    float t = ((ch & 255) >> 4) * 0.0625f;
    vk_draw_texture_rect(x, y, CONCHAR_WIDTH, CONCHAR_HEIGHT,
                         s, t, s + 0.0625f, t + 0.0625f, font);
}

int VKR_DrawString(int x, int y, int flags, size_t max_chars,
                   const char *string, qhandle_t font)
{
    while (max_chars-- && *string) {
        VKR_DrawChar(x, y, flags, (byte)*string++, font);
        x += CONCHAR_WIDTH;
    }

    return x;
}

bool VKR_GetPicSize(int *w, int *h, qhandle_t pic)
{
    return IMG_GetPicSize(w, h, pic);
}

void VKR_DrawPic(int x, int y, qhandle_t pic)
{
    if (pic <= 0 || pic >= r_numImages)
        return;

    const image_t *image = IMG_ForHandle(pic);
    vk_draw_texture_rect(x, y, image->width, image->height,
                         image->sl, image->tl, image->sh, image->th, pic);
}

void VKR_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic)
{
    if (pic <= 0 || pic >= r_numImages)
        return;

    const image_t *image = IMG_ForHandle(pic);
    vk_draw_texture_rect(x, y, w, h,
                         image->sl, image->tl, image->sh, image->th, pic);
}

void VKR_DrawKeepAspectPic(int x, int y, int w, int h, qhandle_t pic)
{
    if (pic <= 0 || pic >= r_numImages)
        return;

    const image_t *image = IMG_ForHandle(pic);
    float scale_w = w;
    float scale_h = h * image->aspect;
    float scale = max(scale_w, scale_h);
    float s = (1.0f - scale_w / scale) * 0.5f;
    float t = (1.0f - scale_h / scale) * 0.5f;

    vk_draw_texture_rect(x, y, w, h, s, t, 1.0f - s, 1.0f - t, pic);
}

void VKR_DrawStretchRaw(int x, int y, int w, int h)
{
    vk_draw_texture_resource(x, y, w, h, 0, 0, 1, 1, &vk.raw_texture);
}

void VKR_UpdateRawPic(int pic_w, int pic_h, const uint32_t *pic)
{
    if (pic_w <= 0 || pic_h <= 0 || !pic)
        return;

    if (!vk_upload_texture_data(&vk.raw_texture, pic_w, pic_h, pic)) {
        Com_WPrintf("Couldn't upload Vulkan raw texture: %s\n",
                    Com_GetLastError());
    }
}

void VKR_TileClear(int x, int y, int w, int h, qhandle_t pic)
{
    const float div64 = 1.0f / 64.0f;

    vk_draw_texture_rect(x, y, w, h, x * div64, y * div64,
                         (x + w) * div64, (y + h) * div64, pic);
}

void VKR_DrawFill8(int x, int y, int w, int h, int c)
{
    uint32_t color = vk.color_set ? vk.color.u32 : d_8to24table[c & 0xff];
    vk_clear_rect(x, y, w, h, color);
}

void VKR_DrawFill32(int x, int y, int w, int h, uint32_t color)
{
    if (vk.color_set)
        color = vk.color.u32;
    vk_clear_rect(x, y, w, h, color);
}

void VKR_BeginFrame(void)
{
    if (!vk.swapchain || vk.frame_active)
        return;

    VkResult result = vk.WaitForFences(vk.device, 1, &vk.frame_fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkWaitForFences failed: Vulkan error %d\n", result);
        return;
    }

    result = vk.AcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX,
                                    vk.image_available, VK_NULL_HANDLE,
                                    &vk.current_image);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        vk_recreate_swapchain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        Com_EPrintf("vkAcquireNextImageKHR failed: Vulkan error %d\n", result);
        return;
    }

    result = vk.ResetFences(vk.device, 1, &vk.frame_fence);
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkResetFences failed: Vulkan error %d\n", result);
        return;
    }

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    result = vk.ResetCommandBuffer(cmd, 0);
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkResetCommandBuffer failed: Vulkan error %d\n", result);
        return;
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    result = vk.BeginCommandBuffer(cmd, &begin_info);
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkBeginCommandBuffer failed: Vulkan error %d\n", result);
        return;
    }

    vk_transition_image(cmd, vk.current_image,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkClearValue clear[] = {
        {
            .color = { .float32 = { 0.015f, 0.025f, 0.035f, 1.0f } },
        },
        {
            .depthStencil = { .depth = 1.0f, .stencil = 0 },
        },
    };
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk.render_pass,
        .framebuffer = vk.framebuffers[vk.current_image],
        .renderArea = {
            .offset = { 0, 0 },
            .extent = vk.swapchain_extent,
        },
        .clearValueCount = q_countof(clear),
        .pClearValues = clear,
    };

    vk.CmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vk.render_pass_active = true;


    vk.frame_active = true;
}

void VKR_EndFrame(void)
{
    if (!vk.frame_active)
        return;

    VkCommandBuffer cmd = vk.command_buffers[vk.current_image];
    if (vk.render_pass_active) {
        vk.CmdEndRenderPass(cmd);
        vk.render_pass_active = false;
    }

    vk_transition_image(cmd, vk.current_image,
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        0,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    VkResult result = vk.EndCommandBuffer(cmd);
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkEndCommandBuffer failed: Vulkan error %d\n", result);
        vk.frame_active = false;
        return;
    }

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &vk.image_available,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &vk.render_finished,
    };

    result = vk.QueueSubmit(vk.graphics_queue, 1, &submit_info, vk.frame_fence);
    if (result != VK_SUCCESS) {
        Com_EPrintf("vkQueueSubmit failed: Vulkan error %d\n", result);
        vk.frame_active = false;
        return;
    }

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &vk.render_finished,
        .swapchainCount = 1,
        .pSwapchains = &vk.swapchain,
        .pImageIndices = &vk.current_image,
    };

    result = vk.QueuePresentKHR(vk.present_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        vk_recreate_swapchain();
    } else if (result != VK_SUCCESS) {
        Com_EPrintf("vkQueuePresentKHR failed: Vulkan error %d\n", result);
    }

    vk.frame_active = false;
}

void VKR_ModeChanged(int width, int height, int flags)
{
    r_config.width = width;
    r_config.height = height;
    r_config.flags = flags | QVF_VIDEOSYNC;

    if (!vk.device)
        return;

    vk_recreate_swapchain();
}

bool VKR_VideoSync(void)
{
    if (!vk.frame_fence)
        return true;

    return vk.WaitForFences(vk.device, 1, &vk.frame_fence, VK_TRUE, 0) == VK_SUCCESS;
}

#endif
