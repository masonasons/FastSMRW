#pragma once

#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "fastsm/store/app_settings.hpp"

namespace fastsmui {

// Shows the tabbed Settings dialog (a Windows property sheet). Returns the
// edited settings if the user clicked OK, else nullopt.
std::optional<fastsm::store::AppSettings>
show_settings_dialog(HWND parent, HINSTANCE inst, const fastsm::store::AppSettings& current,
                     const std::vector<std::string>& soundpacks);

} // namespace fastsmui
