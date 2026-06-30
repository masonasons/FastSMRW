#pragma once

#include <string>
#include <string_view>

#include <windows.h>

namespace fastsmui {

inline std::wstring to_wide(std::string_view s) {
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

inline std::string to_utf8(std::wstring_view s) {
    if (s.empty())
        return {};
    const int n =
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr,
                            nullptr);
    std::string o(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), o.data(), n, nullptr,
                        nullptr);
    return o;
}

} // namespace fastsmui
