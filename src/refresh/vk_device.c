/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Vulkan instance, device, frame resources and swapchain creation.

#include "vk_local.h"
#include "vk_spirv.h"

#if USE_VULKAN

#define VK_PRODUCT_VERSION_(major, minor, patch, unused) VK_MAKE_VERSION(major, minor, patch)
#define VK_PRODUCT_VERSION(...) VK_PRODUCT_VERSION_(__VA_ARGS__)

bool vk_load_global(void)
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
    vk.EnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)
        vk.GetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
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
    LOAD(GetPhysicalDeviceFeatures);
    LOAD(GetPhysicalDeviceMemoryProperties);
    LOAD(GetPhysicalDeviceFormatProperties);
    LOAD(GetPhysicalDeviceQueueFamilyProperties);
    LOAD(GetPhysicalDeviceSurfaceSupportKHR);
    LOAD(GetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD(GetPhysicalDeviceSurfaceFormatsKHR);
    LOAD(GetPhysicalDeviceSurfacePresentModesKHR);
    LOAD(EnumerateDeviceExtensionProperties);

    vk.GetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)
        vk.GetInstanceProcAddr(vk.instance, "vkGetPhysicalDeviceFeatures2");
    vk.GetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)
        vk.GetInstanceProcAddr(vk.instance, "vkGetPhysicalDeviceProperties2");

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
    LOAD(CreateQueryPool);
    LOAD(DestroyQueryPool);
    LOAD(GetQueryPoolResults);
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
    LOAD(CmdSetViewport);
    LOAD(CmdSetScissor);
    LOAD(CmdSetLineWidth);
    LOAD(CmdPushConstants);
    LOAD(CmdDraw);
    LOAD(CmdDrawIndexed);
    LOAD(CmdBeginQuery);
    LOAD(CmdEndQuery);
    LOAD(CmdResetQueryPool);
    LOAD(CmdWriteTimestamp);
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
    LOAD(CmdCopyImageToBuffer);
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

#if USE_VULKAN_RAYTRACING
    if (vk.raytracing_active) {
#define LOAD_RT(name) \
        vk.name = (PFN_vk##name)vk.GetDeviceProcAddr(vk.device, "vk" #name)
        LOAD_RT(GetBufferDeviceAddress);
        LOAD_RT(CreateAccelerationStructureKHR);
        LOAD_RT(DestroyAccelerationStructureKHR);
        LOAD_RT(GetAccelerationStructureBuildSizesKHR);
        LOAD_RT(CmdBuildAccelerationStructuresKHR);
        LOAD_RT(GetAccelerationStructureDeviceAddressKHR);
#undef LOAD_RT
        if (!vk.GetBufferDeviceAddress || !vk.CreateAccelerationStructureKHR ||
            !vk.DestroyAccelerationStructureKHR ||
            !vk.GetAccelerationStructureBuildSizesKHR ||
            !vk.CmdBuildAccelerationStructuresKHR ||
            !vk.GetAccelerationStructureDeviceAddressKHR) {
            vk.raytracing_active = false;
            Q_strlcpy(vk.raytracing_reason,
                      "ray-query device entry points are missing",
                      sizeof(vk.raytracing_reason));
            Com_WPrintf("Vulkan ray tracing disabled: %s\n",
                        vk.raytracing_reason);
        }
    }
#endif

    return true;
}

bool vk_create_instance(void)
{
    const char *extensions[VK_MAX_INSTANCE_EXTENSIONS];
    uint32_t extension_count = 0;
    VkResult result;

    if (!vid->get_vk_instance_extensions ||
        !vid->get_vk_instance_extensions(&extension_count, extensions, q_countof(extensions))) {
        Com_SetLastError("Video driver can't provide Vulkan instance extensions");
        return false;
    }

    uint32_t loader_version = VK_API_VERSION_1_0;
    if (vk.EnumerateInstanceVersion &&
        vk.EnumerateInstanceVersion(&loader_version) != VK_SUCCESS)
        loader_version = VK_API_VERSION_1_0;
    vk.instance_api_version = min(loader_version, VK_API_VERSION_1_2);

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = PRODUCT,
        .applicationVersion = VK_PRODUCT_VERSION(VERSION_NUMBERS),
        .pEngineName = PRODUCT,
        .engineVersion = VK_PRODUCT_VERSION(VERSION_NUMBERS),
        .apiVersion = vk.instance_api_version,
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

bool vk_create_surface(void)
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

bool vk_pick_physical_device(void)
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

    size_t list_size = strlen("\"automatic\" \"\"") + 1;
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties props;
        vk.GetPhysicalDeviceProperties(devices[i], &props);
        list_size += strlen(props.deviceName) + 32;
    }
    char *device_list = Z_Malloc(list_size);
    Q_strlcpy(device_list, "\"automatic\" \"\"", list_size);

    const char *selector = vk_device ? vk_device->string : "";
    const char *mesa_selector = getenv("MESA_VK_DEVICE_SELECT");
    const char *dri_prime = getenv("DRI_PRIME");
    unsigned mesa_vendor = 0, mesa_device = 0;
    bool mesa_match = false;
    if (!selector[0] && mesa_selector && mesa_selector[0]) {
        mesa_match = sscanf(mesa_selector, "%x:%x",
                            &mesa_vendor, &mesa_device) == 2;
        if (!mesa_match)
            selector = mesa_selector;
    }
    bool numeric_selector = selector[0] != '\0';
    for (const char *p = selector; numeric_selector && *p; p++)
        numeric_selector = *p >= '0' && *p <= '9';
    int selected_index = numeric_selector ? Q_atoi(selector) : -1;
    // A PCI vendor:device pair, such as 1002:743f, which the menu uses so
    // that OpenGL understands the same value.
    unsigned pair_vendor = 0, pair_device = 0;
    char pair_extra;
    bool pair_selector = !numeric_selector &&
        sscanf(selector, "%x:%x%c", &pair_vendor, &pair_device,
               &pair_extra) == 2;
    int best_score = INT_MIN;
    bool requested_selected = false;

    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties props;
        vk.GetPhysicalDeviceProperties(devices[i], &props);
        vk_queue_families_t queues = vk_find_queues(devices[i]);
        bool suitable = queues.has_graphics && queues.has_present &&
            vk_has_swapchain_extension(devices[i]);
        Com_Printf("Vulkan device %u: %s (%s), vendor/device 0x%04x/0x%04x%s\n",
                   i, props.deviceName, vk_device_type_string(props.deviceType),
                   props.vendorID, props.deviceID, suitable ? "" : " [unsuitable]");
        if (!suitable)
            continue;

        for (char *p = props.deviceName; *p; p++) {
            if (*p == '"')
                *p = '\'';
        }
        // Two identical GPUs share a vendor:device pair, so those are
        // listed by number.
        bool unique = true;
        for (uint32_t j = 0; j < count && unique; j++) {
            VkPhysicalDeviceProperties other;
            if (j == i)
                continue;
            vk.GetPhysicalDeviceProperties(devices[j], &other);
            unique = other.vendorID != props.vendorID ||
                other.deviceID != props.deviceID;
        }
        if (unique)
            Q_strlcat(device_list, va(" \"%s\" \"%04x:%04x\"",
                                      props.deviceName, props.vendorID,
                                      props.deviceID), list_size);
        else
            Q_strlcat(device_list, va(" \"%s\" \"%u\"",
                                      props.deviceName, i), list_size);

        bool requested = selector[0] &&
            ((numeric_selector && selected_index == (int)i) ||
             (pair_selector && props.vendorID == pair_vendor &&
              props.deviceID == pair_device) ||
             (!numeric_selector && !pair_selector &&
              Q_stristr(props.deviceName, selector)));
        if (!selector[0] && mesa_match)
            requested = props.vendorID == mesa_vendor &&
                props.deviceID == mesa_device;
        if (!selector[0] && !mesa_match && dri_prime && dri_prime[0] &&
            strcmp(dri_prime, "0"))
            requested = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        if ((selector[0] || mesa_match ||
             (dri_prime && dri_prime[0] && strcmp(dri_prime, "0"))) &&
            !requested)
            continue;

        int score = 0;
        switch (props.deviceType) {
        // On hybrid systems the integrated GPU normally owns the display.
        // Rendering on a discrete device that merely supports presentation
        // forces every uncapped frame through a PRIME copy.  OpenGL defaults
        // to the display GPU, so prefer the integrated present-capable device
        // as well; vk_device remains available for an explicit override.
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 400; break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 300; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 200; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: score = 100; break;
        default: score = 0; break;
        }
        score += min((int)(props.limits.maxImageDimension2D / 1024), 99);
        if ((!requested || !requested_selected) &&
            (requested || score > best_score)) {
            vk.physical_device = devices[i];
            vk.queues = queues;
            best_score = score;
            if (requested)
                requested_selected = true;
        }
    }

    Cvar_SetByVar(vk_devicelist, device_list, FROM_CODE);
    Z_Free(device_list);
    Z_Free(devices);

    if (!vk.physical_device) {
        if (mesa_match)
            Com_SetLastError(va("MESA_VK_DEVICE_SELECT device %04x:%04x is not available",
                                mesa_vendor, mesa_device));
        else if (selector[0])
            Com_SetLastError(va("Requested Vulkan device '%s' is not available",
                                selector));
        else
            Com_SetLastError("No suitable Vulkan physical device found");
        return false;
    }

    vk.GetPhysicalDeviceProperties(vk.physical_device,
                                   &vk.physical_device_properties);
    vk.GetPhysicalDeviceFeatures(vk.physical_device,
                                 &vk.physical_device_features);
    Com_Printf("Using Vulkan device: %s\n",
               vk.physical_device_properties.deviceName);
    return true;
}

