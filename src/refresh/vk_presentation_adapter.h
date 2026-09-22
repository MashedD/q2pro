/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 or (at your option) any
later version.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

typedef struct q2_vk_presentation_adapter q2_vk_presentation_adapter_t;

typedef enum {
    Q2_VK_PRESENTATION_NATIVE = 0,
    Q2_VK_PRESENTATION_FRAME_INTERPOLATION = 1
} q2_vk_presentation_mode_t;

typedef enum {
    Q2_VK_PRESENTATION_PROVIDER_UNAVAILABLE = 0,
    Q2_VK_PRESENTATION_PROVIDER_BUILT = 1,
    Q2_VK_PRESENTATION_PROVIDER_ACTIVE = 2
} q2_vk_presentation_provider_status_t;

typedef enum {
    Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_UNKNOWN = 0,
    Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_SINGLE_QUEUE = 1,
    Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_SHARED_FAMILY = 2,
    Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_SEPARATE_PRESENT_QUEUE = 3
} q2_vk_presentation_queue_topology_t;

/* CPU-side facts supplied by a future provider integration. This deliberately
 * contains no Vulkan handles or synchronization objects: native presentation
 * can leave it unknown until the provider owns the queue-family contract. */
typedef struct {
    bool queue_family_facts_known;
    uint32_t graphics_queue_family;
    uint32_t graphics_queue_index;
    uint32_t present_queue_family;
    uint32_t present_queue_index;
    bool provider_synchronization_ready;
} q2_vk_presentation_topology_t;

typedef struct {
    bool prerequisites_compiled;
    bool runtime_ready;
    bool lifecycle_capable;
    q2_vk_presentation_queue_topology_t queue_topology;
    bool provider_synchronization_ready;
    const char *diagnostic;
} q2_vk_presentation_provider_capabilities_t;

typedef enum {
    Q2_VK_PRESENTATION_SHUTDOWN_NORMAL = 0,
    Q2_VK_PRESENTATION_SHUTDOWN_DEVICE_LOST = 1
} q2_vk_presentation_shutdown_t;

typedef VkResult (*q2_vk_presentation_acquire_fn)(
    void *userdata,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence,
    uint32_t *image_index);
typedef VkResult (*q2_vk_presentation_present_fn)(
    void *userdata,
    VkQueue queue,
    const VkPresentInfoKHR *present_info);
typedef VkResult (*q2_vk_presentation_wait_fn)(void *userdata);
typedef VkResult (*q2_vk_presentation_recreate_fn)(
    void *userdata,
    VkSwapchainKHR old_swapchain);
typedef void (*q2_vk_presentation_shutdown_fn)(
    void *userdata,
    q2_vk_presentation_shutdown_t reason);

typedef struct {
    void *userdata;
    bool frame_generation_ready;
    q2_vk_presentation_topology_t topology;
    q2_vk_presentation_acquire_fn acquire;
    q2_vk_presentation_present_fn present;
    q2_vk_presentation_wait_fn wait_idle;
    q2_vk_presentation_recreate_fn recreate;
    q2_vk_presentation_shutdown_fn shutdown;
} q2_vk_presentation_ops_t;

q2_vk_presentation_adapter_t *Q2_VK_PresentationAdapterCreate(
    q2_vk_presentation_mode_t mode,
    const q2_vk_presentation_ops_t *ops);
bool Q2_VK_PresentationAdapterProviderBuildCompiled(void);
const char *Q2_VK_PresentationAdapterProviderBuildPlatform(void);
const char *Q2_VK_PresentationAdapterProviderBuildReason(void);
q2_vk_presentation_provider_capabilities_t
Q2_VK_PresentationAdapterProviderCapabilities(
    const q2_vk_presentation_adapter_t *adapter);
bool Q2_VK_PresentationAdapterProviderReady(
    const q2_vk_presentation_adapter_t *adapter);
q2_vk_presentation_provider_status_t
Q2_VK_PresentationAdapterProviderStatus(
    const q2_vk_presentation_adapter_t *adapter);
void Q2_VK_PresentationAdapterDestroy(
    q2_vk_presentation_adapter_t *adapter,
    q2_vk_presentation_shutdown_t reason);

bool Q2_VK_PresentationAdapterFrameGenerationEnabled(
    const q2_vk_presentation_adapter_t *adapter);
bool Q2_VK_PresentationAdapterHasAcquiredImage(
    const q2_vk_presentation_adapter_t *adapter);

VkResult Q2_VK_PresentationAdapterAcquire(
    q2_vk_presentation_adapter_t *adapter,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence,
    uint32_t *image_index);
VkResult Q2_VK_PresentationAdapterPresent(
    q2_vk_presentation_adapter_t *adapter,
    VkQueue queue,
    const VkPresentInfoKHR *present_info);
VkResult Q2_VK_PresentationAdapterRecreate(
    q2_vk_presentation_adapter_t *adapter,
    VkSwapchainKHR old_swapchain);
/* Notify a native adapter after the renderer has completed its own
 * wait-and-recreate sequence. This keeps the adapter state synchronized
 * without making the native path recursively call the renderer. */
void Q2_VK_PresentationAdapterMarkRecreated(
    q2_vk_presentation_adapter_t *adapter);
void Q2_VK_PresentationAdapterAbortFrame(
    q2_vk_presentation_adapter_t *adapter);
void Q2_VK_PresentationAdapterShutdown(
    q2_vk_presentation_adapter_t *adapter,
    q2_vk_presentation_shutdown_t reason);
