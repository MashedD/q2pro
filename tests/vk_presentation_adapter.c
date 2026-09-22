#include "src/refresh/vk_presentation_adapter.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    MOCK_ACQUIRE = 1,
    MOCK_PRESENT = 2,
    MOCK_WAIT = 3,
    MOCK_RECREATE = 4,
    MOCK_SHUTDOWN = 5
};

typedef struct {
    int events[16];
    size_t event_count;
    int shutdown_reason;
    VkResult acquire_result;
    VkResult present_result;
    VkResult wait_result;
} mock_state_t;

static void mock_event(mock_state_t *state, int event)
{
    assert(state->event_count < sizeof(state->events) / sizeof(state->events[0]));
    state->events[state->event_count++] = event;
}

static VkResult mock_acquire(void *userdata, uint64_t timeout,
                             VkSemaphore semaphore, VkFence fence,
                             uint32_t *image_index)
{
    (void)timeout;
    (void)semaphore;
    (void)fence;
    mock_state_t *state = (mock_state_t *)userdata;
    mock_event(state, MOCK_ACQUIRE);
    *image_index = 7;
    return state->acquire_result;
}

static VkResult mock_present(void *userdata, VkQueue queue,
                             const VkPresentInfoKHR *present_info)
{
    (void)queue;
    (void)present_info;
    mock_state_t *state = (mock_state_t *)userdata;
    mock_event(state, MOCK_PRESENT);
    return state->present_result;
}

static VkResult mock_wait(void *userdata)
{
    mock_state_t *state = (mock_state_t *)userdata;
    mock_event(state, MOCK_WAIT);
    return state->wait_result;
}

static VkResult mock_recreate(void *userdata, VkSwapchainKHR old_swapchain)
{
    (void)old_swapchain;
    mock_event((mock_state_t *)userdata, MOCK_RECREATE);
    return VK_SUCCESS;
}

static void mock_shutdown(void *userdata,
                          q2_vk_presentation_shutdown_t reason)
{
    mock_state_t *state = (mock_state_t *)userdata;
    mock_event(state, MOCK_SHUTDOWN);
    state->shutdown_reason = (int)reason;
}

static q2_vk_presentation_adapter_t *make_adapter(
    mock_state_t *state, bool frame_generation_ready)
{
    const q2_vk_presentation_ops_t ops = {
        .userdata = state,
        .frame_generation_ready = frame_generation_ready,
        .provider_swapchain_owned = true,
        .topology = {
            .queue_family_facts_known = true,
            .graphics_queue_family = 0,
            .graphics_queue_index = 0,
            .present_queue_family = 1,
            .present_queue_index = 0,
            .sync_contract = {
                .acquire_signal = Q2_VK_PRESENTATION_SYNC_BINARY_SEMAPHORE,
                .render_finished_signal =
                    Q2_VK_PRESENTATION_SYNC_BINARY_SEMAPHORE,
                .frame_completion = Q2_VK_PRESENTATION_SYNC_FENCE,
                .image_reuse = Q2_VK_PRESENTATION_SYNC_FENCE,
                .timeline_semaphore_supported = false,
                .synchronization2_supported = false,
            },
            .native_sync_facts_known = true,
            .native_sync_facts =
                Q2_VK_PRESENTATION_SYNC_ACQUIRE_BINARY |
                Q2_VK_PRESENTATION_SYNC_RENDER_FINISHED_BINARY |
                Q2_VK_PRESENTATION_SYNC_FRAME_FENCE |
                Q2_VK_PRESENTATION_SYNC_IMAGE_FENCE_ALIASES_FRAME,
            .provider_synchronization_ready = true,
        },
        .acquire = mock_acquire,
        .present = mock_present,
        .wait_idle = mock_wait,
        .recreate = mock_recreate,
        .shutdown = mock_shutdown,
    };
    return Q2_VK_PresentationAdapterCreate(
        Q2_VK_PRESENTATION_FRAME_INTERPOLATION, &ops);
}