const char *vk_device_type_string(VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "CPU";
    default:
        return "other";
    }
}

void vk_strings_f(void)
{
    const VkPhysicalDeviceProperties *props = &vk.physical_device_properties;

    if (!vk.physical_device) {
        Com_Printf("Vulkan renderer is not initialized\n");
        return;
    }

    Com_Printf("Vulkan device: %s\n", props->deviceName);
    Com_Printf("Vulkan type: %s\n",
               vk_device_type_string(props->deviceType));
    Com_Printf("Vulkan API: %u.%u.%u\n",
               VK_VERSION_MAJOR(props->apiVersion),
               VK_VERSION_MINOR(props->apiVersion),
               VK_VERSION_PATCH(props->apiVersion));
    Com_Printf("Vulkan driver: %u.%u.%u\n",
               VK_VERSION_MAJOR(props->driverVersion),
               VK_VERSION_MINOR(props->driverVersion),
               VK_VERSION_PATCH(props->driverVersion));
    Com_Printf("Vulkan vendor/device: 0x%04x/0x%04x\n",
               props->vendorID, props->deviceID);
    Com_Printf("Vulkan queue families: graphics=%u present=%u\n",
               vk.queues.graphics_family, vk.queues.present_family);
    Com_Printf("Vulkan swapchain: %ux%u, %u images\n",
               vk.swapchain_extent.width, vk.swapchain_extent.height,
               vk.swapchain_image_count);
#if USE_VULKAN_RAYTRACING
    const char *rt_status = vk.raytracing_active ? "active" :
        (vk.raytracing_supported ? "available" : "unavailable");
    if (vk.raytracing_reason[0])
        Com_Printf("Vulkan ray tracing: %s (%s)\n", rt_status,
                   vk.raytracing_reason);
    else
        Com_Printf("Vulkan ray tracing: %s\n", rt_status);
#else
    Com_Printf("Vulkan ray tracing: not compiled\n");
#endif
}

bool vk_create_device(void)
{
    float priorities[2] = { 1.0f, 1.0f };
    VkDeviceQueueCreateInfo queue_infos[2];
    uint32_t queue_info_count = 0;
    uint32_t present_queue_index = 0;
    const char *extensions[1
#if USE_VULKAN_RAYTRACING
                           + VK_RT_REQUIRED_EXTENSION_COUNT
#endif
    ] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    uint32_t extension_count = 1;
    VkPhysicalDeviceFeatures features = { 0 };
#if USE_VULKAN_RAYTRACING
    vk_probe_raytracing();
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
    if (vk.raytracing_active) {
        extensions[extension_count++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        extensions[extension_count++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
        extensions[extension_count++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
        ray_query.rayQuery = VK_TRUE;
        acceleration.accelerationStructure = VK_TRUE;
        buffer_address.bufferDeviceAddress = VK_TRUE;
        descriptors.runtimeDescriptorArray = VK_TRUE;
        descriptors.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        descriptors.descriptorBindingPartiallyBound = VK_TRUE;
        descriptors.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    } else if (vk_raytracing && vk_raytracing->integer) {
        Com_WPrintf("Vulkan ray tracing unavailable: %s; using raster renderer\n",
                    vk.raytracing_reason);
    }
#endif

    uint32_t family_count = 0;
    vk.GetPhysicalDeviceQueueFamilyProperties(vk.physical_device,
                                               &family_count, NULL);
    VkQueueFamilyProperties *families = family_count ?
        Z_Malloc(sizeof(*families) * family_count) : NULL;
    if (families)
        vk.GetPhysicalDeviceQueueFamilyProperties(vk.physical_device,
                                                   &family_count, families);
    bool separate_shared_present_queue =
        vk.queues.present_family == vk.queues.graphics_family && families &&
        vk.queues.graphics_family < family_count &&
        families[vk.queues.graphics_family].queueCount > 1;
    if (families)
        Z_Free(families);
    if (separate_shared_present_queue)
        present_queue_index = 1;

    queue_infos[queue_info_count++] = (VkDeviceQueueCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk.queues.graphics_family,
        .queueCount = separate_shared_present_queue ? 2 : 1,
        .pQueuePriorities = priorities,
    };

    if (vk.queues.present_family != vk.queues.graphics_family) {
        queue_infos[queue_info_count++] = (VkDeviceQueueCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vk.queues.present_family,
            .queueCount = 1,
            .pQueuePriorities = priorities,
        };
    }

    if (vk.physical_device_features.samplerAnisotropy)
        features.samplerAnisotropy = VK_TRUE;
    if (vk.physical_device_features.fillModeNonSolid)
        features.fillModeNonSolid = VK_TRUE;
    if (vk.physical_device_features.wideLines)
        features.wideLines = VK_TRUE;
    if (vk.physical_device_features.occlusionQueryPrecise)
        features.occlusionQueryPrecise = VK_TRUE;
    // Bloom writes a second color attachment with its own blend state.
    if (vk.physical_device_features.independentBlend)
        features.independentBlend = VK_TRUE;
    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
#if USE_VULKAN_RAYTRACING
        .pNext = vk.raytracing_active ? &descriptors : NULL,
#endif
        .queueCreateInfoCount = queue_info_count,
        .pQueueCreateInfos = queue_infos,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
        .pEnabledFeatures = &features,
    };

    VkResult result = vk.CreateDevice(vk.physical_device, &create_info, NULL, &vk.device);
#if USE_VULKAN_RAYTRACING
    if (result != VK_SUCCESS && vk.raytracing_active) {
        Com_WPrintf("Couldn't create a ray-query Vulkan device (error %d); retrying raster-only\n",
                    result);
        vk.raytracing_active = false;
        Q_strlcpy(vk.raytracing_reason, "ray-query device creation failed",
                  sizeof(vk.raytracing_reason));
        create_info.pNext = NULL;
        create_info.enabledExtensionCount = 1;
        result = vk.CreateDevice(vk.physical_device, &create_info, NULL, &vk.device);
    }
#endif
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateDevice", result);

    if (!vk_load_device())
        return false;

    vk.GetDeviceQueue(vk.device, vk.queues.graphics_family, 0, &vk.graphics_queue);
    vk.GetDeviceQueue(vk.device, vk.queues.present_family,
                      present_queue_index, &vk.present_queue);
    Com_Printf("Vulkan queues: graphics %u:0, present %u:%u\n",
               vk.queues.graphics_family, vk.queues.present_family,
               present_queue_index);
    return true;
}

void vk_swapinterval_changed(cvar_t *self)
{
    (void)self;

    if (!vk.device || !vk.swapchain)
        return;

    vk_recreate_swapchain("present mode change");
}

void vk_present_mode_changed(cvar_t *self)
{
    vk_present_mode_warned = false;
    vk_swapinterval_changed(self);
}

void vk_drawsky_changed(cvar_t *self)
{
    extern void CL_SetSky(void);

    (void)self;

    CL_SetSky();
}

bool vk_create_timestamp_query_pool(void)
{
    VkQueryPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = VK_MAX_FRAMES_IN_FLIGHT * 2,
    };
    VkResult result = vk.CreateQueryPool(vk.device, &info, NULL,
                                         &vk.timestamp_query_pool);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateQueryPool(timestamp)", result);
    memset(vk.timestamp_valid, 0, sizeof(vk.timestamp_valid));
    return true;
}

bool vk_create_frame_resources(void)
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

    VkDescriptorSetLayoutBinding ssr_bindings[3] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };
    VkDescriptorSetLayoutCreateInfo ssr_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = q_countof(ssr_bindings),
        .pBindings = ssr_bindings,
    };
    result = vk.CreateDescriptorSetLayout(vk.device, &ssr_layout_info,
                                           NULL, &vk.ssr_set_layout);
    if (result != VK_SUCCESS)
        Com_WPrintf("Couldn't create Vulkan SSR descriptor layout; reflections will be disabled\n");

