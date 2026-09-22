/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 or (at your option) any
later version.
*/

#include "vk_presentation_adapter.h"

#include <stdlib.h>
#include <string.h>

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#ifndef Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_COMPILED
#define Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_COMPILED 0
#endif
#ifndef Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_PLATFORM
#define Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_PLATFORM "unknown"
#endif
#ifndef Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_REASON
#define Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_REASON \
    "real FidelityFX frame-interpolation swapchain provider is not compiled"
#endif

struct q2_vk_presentation_adapter {
    q2_vk_presentation_mode_t mode;
    q2_vk_presentation_ops_t ops;
    bool active;
    bool acquired;
    bool shutdown;
};

static bool q2_vk_presentation_result_is_acquired(VkResult result)
{
    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

static bool q2_vk_presentation_ops_valid(const q2_vk_presentation_ops_t *ops)
{
    return ops && ops->acquire && ops->present && ops->shutdown;
}

bool Q2_VK_PresentationAdapterProviderBuildCompiled(void)
{
    return Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_COMPILED != 0;
}

const char *Q2_VK_PresentationAdapterProviderBuildPlatform(void)
{
    return Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_PLATFORM;
}

const char *Q2_VK_PresentationAdapterProviderBuildReason(void)
{
    return Q2_FSR3_FRAME_INTERPOLATION_PROVIDER_REASON;
}

bool Q2_VK_PresentationAdapterProviderReady(
    const q2_vk_presentation_adapter_t *adapter)
{
    return Q2_VK_PresentationAdapterFrameGenerationEnabled(adapter) &&
        Q2_VK_PresentationAdapterProviderBuildCompiled();
}

q2_vk_presentation_adapter_t *Q2_VK_PresentationAdapterCreate(
    q2_vk_presentation_mode_t mode,
    const q2_vk_presentation_ops_t *ops)
{
    if (!q2_vk_presentation_ops_valid(ops))
        return NULL;

    q2_vk_presentation_adapter_t *adapter =
        (q2_vk_presentation_adapter_t *)calloc(1, sizeof(*adapter));
    if (!adapter)
        return NULL;

    adapter->mode = mode;
    adapter->ops = *ops;
    adapter->active = true;
    return adapter;
}

q2_vk_presentation_provider_status_t
Q2_VK_PresentationAdapterProviderStatus(
    const q2_vk_presentation_adapter_t *adapter)
{
    if (Q2_VK_PresentationAdapterProviderReady(adapter))
        return Q2_VK_PRESENTATION_PROVIDER_ACTIVE;
    if (Q2_VK_PresentationAdapterProviderBuildCompiled())
        return Q2_VK_PRESENTATION_PROVIDER_BUILT;
    return Q2_VK_PRESENTATION_PROVIDER_UNAVAILABLE;
}

void Q2_VK_PresentationAdapterShutdown(
    q2_vk_presentation_adapter_t *adapter,
    q2_vk_presentation_shutdown_t reason)
{
    if (!adapter || adapter->shutdown)
        return;

    /* Device-loss shutdown deliberately does not call wait_idle here. The
     * provider must stop host-side work without touching a lost device. */
    adapter->ops.shutdown(adapter->ops.userdata, reason);
    adapter->shutdown = true;
    adapter->active = false;
    adapter->acquired = false;
}

void Q2_VK_PresentationAdapterDestroy(
    q2_vk_presentation_adapter_t *adapter,
    q2_vk_presentation_shutdown_t reason)
{
    if (!adapter)
        return;
    Q2_VK_PresentationAdapterShutdown(adapter, reason);
    free(adapter);
}

bool Q2_VK_PresentationAdapterFrameGenerationEnabled(
    const q2_vk_presentation_adapter_t *adapter)
{
    return adapter && adapter->active && !adapter->shutdown &&
        adapter->mode == Q2_VK_PRESENTATION_FRAME_INTERPOLATION &&
        adapter->ops.frame_generation_ready;
}

bool Q2_VK_PresentationAdapterHasAcquiredImage(
    const q2_vk_presentation_adapter_t *adapter)
{
    return adapter && adapter->acquired;
}

VkResult Q2_VK_PresentationAdapterAcquire(
    q2_vk_presentation_adapter_t *adapter,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence,
    uint32_t *image_index)
{
    if (!adapter || !adapter->active || adapter->shutdown ||
        adapter->acquired || !image_index)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = adapter->ops.acquire(adapter->ops.userdata, timeout,
                                           semaphore, fence, image_index);
    if (q2_vk_presentation_result_is_acquired(result))
        adapter->acquired = true;
    return result;
}

VkResult Q2_VK_PresentationAdapterPresent(
    q2_vk_presentation_adapter_t *adapter,
    VkQueue queue,
    const VkPresentInfoKHR *present_info)
{
    if (!adapter || !adapter->active || adapter->shutdown ||
        !adapter->acquired || !present_info)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = adapter->ops.present(adapter->ops.userdata, queue,
                                           present_info);
    if (q2_vk_presentation_result_is_acquired(result))
        adapter->acquired = false;
    return result;
}

VkResult Q2_VK_PresentationAdapterRecreate(
    q2_vk_presentation_adapter_t *adapter,
    VkSwapchainKHR old_swapchain)
{
    if (!adapter || !adapter->active || adapter->shutdown ||
        !adapter->ops.wait_idle || !adapter->ops.recreate)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = adapter->ops.wait_idle(adapter->ops.userdata);
    if (result != VK_SUCCESS)
        return result;
    result = adapter->ops.recreate(adapter->ops.userdata, old_swapchain);
    if (result == VK_SUCCESS)
        adapter->acquired = false;
    return result;
}

void Q2_VK_PresentationAdapterMarkRecreated(
    q2_vk_presentation_adapter_t *adapter)
{
    if (!adapter || adapter->shutdown)
        return;
    adapter->acquired = false;
}

void Q2_VK_PresentationAdapterAbortFrame(
    q2_vk_presentation_adapter_t *adapter)
{
    if (!adapter || adapter->shutdown)
        return;
    adapter->acquired = false;
}
