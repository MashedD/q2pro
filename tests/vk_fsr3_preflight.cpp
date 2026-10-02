/* GPU-free tests for the FSR3 Vulkan preflight ABI. */

#include "../src/refresh/vk_fsr3.h"

#include <cassert>
#include <cstdint>
#include <cstring>

static const char *missing_instance_function;
static const char *missing_device_function;
static bool use_core_buffer_requirements_name;

static VKAPI_ATTR void VKAPI_CALL fake_function(void)
{
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fake_instance_proc(
    VkInstance, const char *name)
{
    if (missing_instance_function &&
        std::strcmp(name, missing_instance_function) == 0)
        return nullptr;
    return reinterpret_cast<PFN_vkVoidFunction>(fake_function);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fake_device_proc(
    VkDevice, const char *name)
{
    if (missing_device_function &&
        std::strcmp(name, missing_device_function) == 0)
        return nullptr;
    if (std::strcmp(name, "vkGetBufferMemoryRequirements2KHR") == 0 &&
        use_core_buffer_requirements_name)
        return nullptr;
    return reinterpret_cast<PFN_vkVoidFunction>(fake_function);
}

static q2_fsr3_preflight_result_t run_preflight(
    uint32_t render_width = 640, uint32_t render_height = 360,
    uint32_t display_width = 1280, uint32_t display_height = 720,
    const q2_fsr3_capabilities_t *capabilities = nullptr)
{
    VkPhysicalDevice physical_device = {};
    VkDevice device = {};
    VkInstance instance = {};
    std::memset(&physical_device, 0xff, sizeof(physical_device));
    std::memset(&device, 0xff, sizeof(device));
    std::memset(&instance, 0xff, sizeof(instance));
    return Q2_FSR3_Preflight(
        physical_device, device, instance,
        fake_instance_proc, fake_device_proc,
        render_width, render_height, display_width, display_height,
        VK_FORMAT_R8G8B8A8_UNORM, false, capabilities);
}

int main()
{
    q2_fsr3_preflight_result_t result = run_preflight(0);
    assert(!result.supported);
    assert(!std::strcmp(result.reason, "invalid dimensions"));

    missing_instance_function = "vkGetPhysicalDeviceFeatures2";
    result = run_preflight();
    assert(!result.supported);
    assert(!std::strcmp(result.reason, "missing Vulkan instance function"));
    assert(!std::strcmp(result.missing_function, missing_instance_function));
    missing_instance_function = nullptr;

    missing_device_function = "vkCmdDispatch";
    result = run_preflight();
    assert(!result.supported);
    assert(!std::strcmp(result.reason, "missing Vulkan device function"));
    assert(!std::strcmp(result.missing_function, missing_device_function));
    missing_device_function = nullptr;

    use_core_buffer_requirements_name = true;
    result = run_preflight();
    assert(result.supported);
    assert(result.missing_function == nullptr);
    use_core_buffer_requirements_name = false;

    const q2_fsr3_capabilities_t capabilities = { true, 32 };
    result = run_preflight(640, 360, 1280, 720, &capabilities);
    assert(result.supported);
    assert(result.capabilities.fp16);
    assert(result.capabilities.subgroup_size == 32);

    return 0;
}