#if USE_VULKAN_RAYTRACING
    if (vk.raytracing_active) {
        VkDescriptorSetLayoutBinding rt_bindings[] = {
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };
        VkDescriptorSetLayoutCreateInfo rt_layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = q_countof(rt_bindings),
            .pBindings = rt_bindings,
        };
        result = vk.CreateDescriptorSetLayout(vk.device, &rt_layout_info,
                                               NULL, &vk.rt_set_layout);
        if (result != VK_SUCCESS) {
            Com_WPrintf("Couldn't create Vulkan ray-query descriptor layout; disabling ray tracing\n");
            vk.raytracing_active = false;
            Q_strlcpy(vk.raytracing_reason, "descriptor layout creation failed",
                      sizeof(vk.raytracing_reason));
        }
    }
#endif

    const uint32_t texture_descriptor_count =
        MAX_RIMAGES * 2 + VK_MAX_CUBEMAPS * 6 + 17;
    VkDescriptorPoolSize pool_sizes[3] = { {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = texture_descriptor_count,
    } };
    uint32_t pool_size_count = 1;
#if USE_VULKAN_RAYTRACING
    if (vk.raytracing_active) {
        pool_sizes[pool_size_count++] = (VkDescriptorPoolSize) {
            .type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            .descriptorCount = 1,
        };
        pool_sizes[pool_size_count++] = (VkDescriptorPoolSize) {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
        };
        pool_sizes[0].descriptorCount++;
    }
#endif
    VkDescriptorPoolCreateInfo pool_info_desc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = texture_descriptor_count + pool_size_count - 1,
        .poolSizeCount = pool_size_count,
        .pPoolSizes = pool_sizes,
    };
    result = vk.CreateDescriptorPool(vk.device, &pool_info_desc,
                                     NULL, &vk.descriptor_pool);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateDescriptorPool", result);

    if (vk.ssr_set_layout) {
        VkDescriptorSetLayout layouts[2] = {
            vk.ssr_set_layout, vk.ssr_set_layout
        };
        VkDescriptorSet sets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkDescriptorSetAllocateInfo ssr_alloc = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = vk.descriptor_pool,
            .descriptorSetCount = 2,
            .pSetLayouts = layouts,
        };
        result = vk.AllocateDescriptorSets(vk.device, &ssr_alloc,
                                            sets);
        if (result != VK_SUCCESS) {
            Com_WPrintf("Couldn't allocate Vulkan SSR descriptors; reflections will be disabled\n");
            vk.ssr_descriptor_set = VK_NULL_HANDLE;
            vk.ssr_resolve_descriptor_set = VK_NULL_HANDLE;
        } else {
            vk.ssr_descriptor_set = sets[0];
            vk.ssr_resolve_descriptor_set = sets[1];
        }
    }

#if USE_VULKAN_RAYTRACING
    if (vk.raytracing_active) {
        VkDescriptorSetAllocateInfo rt_alloc = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = vk.descriptor_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &vk.rt_set_layout,
        };
        result = vk.AllocateDescriptorSets(vk.device, &rt_alloc,
                                           &vk.rt_descriptor_set);
        if (result != VK_SUCCESS) {
            Com_WPrintf("Couldn't allocate Vulkan ray-query descriptor set; disabling ray tracing\n");
            vk.raytracing_active = false;
            Q_strlcpy(vk.raytracing_reason, "descriptor allocation failed",
                      sizeof(vk.raytracing_reason));
        }
    }
