/*
 * utf8_util.hpp - Internal UTF-8 Conversion Utilities
 *
 * This file is for INTERNAL USE ONLY. Do not include in public headers.
 * Provides UTF-8 <-> platform-native string conversions.
 */

#ifndef INTERNAL_UTF8_UTIL_HPP
#define INTERNAL_UTF8_UTIL_HPP

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace internal {

#ifdef _WIN32

// Convert UTF-8 string to wide string (Windows)
inline std::wstring utf8_to_wide(const char* utf8) {
    if (!utf8 || !*utf8) return std::wstring();

    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return std::wstring();

    std::wstring wide(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &wide[0], len);
    return wide;
}

inline std::wstring utf8_to_wide(const std::string& utf8) {
    return utf8_to_wide(utf8.c_str());
}

// Convert wide string to UTF-8 (Windows)
inline std::string wide_to_utf8(const wchar_t* wide) {
    if (!wide || !*wide) return std::string();

    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string();

    std::string utf8(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &utf8[0], len, nullptr, nullptr);
    return utf8;
}

inline std::string wide_to_utf8(const std::wstring& wide) {
    return wide_to_utf8(wide.c_str());
}

// Convert UTF-8 to wide and copy to fixed buffer
inline void utf8_to_wide(const char* utf8, wchar_t* out, int out_size) {
    if (!utf8 || !out || out_size <= 0) return;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, out_size);
}

// Convert wide to UTF-8 and copy to fixed buffer
inline void wide_to_utf8(const wchar_t* wide, char* out, int out_size) {
    if (!wide || !out || out_size <= 0) return;
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, out_size, nullptr, nullptr);
}

#else

// On non-Windows platforms, UTF-8 is native
inline const char* utf8_to_native(const char* utf8) { return utf8; }
inline std::string native_to_utf8(const char* native) { return native ? native : ""; }

#endif

} // namespace internal

#endif // INTERNAL_UTF8_UTIL_HPP
