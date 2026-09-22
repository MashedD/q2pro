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
    assert(Q2_VK_PresentationAdapterProviderStatus(adapter) ==
           (Q2_VK_PresentationAdapterProviderBuildCompiled() ?
            Q2_VK_PRESENTATION_PROVIDER_BUILT :
            Q2_VK_PRESENTATION_PROVIDER_UNAVAILABLE));
    Q2_VK_PresentationAdapterDestroy(adapter,
                                     Q2_VK_PRESENTATION_SHUTDOWN_NORMAL);
}

int main(void)
{
    check_ordering_and_fallback();
    check_frame_generation_gate_and_device_loss();
    check_provider_status_is_not_implied_by_native_adapter();
    puts("Vulkan presentation adapter contracts: passed");
    return 0;
}
