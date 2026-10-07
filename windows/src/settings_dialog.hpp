#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "fastsm/store/app_settings.hpp"

namespace fastsmui {

// The lists the Audio page offers, gathered by the caller: soundpacks and the
// sound-effect devices come from the core, the media devices from DirectShow.
// An empty device list just leaves that picker at "System default".
struct AudioChoices {
    std::vector<std::string> soundpacks;
    std::vector<std::string> sound_devices;
    std::vector<std::string> media_devices;
};

// Shows the tabbed Settings dialog (a Windows property sheet). Returns the
// edited settings if the user clicked OK, else nullopt. `open_manager`, if set,
// is invoked (with the settings dialog as parent) when the user clicks the
// Keyboard Manager button on the Invisible interface tab.
//
// `media_command`, if set, sends one of the FastPlay page's commands to the core at
// once (media_settings_import / _export with a path, media_settings_reset).
std::optional<fastsm::store::AppSettings>
show_settings_dialog(HWND parent, HINSTANCE inst, const fastsm::store::AppSettings& current,
                     const AudioChoices& audio, std::function<void(HWND)> open_manager = {},
                     std::function<void(const std::string& cmd, const std::string& path)> media_command = {});

} // namespace fastsmui
