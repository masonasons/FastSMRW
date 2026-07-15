#pragma once

#include <optional>
#include <string>

#include <windows.h>

// User Analysis picker (Me menu). A small modal dialog listing the available
// analyses of your follow relationships; picking one and choosing Run returns its
// category id, which the main window sends as an "analyze_users" command. The core
// then fetches your full followers/following lists and spawns a user timeline of
// the result (or announces an error if the lists can't be fully loaded).

namespace fastsmui {

// Runs the picker modally. Returns the chosen analysis category id
// ("not_following_back" | "no_followback" | "mutuals"), or nullopt if cancelled.
std::optional<std::string> show_user_analysis_dialog(HWND parent, HINSTANCE inst);

} // namespace fastsmui