static q2_vk_presentation_adapter_t *make_capability_adapter(
    mock_state_t *state, bool topology_known, bool synchronization_ready,
    uint32_t graphics_queue_family, uint32_t graphics_queue_index,
    uint32_t present_queue_family, uint32_t present_queue_index)
{
    const q2_vk_presentation_ops_t ops = {
        .userdata = state,
        .frame_generation_ready = true,
        .provider_swapchain_owned = true,
        .topology = {
            .queue_family_facts_known = topology_known,
            .graphics_queue_family = graphics_queue_family,
            .graphics_queue_index = graphics_queue_index,
            .present_queue_family = present_queue_family,
            .present_queue_index = present_queue_index,
            .provider_synchronization_ready = synchronization_ready,
        },
        .acquire = mock_acquire,
        .present = mock_present,
        .wait_idle = mock_wait,
        .recreate = mock_recreate,
        .shutdown = mock_shutdown,
    };
    return Q2_VK_PresentationAdapterCreate(
        Q2_VK_PRESENTATION_FRAME_INTERPOLATION, &ops);
}

static void check_ordering_and_fallback(void)
{
    mock_state_t state = {.acquire_result = VK_SUCCESS,
                          .present_result = VK_SUCCESS,
                          .wait_result = VK_SUCCESS};
    q2_vk_presentation_adapter_t *adapter = make_adapter(&state, true);
    assert(adapter);
    assert(Q2_VK_PresentationAdapterFrameGenerationEnabled(adapter));
    q2_vk_presentation_provider_capabilities_t capabilities =
        Q2_VK_PresentationAdapterProviderCapabilities(adapter);
    assert(capabilities.prerequisites_compiled ==
           Q2_VK_PresentationAdapterProviderBuildCompiled());
    assert(capabilities.runtime_ready);
    assert(capabilities.lifecycle_capable);
    assert(capabilities.provider_swapchain_owned);
    assert(capabilities.queue_topology ==
           Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_SEPARATE_PRESENT_QUEUE);
    assert(capabilities.sync_contract.acquire_signal ==
           Q2_VK_PRESENTATION_SYNC_BINARY_SEMAPHORE);
    assert(capabilities.sync_contract.render_finished_signal ==
           Q2_VK_PRESENTATION_SYNC_BINARY_SEMAPHORE);
    assert(capabilities.sync_contract.frame_completion ==
           Q2_VK_PRESENTATION_SYNC_FENCE);
    assert(capabilities.sync_contract.image_reuse ==
           Q2_VK_PRESENTATION_SYNC_FENCE);
    assert(!capabilities.sync_contract.timeline_semaphore_supported);
    assert(!capabilities.sync_contract.synchronization2_supported);
    assert(capabilities.native_sync_facts_known);
    assert((capabilities.native_sync_facts &
            (Q2_VK_PRESENTATION_SYNC_ACQUIRE_BINARY |
             Q2_VK_PRESENTATION_SYNC_RENDER_FINISHED_BINARY |
             Q2_VK_PRESENTATION_SYNC_FRAME_FENCE |
             Q2_VK_PRESENTATION_SYNC_IMAGE_FENCE_ALIASES_FRAME)) ==
           (Q2_VK_PRESENTATION_SYNC_ACQUIRE_BINARY |
            Q2_VK_PRESENTATION_SYNC_RENDER_FINISHED_BINARY |
            Q2_VK_PRESENTATION_SYNC_FRAME_FENCE |
            Q2_VK_PRESENTATION_SYNC_IMAGE_FENCE_ALIASES_FRAME));
    assert(!(capabilities.native_sync_facts &
              (Q2_VK_PRESENTATION_SYNC_TIMELINE |
               Q2_VK_PRESENTATION_SYNC_SYNCHRONIZATION2)));
    assert(capabilities.provider_synchronization_ready);
    assert(capabilities.diagnostic[0] != '\0');
    assert(Q2_VK_PresentationAdapterProviderReady(adapter) ==
           Q2_VK_PresentationAdapterProviderBuildCompiled());
    assert(!Q2_VK_PresentationAdapterHasAcquiredImage(adapter));

    VkPresentInfoKHR present_info = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    assert(Q2_VK_PresentationAdapterPresent(adapter, VK_NULL_HANDLE,
                                            &present_info) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(Q2_VK_PresentationAdapterAcquire(adapter, UINT64_MAX,
                                            VK_NULL_HANDLE, VK_NULL_HANDLE,
                                            &(uint32_t){0}) == VK_SUCCESS);
    assert(Q2_VK_PresentationAdapterHasAcquiredImage(adapter));
    assert(Q2_VK_PresentationAdapterPresent(adapter, VK_NULL_HANDLE,
                                            &present_info) == VK_SUCCESS);
    assert(!Q2_VK_PresentationAdapterHasAcquiredImage(adapter));
    assert(state.events[0] == MOCK_ACQUIRE && state.events[1] == MOCK_PRESENT);

    state.present_result = VK_ERROR_OUT_OF_HOST_MEMORY;
    assert(Q2_VK_PresentationAdapterAcquire(adapter, UINT64_MAX,
                                            VK_NULL_HANDLE, VK_NULL_HANDLE,
                                            &(uint32_t){0}) == VK_SUCCESS);
    assert(Q2_VK_PresentationAdapterPresent(adapter, VK_NULL_HANDLE,
                                            &present_info) ==
           VK_ERROR_OUT_OF_HOST_MEMORY);
    assert(Q2_VK_PresentationAdapterHasAcquiredImage(adapter));
    state.present_result = VK_SUCCESS;
    Q2_VK_PresentationAdapterAbortFrame(adapter);
    assert(!Q2_VK_PresentationAdapterHasAcquiredImage(adapter));

    state.acquire_result = VK_ERROR_OUT_OF_DATE_KHR;
    assert(Q2_VK_PresentationAdapterAcquire(adapter, UINT64_MAX,
                                            VK_NULL_HANDLE, VK_NULL_HANDLE,
                                            &(uint32_t){0}) ==
           VK_ERROR_OUT_OF_DATE_KHR);
    assert(!Q2_VK_PresentationAdapterHasAcquiredImage(adapter));
    assert(Q2_VK_PresentationAdapterRecreate(adapter, VK_NULL_HANDLE) ==
           VK_SUCCESS);
    assert(state.events[state.event_count - 2] == MOCK_WAIT);
    assert(state.events[state.event_count - 1] == MOCK_RECREATE);
    Q2_VK_PresentationAdapterDestroy(adapter, Q2_VK_PRESENTATION_SHUTDOWN_NORMAL);
}

static void check_frame_generation_gate_and_device_loss(void)
{
    mock_state_t state = {.acquire_result = VK_SUCCESS,
                          .present_result = VK_SUCCESS,
                          .wait_result = VK_SUCCESS};
    q2_vk_presentation_adapter_t *adapter = make_adapter(&state, false);
    assert(adapter);
    assert(!Q2_VK_PresentationAdapterFrameGenerationEnabled(adapter));
    assert(!Q2_VK_PresentationAdapterProviderReady(adapter));
    q2_vk_presentation_provider_capabilities_t capabilities =
        Q2_VK_PresentationAdapterProviderCapabilities(adapter);
    assert(capabilities.prerequisites_compiled ==
           Q2_VK_PresentationAdapterProviderBuildCompiled());
    assert(!capabilities.runtime_ready);
    assert(capabilities.lifecycle_capable);
    assert(capabilities.provider_swapchain_owned);
    assert(capabilities.queue_topology ==
           Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_SEPARATE_PRESENT_QUEUE);
    assert(capabilities.diagnostic[0] != '\0');
    assert(Q2_VK_PresentationAdapterProviderBuildPlatform()[0] != '\0');
    assert(Q2_VK_PresentationAdapterProviderBuildReason()[0] != '\0');
    assert(Q2_VK_PresentationAdapterProviderStatus(adapter) ==
           (Q2_VK_PresentationAdapterProviderBuildCompiled() ?
            Q2_VK_PRESENTATION_PROVIDER_BUILT :
            Q2_VK_PRESENTATION_PROVIDER_UNAVAILABLE));
    Q2_VK_PresentationAdapterShutdown(
        adapter, Q2_VK_PRESENTATION_SHUTDOWN_DEVICE_LOST);
    assert(state.shutdown_reason == Q2_VK_PRESENTATION_SHUTDOWN_DEVICE_LOST);
    assert(state.event_count == 1 && state.events[0] == MOCK_SHUTDOWN);
    Q2_VK_PresentationAdapterDestroy(adapter,
                                     Q2_VK_PRESENTATION_SHUTDOWN_DEVICE_LOST);
}

static void check_provider_status_is_not_implied_by_native_adapter(void)
{
    mock_state_t state = {.acquire_result = VK_SUCCESS,
                          .present_result = VK_SUCCESS,
                          .wait_result = VK_SUCCESS};
    const q2_vk_presentation_ops_t ops = {
        .userdata = &state,
        .frame_generation_ready = true,
        .provider_swapchain_owned = false,
        .topology = {
            .queue_family_facts_known = true,
            .graphics_queue_family = 0,
            .present_queue_family = 0,
            .sync_contract = {
                .acquire_signal = Q2_VK_PRESENTATION_SYNC_BINARY_SEMAPHORE,
                .render_finished_signal =
                    Q2_VK_PRESENTATION_SYNC_BINARY_SEMAPHORE,
                .frame_completion = Q2_VK_PRESENTATION_SYNC_FENCE,
                .image_reuse = Q2_VK_PRESENTATION_SYNC_FENCE,
            },
            .native_sync_facts_known = true,
            .native_sync_facts =
                Q2_VK_PRESENTATION_SYNC_ACQUIRE_BINARY |
                Q2_VK_PRESENTATION_SYNC_RENDER_FINISHED_BINARY |
                Q2_VK_PRESENTATION_SYNC_FRAME_FENCE |
                Q2_VK_PRESENTATION_SYNC_IMAGE_FENCE_ALIASES_FRAME,
            .provider_synchronization_ready = true,
        },
        .acquire = mock_acquire,
        .present = mock_present,
        .wait_idle = mock_wait,
        .recreate = mock_recreate,
        .shutdown = mock_shutdown,
    };
    q2_vk_presentation_adapter_t *adapter = Q2_VK_PresentationAdapterCreate(
        Q2_VK_PRESENTATION_NATIVE, &ops);
    assert(adapter);
    assert(!Q2_VK_PresentationAdapterFrameGenerationEnabled(adapter));
    assert(!Q2_VK_PresentationAdapterProviderReady(adapter));
    q2_vk_presentation_provider_capabilities_t capabilities =
        Q2_VK_PresentationAdapterProviderCapabilities(adapter);
    assert(capabilities.prerequisites_compiled ==
           Q2_VK_PresentationAdapterProviderBuildCompiled());
    assert(!capabilities.runtime_ready);
    assert(!capabilities.provider_swapchain_owned);
    assert(capabilities.lifecycle_capable);
    assert(capabilities.queue_topology ==
           Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_SINGLE_QUEUE);
    assert(!capabilities.provider_synchronization_ready);
    assert(capabilities.sync_contract.acquire_signal ==
           Q2_VK_PRESENTATION_SYNC_BINARY_SEMAPHORE);
    assert(capabilities.sync_contract.render_finished_signal ==
           Q2_VK_PRESENTATION_SYNC_BINARY_SEMAPHORE);
    assert(capabilities.sync_contract.frame_completion ==
           Q2_VK_PRESENTATION_SYNC_FENCE);
    assert(capabilities.sync_contract.image_reuse ==
           Q2_VK_PRESENTATION_SYNC_FENCE);
    assert(capabilities.native_sync_facts_known);
    assert(capabilities.native_sync_facts &
           Q2_VK_PRESENTATION_SYNC_IMAGE_FENCE_ALIASES_FRAME);
    assert(capabilities.diagnostic[0] != '\0');
    assert(Q2_VK_PresentationAdapterProviderStatus(adapter) ==
           (Q2_VK_PresentationAdapterProviderBuildCompiled() ?
            Q2_VK_PRESENTATION_PROVIDER_BUILT :
            Q2_VK_PRESENTATION_PROVIDER_UNAVAILABLE));
    Q2_VK_PresentationAdapterDestroy(adapter,
                                     Q2_VK_PRESENTATION_SHUTDOWN_NORMAL);
}

static void check_topology_readiness_diagnostics(void)
{
    if (!Q2_VK_PresentationAdapterProviderBuildCompiled())
        return;

    mock_state_t state = {.acquire_result = VK_SUCCESS,
                          .present_result = VK_SUCCESS,
                          .wait_result = VK_SUCCESS};
    q2_vk_presentation_adapter_t *adapter = make_capability_adapter(
        &state, false, true, 0, 0, 0, 0);
    assert(adapter);
    q2_vk_presentation_provider_capabilities_t capabilities =
        Q2_VK_PresentationAdapterProviderCapabilities(adapter);
    assert(capabilities.runtime_ready && capabilities.lifecycle_capable);
    assert(capabilities.queue_topology ==
           Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_UNKNOWN);
    assert(!capabilities.provider_synchronization_ready);
    assert(strcmp(capabilities.diagnostic,
                  "provider queue-family topology is unknown") == 0);
    assert(!Q2_VK_PresentationAdapterProviderReady(adapter));
    Q2_VK_PresentationAdapterDestroy(adapter,
                                     Q2_VK_PRESENTATION_SHUTDOWN_NORMAL);

    adapter = make_capability_adapter(&state, true, false, 0, 0, 0, 0);
    assert(adapter);
    capabilities = Q2_VK_PresentationAdapterProviderCapabilities(adapter);
    assert(capabilities.runtime_ready && capabilities.lifecycle_capable);
    assert(capabilities.queue_topology ==
           Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_SINGLE_QUEUE);
    assert(!capabilities.provider_synchronization_ready);
    assert(strcmp(capabilities.diagnostic,
                  "native synchronization facts are unknown") == 0);
    assert(!Q2_VK_PresentationAdapterProviderReady(adapter));
    Q2_VK_PresentationAdapterDestroy(adapter,
                                     Q2_VK_PRESENTATION_SHUTDOWN_NORMAL);

    adapter = make_capability_adapter(&state, true, true, 0, 0, 0, 1);
    assert(adapter);
    capabilities = Q2_VK_PresentationAdapterProviderCapabilities(adapter);
    assert(capabilities.queue_topology ==
           Q2_VK_PRESENTATION_QUEUE_TOPOLOGY_SHARED_FAMILY);
    assert(Q2_VK_PresentationAdapterProviderReady(adapter));
    Q2_VK_PresentationAdapterDestroy(adapter,
                                     Q2_VK_PRESENTATION_SHUTDOWN_NORMAL);
}

static void check_lifecycle_capability_diagnostic(void)
{
    mock_state_t state = {.acquire_result = VK_SUCCESS,
                          .present_result = VK_SUCCESS,
                          .wait_result = VK_SUCCESS};
    const q2_vk_presentation_ops_t ops = {
        .userdata = &state,
        .frame_generation_ready = true,
        .provider_swapchain_owned = true,
        .topology = {
            .queue_family_facts_known = true,
            .graphics_queue_family = 0,
            .present_queue_family = 1,
            .provider_synchronization_ready = false,
        },
        .acquire = mock_acquire,
        .present = mock_present,
        .shutdown = mock_shutdown,
    };
    q2_vk_presentation_adapter_t *adapter = Q2_VK_PresentationAdapterCreate(
        Q2_VK_PRESENTATION_FRAME_INTERPOLATION, &ops);
    assert(adapter);
    q2_vk_presentation_provider_capabilities_t capabilities =
        Q2_VK_PresentationAdapterProviderCapabilities(adapter);
    assert(capabilities.prerequisites_compiled ==
           Q2_VK_PresentationAdapterProviderBuildCompiled());
    assert(capabilities.runtime_ready);
    assert(!capabilities.lifecycle_capable);
    assert(strcmp(capabilities.diagnostic,
                  Q2_VK_PresentationAdapterProviderBuildCompiled() ?
                  "presentation lifecycle callbacks are incomplete" :
                  Q2_VK_PresentationAdapterProviderBuildReason()) == 0);
    assert(!Q2_VK_PresentationAdapterProviderReady(adapter));
    Q2_VK_PresentationAdapterDestroy(adapter,
                                     Q2_VK_PRESENTATION_SHUTDOWN_NORMAL);
}

int main(void)
{
    check_ordering_and_fallback();
    check_frame_generation_gate_and_device_loss();
    check_provider_status_is_not_implied_by_native_adapter();
    check_lifecycle_capability_diagnostic();
    check_topology_readiness_diagnostics();
    puts("Vulkan presentation adapter contracts: passed");
    return 0;
}
