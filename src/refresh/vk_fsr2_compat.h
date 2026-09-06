#pragma once

#include <stddef.h>
#include <string>
#include <wchar.h>

#define FFX_GCC 1
#ifndef _countof
#define _countof(value) (sizeof(value) / sizeof((value)[0]))
#endif
#define wcscpy_s(destination, source) wcscpy((destination), (source))

#if defined(__GNUC__) && __GNUC__ >= 16
namespace std {
template <typename Codecvt>
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
};
}
#endif
