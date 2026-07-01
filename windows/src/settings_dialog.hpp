#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "fastsm/store/app_settings.hpp"

namespace fastsmui {

// Shows the tabbed Settings dialog (a Windows property sheet). Returns the
// edited settings if the user clicked OK, else nullopt. `open_manager`, if set,
// is invoked (with the settings dialog as parent) when the user clicks the
// Keyboard Manager button on the Invisible interface tab.
std::optional<fastsm::store::AppSettings>
show_settings_dialog(HWND parent, HINSTANCE inst, const fastsm::store::AppSettings& current,
                     const std::vector<std::string>& soundpacks,
                     std::function<void(HWND)> open_manager = {});

} // namespace fastsmui
