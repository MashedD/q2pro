#pragma once

#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <wchar.h>

#define FFX_GCC 1
#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_types.h>
#ifdef interface
#undef interface
#endif
#undef FFX_SDK_DEFAULT_CONTEXT_SIZE
#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 256)
#ifndef _countof
#define _countof(value) (sizeof(value) / sizeof((value)[0]))
#endif

static inline VkImage q2_fsr3_vk_image_from_resource(const void *resource)
{
#if VK_USE_64_BIT_PTR_DEFINES
    return reinterpret_cast<VkImage>(const_cast<void *>(resource));
#else
    return static_cast<VkImage>(reinterpret_cast<uintptr_t>(resource));
#endif
}

static inline void *q2_fsr3_vk_handle_as_resource(const void *handle)
{
    return const_cast<void *>(handle);
}

static inline void *q2_fsr3_vk_handle_as_resource(uint64_t handle)
{
    return reinterpret_cast<void *>(static_cast<uintptr_t>(handle));
}

static inline FfxResource ffxGetResourceVK(
    VkImage image, FfxResourceDescription description,
    const wchar_t *name, FfxResourceStates state)
{
    return ffxGetResourceVK(q2_fsr3_vk_handle_as_resource(image),
                            description, name, state);
}

static inline const wchar_t *q2_fsr3_copy_source(const wchar_t *source)
{
    return source;
}

static inline const wchar_t *q2_fsr3_copy_source(
    size_t size, const wchar_t *source)
{
    (void)size;
    return source;
}

static inline const char *q2_fsr3_copy_source(const char *source)
{
    return source;
}

static inline const char *q2_fsr3_copy_source(
    size_t size, const char *source)
{
    (void)size;
    return source;
}

#define wcscpy_s(destination, ...) \
    wcscpy((destination), q2_fsr3_copy_source(__VA_ARGS__))
#define strcpy_s(destination, ...) \
    strcpy((destination), q2_fsr3_copy_source(__VA_ARGS__))
#define sprintf_s(destination, size, ...) \
    snprintf((destination), (size), __VA_ARGS__)

#if defined(__GNUC__) && __GNUC__ >= 16
namespace std {
template <typename Codecvt, typename Elem = wchar_t>
class wstring_convert {
public:
    std::wstring from_bytes(const char *value) const
    {
        std::wstring result;
        if (!value)
            return result;
        while (*value)
            result.push_back((unsigned char)*value++);
        return result;
    }

    std::string to_bytes(const std::wstring &value) const
    {
        std::string result;
        for (wchar_t character : value)
            result.push_back((char)character);
        return result;
    }
};
}
#endif
