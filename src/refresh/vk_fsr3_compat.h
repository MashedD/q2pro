#pragma once

#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <wchar.h>

#define FFX_GCC 1
#include <FidelityFX/host/ffx_types.h>
#undef FFX_SDK_DEFAULT_CONTEXT_SIZE
#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 256)
#ifndef _countof
#define _countof(value) (sizeof(value) / sizeof((value)[0]))
#endif

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
