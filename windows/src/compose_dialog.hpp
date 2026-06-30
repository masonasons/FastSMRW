#pragma once

#include <optional>
#include <string>

#include <windows.h>

namespace fastsmui {

// Modal compose dialog. Returns the entered UTF-8 text if the user posts.
std::optional<std::string> show_compose_dialog(HWND parent, HINSTANCE inst,
                                               const std::wstring& title,
                                               const std::wstring& context);

} // namespace fastsmui