#endif

    if (!vk_create_sampler(&vk.sampler))
        return false;
    if (!vk_create_sky_sampler(&vk.sky_sampler))
        return false;
    if (!vk_create_postprocess_sampler(&vk.postprocess_sampler))
        return false;
    if (!vk_create_nearest_sampler(&vk.nearest_sampler,
                                   VK_SAMPLER_ADDRESS_MODE_REPEAT))
        return false;
    if (!vk_create_nearest_sampler(&vk.sky_nearest_sampler,
                                   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
        return false;

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = max(max(max(max(max(max(sizeof(vk_draw_push_t), sizeof(vk_color3d_push_t)),
                                    sizeof(vk_snow_push_t)),
                                sizeof(vk_world_push_t)), sizeof(vk_world_lit_push_t)),
                            sizeof(vk_alias_push_t)),
                    sizeof(vk_alias_shadow_push_t)),
    };
    VkDescriptorSetLayout set_layouts[2] = { vk.texture_set_layout,
#if USE_VULKAN_RAYTRACING
                                             vk.rt_set_layout,
#endif
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount =
#if USE_VULKAN_RAYTRACING
            vk.raytracing_active ? 2 : 1,
#else
            1,
#endif
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    result = vk.CreatePipelineLayout(vk.device, &layout_info,
                                     NULL, &vk.rect_pipeline_layout);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreatePipelineLayout", result);

    if (vk.ssr_set_layout) {
        VkPushConstantRange ssr_push_range = {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(vk_ssr_push_t),
        };
        VkPipelineLayoutCreateInfo ssr_pipeline_layout_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &vk.ssr_set_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &ssr_push_range,
        };
        result = vk.CreatePipelineLayout(vk.device, &ssr_pipeline_layout_info,
                                          NULL, &vk.ssr_pipeline_layout);
        if (result != VK_SUCCESS) {
            Com_WPrintf("Couldn't create Vulkan SSR pipeline layout; reflections will be disabled\n");
            vk.ssr_pipeline_layout = VK_NULL_HANDLE;
        }
    }

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
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++) {
        result = vk.CreateSemaphore(vk.device, &semaphore_info, NULL,
                                    &vk.image_available[i]);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateSemaphore", result);
        result = vk.CreateFence(vk.device, &fence_info, NULL,
                                &vk.frame_fence[i]);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateFence", result);
    }

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
    VkPresentModeKHR requested = VK_PRESENT_MODE_FIFO_KHR;
    bool explicit_mode = vk_present_mode && vk_present_mode->integer > 0;

    if (explicit_mode) {
        switch (vk_present_mode->integer) {
        case 2:
            requested = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        case 3:
            requested = VK_PRESENT_MODE_IMMEDIATE_KHR;
            break;
        default:
            requested = VK_PRESENT_MODE_FIFO_KHR;
            break;
        }
    } else if (!vk_swapinterval || vk_swapinterval->integer) {
        return VK_PRESENT_MODE_FIFO_KHR;
    } else {
        requested = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (modes[i] == requested)
            return requested;
    }

    if (!explicit_mode) {
        for (uint32_t i = 0; i < count; i++) {
            if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                return modes[i];
        }
    } else if (!vk_present_mode_warned) {
        Com_WPrintf("Requested Vulkan present mode is unavailable; falling back to fifo.\n");
        vk_present_mode_warned = true;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

const char *vk_present_mode_name(VkPresentModeKHR mode)
{
    switch (mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "mailbox";
    case VK_PRESENT_MODE_FIFO_KHR:
        return "fifo";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "fifo_relaxed";
    default:
        return "unknown";
    }
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

void vk_destroy_swapchain(void)
{
    // An image that VKR_VideoSync acquired ahead of a frame that was never
    // drawn leaves a signal pending on its image_available semaphore, which
    // outlives the swapchain. Acquiring with a signaled semaphore is invalid,
    // so consume the signal with an empty submission; the wait below
    // completes it.
    if (vk.image_acquired && vk.device && vk.graphics_queue) {
        VkSemaphore semaphore = vk.image_available[vk.frame_index];
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo submit = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &semaphore,
            .pWaitDstStageMask = &stage,
        };
        VkResult result = vk.QueueSubmit(vk.graphics_queue, 1, &submit,
                                         VK_NULL_HANDLE);
        if (result != VK_SUCCESS)
            Com_EPrintf("vkQueueSubmit failed: Vulkan error %d\n", result);
    }

    if (vk.device && vk.DeviceWaitIdle)
        vk.DeviceWaitIdle(vk.device);

    vk.render_pass_active = false;
    vk.frame_active = false;
    vk.image_acquired = false;

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

    if (vk.vignette_pipeline) {
        vk.DestroyPipeline(vk.device, vk.vignette_pipeline, NULL);
        vk.vignette_pipeline = VK_NULL_HANDLE;
    }

    if (vk.texture_pipeline) {
        vk.DestroyPipeline(vk.device, vk.texture_pipeline, NULL);
        vk.texture_pipeline = VK_NULL_HANDLE;
    }
    if (vk.scene_pipeline) {
        vk.DestroyPipeline(vk.device, vk.scene_pipeline, NULL);
        vk.scene_pipeline = VK_NULL_HANDLE;
    }

    if (vk.waterwarp_pipeline) {
        vk.DestroyPipeline(vk.device, vk.waterwarp_pipeline, NULL);
        vk.waterwarp_pipeline = VK_NULL_HANDLE;
    }

    if (vk.bloom_downscale_pipeline) {
        vk.DestroyPipeline(vk.device, vk.bloom_downscale_pipeline, NULL);
        vk.bloom_downscale_pipeline = VK_NULL_HANDLE;
    }

    if (vk.bloom_blur_pipeline) {
        vk.DestroyPipeline(vk.device, vk.bloom_blur_pipeline, NULL);
        vk.bloom_blur_pipeline = VK_NULL_HANDLE;
    }

    if (vk.bloom_add_pipeline) {
        vk.DestroyPipeline(vk.device, vk.bloom_add_pipeline, NULL);
        vk.bloom_add_pipeline = VK_NULL_HANDLE;
    }
    if (vk.ssr_pipeline) {
        vk.DestroyPipeline(vk.device, vk.ssr_pipeline, NULL);
        vk.ssr_pipeline = VK_NULL_HANDLE;
    }
    if (vk.ssr_resolve_pipeline) {
        vk.DestroyPipeline(vk.device, vk.ssr_resolve_pipeline, NULL);
        vk.ssr_resolve_pipeline = VK_NULL_HANDLE;
    }

    if (vk.color3d_pipeline) {
        vk.DestroyPipeline(vk.device, vk.color3d_pipeline, NULL);
        vk.color3d_pipeline = VK_NULL_HANDLE;
    }

    if (vk.snow_pipeline) {
        vk.DestroyPipeline(vk.device, vk.snow_pipeline, NULL);
        vk.snow_pipeline = VK_NULL_HANDLE;
    }

    if (vk.snow_full_pipeline) {
        vk.DestroyPipeline(vk.device, vk.snow_full_pipeline, NULL);
        vk.snow_full_pipeline = VK_NULL_HANDLE;
    }

    if (vk.puddle_pipeline) {
        vk.DestroyPipeline(vk.device, vk.puddle_pipeline, NULL);
        vk.puddle_pipeline = VK_NULL_HANDLE;
    }

    for (int i = 0; i < 2; i++) {
        if (vk.snow_rt_pipelines[i]) {
            vk.DestroyPipeline(vk.device, vk.snow_rt_pipelines[i], NULL);
            vk.snow_rt_pipelines[i] = VK_NULL_HANDLE;
        }
    }

    if (vk.line3d_pipeline) {
        vk.DestroyPipeline(vk.device, vk.line3d_pipeline, NULL);
        vk.line3d_pipeline = VK_NULL_HANDLE;
    }

    if (vk.debug_line_pipeline) {
        vk.DestroyPipeline(vk.device, vk.debug_line_pipeline, NULL);
        vk.debug_line_pipeline = VK_NULL_HANDLE;
    }

    if (vk.debug_text_pipeline) {
        vk.DestroyPipeline(vk.device, vk.debug_text_pipeline, NULL);
        vk.debug_text_pipeline = VK_NULL_HANDLE;
    }

    if (vk.beam_pipeline) {
        vk.DestroyPipeline(vk.device, vk.beam_pipeline, NULL);
        vk.beam_pipeline = VK_NULL_HANDLE;
    }
    if (vk.beam_bloom_pipeline) {
        vk.DestroyPipeline(vk.device, vk.beam_bloom_pipeline, NULL);
        vk.beam_bloom_pipeline = VK_NULL_HANDLE;
    }
    if (vk.beam_add_pipeline) {
        vk.DestroyPipeline(vk.device, vk.beam_add_pipeline, NULL);
        vk.beam_add_pipeline = VK_NULL_HANDLE;
    }
    if (vk.beam_add_bloom_pipeline) {
        vk.DestroyPipeline(vk.device, vk.beam_add_bloom_pipeline, NULL);
        vk.beam_add_bloom_pipeline = VK_NULL_HANDLE;
    }

    if (vk.world_pipeline) {
        vk.DestroyPipeline(vk.device, vk.world_pipeline, NULL);
        vk.world_pipeline = VK_NULL_HANDLE;
    }

    if (vk.world_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.world_alpha_pipeline, NULL);
        vk.world_alpha_pipeline = VK_NULL_HANDLE;
    }

    if (vk.world_blend_pipeline) {
        vk.DestroyPipeline(vk.device, vk.world_blend_pipeline, NULL);
        vk.world_blend_pipeline = VK_NULL_HANDLE;
    }

    if (vk.world_glow_pipeline) {
        vk.DestroyPipeline(vk.device, vk.world_glow_pipeline, NULL);
        vk.world_glow_pipeline = VK_NULL_HANDLE;
    }

    if (vk.pixel_world_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_pipeline, NULL);
        vk.pixel_world_pipeline = VK_NULL_HANDLE;
    }

    if (vk.pixel_world_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_alpha_pipeline, NULL);
        vk.pixel_world_alpha_pipeline = VK_NULL_HANDLE;
    }
    if (vk.pixel_world_fast_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_fast_pipeline, NULL);
        vk.pixel_world_fast_pipeline = VK_NULL_HANDLE;
    }

    if (vk.pixel_world_glow_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_glow_pipeline, NULL);
        vk.pixel_world_glow_pipeline = VK_NULL_HANDLE;
    }

    if (vk.pixel_world_glow_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_glow_alpha_pipeline, NULL);
        vk.pixel_world_glow_alpha_pipeline = VK_NULL_HANDLE;
    }
    if (vk.pixel_world_glow_fast_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_glow_fast_pipeline, NULL);
        vk.pixel_world_glow_fast_pipeline = VK_NULL_HANDLE;
    }
