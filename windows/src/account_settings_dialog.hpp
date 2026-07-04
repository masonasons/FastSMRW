#pragma once

#include <optional>
#include <string>
#include <vector>

#include <windows.h>

// Per-account settings dialog. Currently a soundpack picker for the focused
// account (mirrors FastSM for Windows' Account Options -> General). Modal;
// returns the chosen soundpack, or nullopt if cancelled.

namespace fastsmui {

std::optional<std::string> show_account_settings_dialog(HWND parent, HINSTANCE inst,
                                                        const std::wstring& title,
                                                        const std::vector<std::string>& packs,
                                                        const std::string& current);

} // namespace fastsmui
