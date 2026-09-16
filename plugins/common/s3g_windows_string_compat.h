#pragma once

#if !defined(_WIN32) || !defined(_MSC_VER)
#error "This adapter is only for the native Windows compiler."
#endif
#include <cstddef>
#include <cstring>

// MSVC names for the POSIX case-insensitive comparisons used by CLAP
// parameter text parsers. Keep the existing call sites and non-Windows CRT.
inline int strcasecmp(const char* left, const char* right)
{
    return _stricmp(left, right);
}
inline int strncasecmp(const char* left, const char* right, std::size_t count)
{
    return _strnicmp(left, right, count);
}