#if USE_VULKAN_RAYTRACING
    if (vk.pixel_world_rt_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_rt_pipeline, NULL);
        vk.pixel_world_rt_pipeline = VK_NULL_HANDLE;
    }
    if (vk.pixel_world_rt_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_rt_alpha_pipeline, NULL);
        vk.pixel_world_rt_alpha_pipeline = VK_NULL_HANDLE;
    }
    if (vk.pixel_world_rt_glow_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_rt_glow_pipeline, NULL);
        vk.pixel_world_rt_glow_pipeline = VK_NULL_HANDLE;
    }
    if (vk.pixel_world_rt_glow_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.pixel_world_rt_glow_alpha_pipeline, NULL);
        vk.pixel_world_rt_glow_alpha_pipeline = VK_NULL_HANDLE;
    }
#endif

    if (vk.pixel_world_pipeline_layout) {
        vk.DestroyPipelineLayout(vk.device, vk.pixel_world_pipeline_layout, NULL);
        vk.pixel_world_pipeline_layout = VK_NULL_HANDLE;
    }

    if (vk.sky_pipeline) {
        vk.DestroyPipeline(vk.device, vk.sky_pipeline, NULL);
        vk.sky_pipeline = VK_NULL_HANDLE;
    }

    if (vk.sprite_pipeline) {
        vk.DestroyPipeline(vk.device, vk.sprite_pipeline, NULL);
        vk.sprite_pipeline = VK_NULL_HANDLE;
    }

    if (vk.sprite_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.sprite_alpha_pipeline, NULL);
        vk.sprite_alpha_pipeline = VK_NULL_HANDLE;
    }
    if (vk.sprite_bloom_pipeline) {
        vk.DestroyPipeline(vk.device, vk.sprite_bloom_pipeline, NULL);
        vk.sprite_bloom_pipeline = VK_NULL_HANDLE;
    }

    if (vk.particle_add_pipeline) {
        vk.DestroyPipeline(vk.device, vk.particle_add_pipeline, NULL);
        vk.particle_add_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_pipeline, NULL);
        vk.alias_pipeline = VK_NULL_HANDLE;
    }
    if (vk.alias_bloom_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_bloom_pipeline, NULL);
        vk.alias_bloom_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_shell_bloom_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_shell_bloom_pipeline, NULL);
        vk.alias_shell_bloom_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_alpha_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_alpha_pipeline, NULL);
        vk.alias_alpha_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_depth_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_depth_pipeline, NULL);
        vk.alias_depth_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_blend_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_blend_pipeline, NULL);
        vk.alias_blend_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_shadow_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_shadow_pipeline, NULL);
        vk.alias_shadow_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_cel_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_cel_pipeline, NULL);
        vk.alias_cel_pipeline = VK_NULL_HANDLE;
    }

    if (vk.alias_line_pipeline) {
        vk.DestroyPipeline(vk.device, vk.alias_line_pipeline, NULL);
        vk.alias_line_pipeline = VK_NULL_HANDLE;
    }

    if (vk.framebuffers) {
        for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
            if (vk.framebuffers[i])
                vk.DestroyFramebuffer(vk.device, vk.framebuffers[i], NULL);
        }
        Z_Free(vk.framebuffers);
        vk.framebuffers = NULL;
    }

    if (vk.scene_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.scene_framebuffer, NULL);
        vk.scene_framebuffer = VK_NULL_HANDLE;
    }

    if (vk.bloom_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.bloom_framebuffer, NULL);
        vk.bloom_framebuffer = VK_NULL_HANDLE;
    }

    if (vk.blur_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.blur_framebuffer, NULL);
        vk.blur_framebuffer = VK_NULL_HANDLE;
    }
    if (vk.ssr_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.ssr_framebuffer, NULL);
        vk.ssr_framebuffer = VK_NULL_HANDLE;
    }
    if (vk.ssr_resolve_framebuffer) {
        vk.DestroyFramebuffer(vk.device, vk.ssr_resolve_framebuffer, NULL);
        vk.ssr_resolve_framebuffer = VK_NULL_HANDLE;
    }

    vk_destroy_texture_resource(&vk.scene_texture);
    vk_destroy_texture_resource(&vk.bloom_source_texture);
    vk_destroy_texture_resource(&vk.bloom_texture);
    vk_destroy_texture_resource(&vk.ssr_texture);
    vk_destroy_texture_resource(&vk.ssr_resolve_texture);
    vk_destroy_texture_resource(&vk.blur_texture);
    vk.scene_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.bloom_source_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.bloom_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.blur_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.ssr_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.ssr_resolve_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    vk.ssr_ready = false;

    if (vk.multisample_view) {
        vk.DestroyImageView(vk.device, vk.multisample_view, NULL);
        vk.multisample_view = VK_NULL_HANDLE;
    }

    if (vk.multisample_image) {
        vk.DestroyImage(vk.device, vk.multisample_image, NULL);
        vk.multisample_image = VK_NULL_HANDLE;
    }

    if (vk.multisample_memory) {
        vk.FreeMemory(vk.device, vk.multisample_memory, NULL);
        vk.multisample_memory = VK_NULL_HANDLE;
    }

    if (vk.bloom_multisample_view) {
        vk.DestroyImageView(vk.device, vk.bloom_multisample_view, NULL);
        vk.bloom_multisample_view = VK_NULL_HANDLE;
    }

    if (vk.bloom_multisample_image) {
        vk.DestroyImage(vk.device, vk.bloom_multisample_image, NULL);
        vk.bloom_multisample_image = VK_NULL_HANDLE;
    }

    if (vk.bloom_multisample_memory) {
        vk.FreeMemory(vk.device, vk.bloom_multisample_memory, NULL);
        vk.bloom_multisample_memory = VK_NULL_HANDLE;
    }

    if (vk.depth_view) {
        vk.DestroyImageView(vk.device, vk.depth_view, NULL);
        vk.depth_view = VK_NULL_HANDLE;
    }
    if (vk.depth_sample_view) {
        vk.DestroyImageView(vk.device, vk.depth_sample_view, NULL);
        vk.depth_sample_view = VK_NULL_HANDLE;
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

    if (vk.bloom_render_pass) {
        vk.DestroyRenderPass(vk.device, vk.bloom_render_pass, NULL);
        vk.bloom_render_pass = VK_NULL_HANDLE;
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

    if (vk.image_fences) {
        Z_Free(vk.image_fences);
        vk.image_fences = NULL;
    }

    if (vk.render_finished) {
        for (uint32_t i = 0; i < vk.swapchain_image_count; i++)
            if (vk.render_finished[i])
                vk.DestroySemaphore(vk.device, vk.render_finished[i], NULL);
        Z_Free(vk.render_finished);
        vk.render_finished = NULL;
    }

    if (vk.swapchain) {
        vk.DestroySwapchainKHR(vk.device, vk.swapchain, NULL);
        vk.swapchain = VK_NULL_HANDLE;
    }

    vk.swapchain_image_count = 0;
}

bool vk_recreate_swapchain(const char *reason)
{
    if (!vk.device)
        return false;

    int width = r_config.width;
    int height = r_config.height;

    vk_destroy_swapchain();
    if (!vk_create_swapchain(width, height)) {
        if (vk.swapchain_deferred) {
            vk.swapchain_retry_time = Sys_Milliseconds() + 1000;
            vk_destroy_swapchain();
            return false;
        }
        if (!vk.swapchain_recreate_failed) {
            Com_EPrintf("Couldn't recreate Vulkan swapchain after %s: %s\n",
                        reason, Com_GetLastError());
        }
        vk.swapchain_recreate_failed = true;
        vk.swapchain_retry_time = Sys_Milliseconds() + 1000;
        vk_destroy_swapchain();
        return false;
    }

    vk.swapchain_recreate_failed = false;
    vk.swapchain_retry_time = 0;
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
        .samples = vk.sample_count,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT,
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
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                (vk_depth_format_has_stencil(vk.depth_format) ?
                 VK_IMAGE_ASPECT_STENCIL_BIT : 0),
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    result = vk.CreateImageView(vk.device, &view_info, NULL, &vk.depth_view);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImageView", result);

    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    result = vk.CreateImageView(vk.device, &view_info, NULL,
                                &vk.depth_sample_view);
    if (result != VK_SUCCESS) {
        Com_WPrintf("Couldn't create sampled Vulkan depth view; SSR reflections will be disabled\n");
        vk.depth_sample_view = VK_NULL_HANDLE;
    }
    vk.depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    return true;
}

static bool vk_create_multisample_resources(void)
{
    if (vk.sample_count == VK_SAMPLE_COUNT_1_BIT)
        return true;

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = vk.swapchain_format,
        .extent = {
            .width = vk.swapchain_extent.width,
            .height = vk.swapchain_extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk.sample_count,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkResult result = vk.CreateImage(vk.device, &image_info, NULL,
                                     &vk.multisample_image);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImage", result);

    VkMemoryRequirements req;
    vk.GetImageMemoryRequirements(vk.device, vk.multisample_image, &req);
    uint32_t memory_type = vk_find_memory_type(req.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("No suitable Vulkan multisample memory type");
        return false;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = memory_type,
    };
    result = vk.AllocateMemory(vk.device, &alloc_info, NULL,
                               &vk.multisample_memory);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateMemory", result);

    result = vk.BindImageMemory(vk.device, vk.multisample_image,
                                vk.multisample_memory, 0);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkBindImageMemory", result);

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk.multisample_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = vk.swapchain_format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    result = vk.CreateImageView(vk.device, &view_info, NULL,
                                &vk.multisample_view);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImageView", result);

    if (!gl_bloom || gl_bloom->integer <= 0)
        return true;

    // Bloom MRT needs an independent multisampled color attachment so scene
    // color and bloom contribution can resolve in the same subpass.
    result = vk.CreateImage(vk.device, &image_info, NULL,
                            &vk.bloom_multisample_image);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImage(bloom multisample)", result);

    vk.GetImageMemoryRequirements(vk.device, vk.bloom_multisample_image, &req);
    memory_type = vk_find_memory_type(req.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        Com_SetLastError("No suitable Vulkan bloom multisample memory type");
        return false;
    }
    alloc_info.allocationSize = req.size;
    alloc_info.memoryTypeIndex = memory_type;
    result = vk.AllocateMemory(vk.device, &alloc_info, NULL,
                               &vk.bloom_multisample_memory);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkAllocateMemory(bloom multisample)", result);

    result = vk.BindImageMemory(vk.device, vk.bloom_multisample_image,
                                vk.bloom_multisample_memory, 0);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkBindImageMemory(bloom multisample)", result);

    view_info.image = vk.bloom_multisample_image;
    result = vk.CreateImageView(vk.device, &view_info, NULL,
                                &vk.bloom_multisample_view);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateImageView(bloom multisample)", result);

    return true;
}

static bool vk_create_framebuffers(void)
{
    vk.framebuffers = Z_Mallocz(sizeof(*vk.framebuffers) * vk.swapchain_image_count);

    for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
        bool multisampled = vk.sample_count != VK_SAMPLE_COUNT_1_BIT;
        VkImageView attachments[5];
        uint32_t attachment_count;
        if (vk.mrt_bloom) {
            attachments[0] = multisampled ? vk.multisample_view : vk.swapchain_views[i];
            attachments[1] = multisampled ? vk.bloom_multisample_view :
                                             vk.bloom_source_texture.view;
            attachments[2] = vk.depth_view;
            if (multisampled) {
                attachments[3] = vk.swapchain_views[i];
                attachments[4] = vk.bloom_source_texture.view;
            }
            attachment_count = multisampled ? 5 : 3;
        } else {
            attachments[0] = multisampled ? vk.multisample_view : vk.swapchain_views[i];
            attachments[1] = vk.depth_view;
            if (multisampled)
                attachments[2] = vk.swapchain_views[i];
            attachment_count = multisampled ? 3 : 2;
        }
        VkFramebufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vk.render_pass,
            .attachmentCount = attachment_count,
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

static bool vk_create_scene_target(void)
{
    uint32_t bloom_downsample = vk_bloom_downsample_value();
    struct {
        vk_texture_t *texture;
        VkImageLayout *layout;
        uint32_t width;
        uint32_t height;
    } targets[] = {
        { &vk.scene_texture, &vk.scene_layout,
          vk.swapchain_extent.width, vk.swapchain_extent.height },
        { &vk.bloom_source_texture, &vk.bloom_source_layout,
          vk.swapchain_extent.width, vk.swapchain_extent.height },
        { &vk.bloom_texture, &vk.bloom_layout,
          max(vk.swapchain_extent.width / bloom_downsample, 1),
          max(vk.swapchain_extent.height / bloom_downsample, 1) },
        { &vk.blur_texture, &vk.blur_layout,
          max(vk.swapchain_extent.width / bloom_downsample, 1),
          max(vk.swapchain_extent.height / bloom_downsample, 1) },
        { &vk.ssr_texture, &vk.ssr_layout,
          max(vk.swapchain_extent.width / 2, 1),
          max(vk.swapchain_extent.height / 2, 1) },
        { &vk.ssr_resolve_texture, &vk.ssr_resolve_layout,
          vk.swapchain_extent.width, vk.swapchain_extent.height },
    };

    for (size_t i = 0; i < q_countof(targets); i++) {
        if (!vk_create_color_target(targets[i].texture,
                                    targets[i].width,
                                    targets[i].height,
                                    vk.swapchain_format, 0))
            return false;

        vk_update_texture_descriptor_with_sampler(targets[i].texture,
                                                  vk.postprocess_sampler);

        *targets[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    bool multisampled = vk.sample_count != VK_SAMPLE_COUNT_1_BIT;
    VkImageView scene_attachments[5];
    uint32_t scene_attachment_count;
    if (vk.mrt_bloom) {
        scene_attachments[0] = multisampled ? vk.multisample_view : vk.scene_texture.view;
        scene_attachments[1] = multisampled ? vk.bloom_multisample_view :
                                              vk.bloom_source_texture.view;
        scene_attachments[2] = vk.depth_view;
        if (multisampled) {
            scene_attachments[3] = vk.scene_texture.view;
            scene_attachments[4] = vk.bloom_source_texture.view;
        }
        scene_attachment_count = multisampled ? 5 : 3;
    } else {
        scene_attachments[0] = multisampled ? vk.multisample_view : vk.scene_texture.view;
        scene_attachments[1] = vk.depth_view;
        if (multisampled)
            scene_attachments[2] = vk.scene_texture.view;
        scene_attachment_count = multisampled ? 3 : 2;
    }
    VkFramebufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = vk.render_pass,
        .attachmentCount = scene_attachment_count,
        .pAttachments = scene_attachments,
        .width = vk.swapchain_extent.width,
        .height = vk.swapchain_extent.height,
        .layers = 1,
    };
    VkResult result = vk.CreateFramebuffer(vk.device, &create_info, NULL,
                                           &vk.scene_framebuffer);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkCreateFramebuffer(scene MRT)", result);

    struct {
        vk_texture_t *texture;
        VkFramebuffer *framebuffer;
    } post_targets[] = {
        { &vk.bloom_texture, &vk.bloom_framebuffer },
        { &vk.blur_texture, &vk.blur_framebuffer },
        { &vk.ssr_texture, &vk.ssr_framebuffer },
        { &vk.ssr_resolve_texture, &vk.ssr_resolve_framebuffer },
    };
    for (size_t i = 0; i < q_countof(post_targets); i++) {
        VkImageView attachment = post_targets[i].texture->view;
        create_info.renderPass = vk.bloom_render_pass;
        create_info.attachmentCount = 1;
        create_info.pAttachments = &attachment;
        create_info.width = post_targets[i].texture->width;
        create_info.height = post_targets[i].texture->height;
        result = vk.CreateFramebuffer(vk.device, &create_info, NULL,
                                      post_targets[i].framebuffer);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateFramebuffer(bloom postprocess)", result);
    }

    vk.ssr_ready = false;
    if (vk.sample_count == VK_SAMPLE_COUNT_1_BIT && vk.ssr_pipeline_layout &&
        vk.ssr_descriptor_set && vk.ssr_resolve_descriptor_set &&
        vk.depth_sample_view && vk.scene_texture.view && vk.ssr_framebuffer &&
        vk.ssr_resolve_framebuffer) {
        VkDescriptorImageInfo images[3] = {
            {
                .sampler = vk.postprocess_sampler,
                .imageView = vk.scene_texture.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            {
                .sampler = vk.postprocess_sampler,
                .imageView = vk.depth_sample_view,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            },
            {
                .sampler = vk.postprocess_sampler,
                .imageView = vk.bloom_source_texture.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
        };
        VkDescriptorImageInfo resolve_images[3] = {
            {
                .sampler = vk.postprocess_sampler,
                .imageView = vk.ssr_texture.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            {
                .sampler = vk.postprocess_sampler,
                .imageView = vk.depth_sample_view,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            },
            {
                .sampler = vk.postprocess_sampler,
                .imageView = vk.bloom_source_texture.view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
        };
        VkWriteDescriptorSet writes[6];
        memset(writes, 0, sizeof(writes));
        for (uint32_t i = 0; i < 3; i++) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = vk.ssr_descriptor_set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[i];
            writes[i + 3] = writes[i];
            writes[i + 3].dstSet = vk.ssr_resolve_descriptor_set;
            writes[i + 3].pImageInfo = &resolve_images[i];
        }
        vk.UpdateDescriptorSets(vk.device, q_countof(writes), writes, 0, NULL);
    }

    return true;
}

int vk_pixel_lightmap_mode(void)
{
    if (!vk_pixel_lightmaps)
        return 0;
    return Cvar_ClampInteger(vk_pixel_lightmaps, 0, 2);
}

static const VkDynamicState vk_3d_dynamic_states[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
    VK_DYNAMIC_STATE_LINE_WIDTH,
};

const VkPipelineDynamicStateCreateInfo vk_3d_dynamic_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = q_countof(vk_3d_dynamic_states),
    .pDynamicStates = vk_3d_dynamic_states,
};

uint32_t vk_frames_in_flight_value(void)
{
    int value = vk_frames_in_flight ?
        Cvar_ClampInteger(vk_frames_in_flight, 1, VK_MAX_FRAMES_IN_FLIGHT) : 2;

    return max(value, 1);
}

// Ray-traced reflections keep each pixel's material in the second color
// attachment that bloom also uses.
static bool vk_rt_reflections_requested(void)
{
    return vk_raytracing && vk_raytracing->integer && vk_rt_reflections &&
        vk_rt_reflections->value > 0.001f;
}

bool vk_mrt_requested(void)
{
    return vk_bloom_requested() || vk_rt_reflections_requested();
}

bool vk_create_swapchain(int width, int height)
{
    VkSurfaceCapabilitiesKHR caps;
    vk.swapchain_deferred = false;

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
    if (!extent.width || !extent.height) {
        vk.swapchain_deferred = true;
        return false;
    }

    uint32_t queue_indices[] = { vk.queues.graphics_family, vk.queues.present_family };

    vk.swapchain_transfer_src =
        (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    VkImageUsageFlags image_usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (vk.swapchain_transfer_src)
        image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk.surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = image_usage,
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
    vk.present_mode = present_mode;
    vk.sample_count = vk_choose_sample_count();
    vk.depth_format = vk_choose_depth_format(vk_shadow_stencil_requested());
    if (vk.depth_format == VK_FORMAT_UNDEFINED) {
        Com_SetLastError("No supported Vulkan depth format");
        return false;
    }
    Com_Printf("Vulkan depth format: %s, shadow stencil %s, multisampling %ux\n",
               vk_format_name(vk.depth_format),
               vk_shadow_stencil_enabled() ? "enabled" : "disabled",
               (unsigned)vk.sample_count);
    vk.swapchain_extent = extent;

    result = vk.GetSwapchainImagesKHR(vk.device, vk.swapchain, &vk.swapchain_image_count, NULL);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkGetSwapchainImagesKHR", result);
    if (!vk.swapchain_image_count) {
        Com_SetLastError("Vulkan swapchain has no images");
        return false;
    }

    vk.swapchain_images = Z_Malloc(sizeof(*vk.swapchain_images) * vk.swapchain_image_count);
    result = vk.GetSwapchainImagesKHR(vk.device, vk.swapchain,
                                      &vk.swapchain_image_count, vk.swapchain_images);
    if (result != VK_SUCCESS)
        return vk_fail_result("vkGetSwapchainImagesKHR", result);

    vk.swapchain_views = Z_Mallocz(sizeof(*vk.swapchain_views) * vk.swapchain_image_count);
    vk.swapchain_layouts = Z_Malloc(sizeof(*vk.swapchain_layouts) * vk.swapchain_image_count);
    vk.image_fences = Z_Mallocz(sizeof(*vk.image_fences) * vk.swapchain_image_count);
    for (uint32_t i = 0; i < vk.swapchain_image_count; i++)
        vk.swapchain_layouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;

    // The semaphore a present waits on can only be reused once that present
    // has finished, which is known for certain only when the same image is
    // acquired again. So there is one per image, not one per frame in flight.
    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    vk.render_finished = Z_Mallocz(sizeof(*vk.render_finished) * vk.swapchain_image_count);
    for (uint32_t i = 0; i < vk.swapchain_image_count; i++) {
        result = vk.CreateSemaphore(vk.device, &semaphore_info, NULL,
                                    &vk.render_finished[i]);
        if (result != VK_SUCCESS)
            return vk_fail_result("vkCreateSemaphore", result);
    }

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

    VkExtent2D bloom_extent = {
        max(vk.swapchain_extent.width / vk_bloom_downsample_value(), 1),
        max(vk.swapchain_extent.height / vk_bloom_downsample_value(), 1),
    };
    vk.mrt_bloom = vk_mrt_requested() && vk_bloom_supported();

    VkPipelineDepthStencilStateCreateInfo shadow_depth_stencil =
        vk_shadow_depth_stencil_state();

    if (!vk_validate_pixel_lightmap_shaders() ||
        !vk_create_pixel_world_pipeline_layout() ||
        !vk_create_render_pass() ||
        !vk_create_rect_pipeline() ||
        !vk_create_rect_pipeline_ex(&vk.vignette_pipeline,
                                    vk_vignette_vert_spv,
                                    sizeof(vk_vignette_vert_spv)) ||
        !vk_create_texture_pipeline() ||
        !vk_create_texture_pipeline_ex(&vk.scene_pipeline,
                                       vk_scene_frag_spv,
                                       sizeof(vk_scene_frag_spv),
                                       false,
                                       vk.swapchain_extent,
                                       false) ||
        !vk_create_texture_pipeline_ex(&vk.waterwarp_pipeline,
                                       vk_waterwarp_frag_spv,
                                       sizeof(vk_waterwarp_frag_spv),
                                       false,
                                       vk.swapchain_extent,
                                       false) ||
        !vk_create_texture_pipeline_ex(&vk.bloom_downscale_pipeline,
                                       vk_bloom_downscale_frag_spv,
                                       sizeof(vk_bloom_downscale_frag_spv),
                                       false,
                                       bloom_extent,
                                       true) ||
        !vk_create_texture_pipeline_ex(&vk.bloom_blur_pipeline,
                                       vk_bloom_blur_frag_spv,
                                       sizeof(vk_bloom_blur_frag_spv),
                                       false,
                                       bloom_extent,
                                       true) ||
        !vk_create_texture_pipeline_ex(&vk.bloom_add_pipeline,
                                       vk_tex_frag_spv,
                                       sizeof(vk_tex_frag_spv),
                                       true,
                                       vk.swapchain_extent,
                                       false) ||
        !vk_create_color3d_pipeline(&vk.color3d_pipeline, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) ||
        !vk_create_snow_pipeline(&vk.snow_pipeline, false, false) ||
        !vk_create_snow_pipeline(&vk.snow_full_pipeline, true, false) ||
        !vk_create_puddle_pipeline(&vk.puddle_pipeline) ||
        !vk_create_color3d_pipeline(&vk.line3d_pipeline, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE,
                                    VK_PRIMITIVE_TOPOLOGY_LINE_LIST) ||
        !vk_create_color3d_pipeline(&vk.debug_line_pipeline, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE,
                                    VK_PRIMITIVE_TOPOLOGY_LINE_LIST) ||
        !vk_create_color3d_pipeline(&vk.beam_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) ||
        !vk_create_color3d_pipeline(&vk.beam_bloom_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) ||
        !vk_create_color3d_pipeline(&vk.beam_add_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_TRUE,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) ||
        !vk_create_color3d_pipeline(&vk.beam_add_bloom_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_TRUE,
                                    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) ||
        !vk_create_world_pipeline(&vk.world_pipeline, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.world_alpha_pipeline, VK_TRUE, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.world_blend_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.world_glow_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_TRUE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_pipeline, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_alpha_pipeline, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_fast_pipeline, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_glow_pipeline, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_glow_alpha_pipeline, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE) ||
        !vk_create_pixel_world_pipeline(&vk.pixel_world_glow_fast_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE) ||
        !vk_create_world_pipeline(&vk.sky_pipeline, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.sprite_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.sprite_alpha_pipeline, VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.sprite_bloom_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.particle_add_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_world_pipeline(&vk.debug_text_pipeline, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE) ||
        !vk_create_alias_pipeline(&vk.alias_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_alpha_pipeline, VK_TRUE, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_depth_pipeline, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_blend_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_bloom_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        // The projection flips the models' winding, so their front faces
        // are the ones Vulkan calls back faces (see alias_cel_pipeline).
        !vk_create_alias_pipeline(&vk.alias_shell_bloom_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_FRONT_BIT, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_shadow_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_TRUE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_TRUE, -1.0f, -2.0f, &shadow_depth_stencil) ||
        !vk_create_alias_pipeline(&vk.alias_cel_pipeline, VK_FALSE, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_BACK_BIT, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_COMPARE_OP_LESS, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_alias_pipeline(&vk.alias_line_pipeline, VK_FALSE, VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL,
                                  VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_COMPARE_OP_LESS_OR_EQUAL, VK_FALSE, 0.0f, 0.0f, NULL) ||
        !vk_create_depth_resources() ||
        !vk_create_multisample_resources() ||
        !vk_create_scene_target() ||
        !vk_create_framebuffers())
        return false;

    if (vk.sample_count == VK_SAMPLE_COUNT_1_BIT && vk.ssr_pipeline_layout &&
        vk.ssr_descriptor_set && vk.ssr_resolve_descriptor_set &&
        vk.depth_sample_view && vk.ssr_framebuffer &&
        vk.ssr_resolve_framebuffer) {
        if (vk_create_ssr_pipeline()) {
            vk.ssr_ready = true;
            Com_Printf("Vulkan SSR: half resolution, 10-14 adaptive steps, full-resolution resolve\n");
        } else {
            Com_WPrintf("Couldn't create Vulkan SSR pipeline: %s; reflections disabled\n",
                        Com_GetLastError());
        }
    } else if (vk.sample_count != VK_SAMPLE_COUNT_1_BIT) {
        Com_WPrintf("Vulkan SSR requires multisampling 1x; reflections disabled\n");
    }

#if USE_VULKAN_RAYTRACING
    // snow with shadows from dynamic lights; without, the snow is drawn
    // with the pipelines above
    if (vk.raytracing_active &&
        (!vk_create_snow_pipeline(&vk.snow_rt_pipelines[0], false, true) ||
         !vk_create_snow_pipeline(&vk.snow_rt_pipelines[1], true, true)))
        Com_WPrintf("Couldn't create Vulkan RT snow pipelines: %s; the snow cover gets no shadows\n",
                    Com_GetLastError());

    vk.raytracing_pixel_ready = false;
    if (vk.raytracing_active) {
        bool rt_ok =
            vk_create_pixel_world_pipeline(&vk.pixel_world_rt_pipeline,
                                           VK_FALSE, VK_FALSE, VK_FALSE, VK_TRUE) &&
            vk_create_pixel_world_pipeline(&vk.pixel_world_rt_alpha_pipeline,
                                           VK_TRUE, VK_FALSE, VK_FALSE, VK_TRUE) &&
            vk_create_pixel_world_pipeline(&vk.pixel_world_rt_glow_pipeline,
                                           VK_FALSE, VK_TRUE, VK_FALSE, VK_TRUE) &&
            vk_create_pixel_world_pipeline(&vk.pixel_world_rt_glow_alpha_pipeline,
                                           VK_TRUE, VK_TRUE, VK_FALSE, VK_TRUE);
        if (rt_ok) {
            vk.raytracing_pixel_ready = true;
            Com_Printf("Vulkan RT fragment sharing: %s\n",
                       vk.raytracing_quad_sharing ?
                       "adaptive 2x2 (static >384, dynamic >512)" :
                       "unsupported (per-pixel fallback)");
        } else {
            Com_WPrintf("Couldn't create Vulkan RT pixel-lightmap pipelines: %s; using raster pixel lighting\n",
                        Com_GetLastError());
            VkPipeline *rt_pipelines[] = {
                &vk.pixel_world_rt_pipeline,
                &vk.pixel_world_rt_alpha_pipeline,
                &vk.pixel_world_rt_glow_pipeline,
                &vk.pixel_world_rt_glow_alpha_pipeline,
            };
            for (uint32_t i = 0; i < q_countof(rt_pipelines); i++) {
                if (*rt_pipelines[i]) {
                    vk.DestroyPipeline(vk.device, *rt_pipelines[i], NULL);
                    *rt_pipelines[i] = VK_NULL_HANDLE;
                }
            }
        }
    }
#endif

    if (!vk_allocate_swapchain_commands())
        return false;

    Com_Printf("Vulkan swapchain: requested %dx%d, actual %ux%u, %u images, %s present\n",
               width, height, vk.swapchain_extent.width,
               vk.swapchain_extent.height, vk.swapchain_image_count,
               vk_present_mode_name(vk.present_mode));
    return true;
}

#endif // USE_VULKAN
