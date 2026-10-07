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

// The player's effects, as the core reported them (the media_effects event). The
// dialog only renders this and sends changes back; it knows nothing about the engine.
struct MediaEffectChoices {
    struct Effect {
        std::string key;
        std::string name;
        bool enabled = false;
    };
    struct Param {
        std::string key;
        std::string name;
        std::string unit;
        std::string effect;  // the effect it belongs to; empty = always applies
        std::string display; // the value as the core wrote it ("+3.0 dB", "Cathedral")
        float min_value = 0.0f;
        float max_value = 1.0f;
        float step = 0.01f;
        float value = 0.0f;
        std::vector<std::string> choices; // named values; empty = a plain number
    };
    bool available = false;
    std::vector<Effect> effects;
    std::vector<Param> params;
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
                     std::function<void(const std::string& cmd, const std::string& path)> media_command = {},
                     const MediaEffectChoices& effects = {},
                     std::function<void(const std::string& effect, bool on)> set_effect = {},
                     std::function<void(const std::string& key, float value)> set_param = {});

} // namespace fastsmui
