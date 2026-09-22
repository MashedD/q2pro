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

static q2_vk_presentation_queue_topology_t
q2_vk_presentation_queue_topology(
    const q2_vk_presentation_topology_t *topology)
{
    if (!topology || !topology->queue_family_facts_known)
        return Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_UNKNOWN;
    if (topology->graphics_queue_family != topology->present_queue_family)
        return Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_SEPARATE_PRESENT_QUEUE;
    return topology->graphics_queue_index == topology->present_queue_index ?
        Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_SINGLE_QUEUE :
        Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_SHARED_FAMILY;
}

static bool q2_vk_presentation_provider_sync_ready(
    const q2_vk_presentation_adapter_t *adapter)
{
    return adapter &&
        adapter->mode == Q2_VK_PRESENTATION_FRAME_INTERPOLATION &&
        adapter->ops.topology.queue_family_facts_known &&
        adapter->ops.topology.provider_synchronization_ready;
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

q2_vk_presentation_provider_capabilities_t
Q2_VK_PresentationAdapterProviderCapabilities(
    const q2_vk_presentation_adapter_t *adapter)
{
    q2_vk_presentation_provider_capabilities_t capabilities = {
        .prerequisites_compiled =
            Q2_VK_PresentationAdapterProviderBuildCompiled(),
        .runtime_ready = Q2_VK_PresentationAdapterFrameGenerationEnabled(adapter),
        .lifecycle_capable = adapter && adapter->ops.wait_idle &&
            adapter->ops.recreate,
        .queue_topology = q2_vk_presentation_queue_topology(
            adapter ? &adapter->ops.topology : NULL),
        .sync_contract = adapter ? adapter->ops.topology.sync_contract :
            (q2_vk_presentation_sync_contract_t){0},
        .native_sync_facts_known = adapter &&
            adapter->ops.topology.native_sync_facts_known,
        .native_sync_facts = adapter ? adapter->ops.topology.native_sync_facts : 0,
        .provider_synchronization_ready =
            q2_vk_presentation_provider_sync_ready(adapter),
        .diagnostic = NULL,
    };

    if (!capabilities.prerequisites_compiled) {
        capabilities.diagnostic =
            Q2_VK_PresentationAdapterProviderBuildReason();
    } else if (!adapter) {
        capabilities.diagnostic = "presentation adapter is not created";
    } else if (!capabilities.runtime_ready) {
        capabilities.diagnostic = "frame-generation runtime is not ready";
    } else if (!capabilities.lifecycle_capable) {
        capabilities.diagnostic =
            "presentation lifecycle callbacks are incomplete";
    } else if (capabilities.queue_topology ==
               Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_UNKNOWN) {
        capabilities.diagnostic =
            "provider queue-family topology is unknown";
    } else if (!capabilities.provider_synchronization_ready) {
        const q2_vk_presentation_sync_contract_t *contract =
            &capabilities.sync_contract;
        if (!capabilities.native_sync_facts_known) {
            capabilities.diagnostic = "native synchronization facts are unknown";
        } else if (contract->acquire_signal ==
                Q2_VK_PRESENTATION_SYNC_BINARY_SEMAPHORE &&
            contract->render_finished_signal ==
                Q2_VK_PRESENTATION_SYNC_BINARY_SEMAPHORE &&
            contract->frame_completion == Q2_VK_PRESENTATION_SYNC_FENCE &&
            contract->image_reuse == Q2_VK_PRESENTATION_SYNC_FENCE &&
            !contract->timeline_semaphore_supported &&
            !contract->synchronization2_supported) {
            capabilities.diagnostic =
                "provider synchronization readiness is not declared "
                "(native binary semaphore/fence lifecycle)";
        } else {
            capabilities.diagnostic =
                "provider synchronization readiness is not declared";
        }
    } else {
        capabilities.diagnostic =
            "provider prerequisites and presentation lifecycle are ready";
    }
    return capabilities;
}

bool Q2_VK_PresentationAdapterProviderReady(
    const q2_vk_presentation_adapter_t *adapter)
{
    const q2_vk_presentation_provider_capabilities_t capabilities =
        Q2_VK_PresentationAdapterProviderCapabilities(adapter);
    return capabilities.prerequisites_compiled && capabilities.runtime_ready &&
        capabilities.lifecycle_capable && capabilities.provider_synchronization_ready;
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
