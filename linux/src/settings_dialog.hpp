#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "fastsm/store/app_settings.hpp"

namespace fastsmgtk {

// Modal Settings dialog — a GtkNotebook mirroring the Windows property sheet's
// pages (General, Timelines, Audio, Earcons, Speech, Advanced, Confirmations,
// Behavior, Invisible interface, Updates, FastPlay). Edits a copy of
// AppSettings; returns it if the user accepts (the caller serializes and sends
// update_settings), nullopt on cancel. The FastPlay page is the exception: its
// changes go to the core at once, as on Windows.
// The lists the Audio page offers, gathered by the caller: soundpacks and the
// sound-effect devices and the media devices (FastPlay's engine's) all come
// from the core.
// An empty device list just leaves that picker at "System default".
struct AudioChoices {
    std::vector<std::string> soundpacks;
    std::vector<std::string> sound_devices;
    std::vector<std::string> media_devices;
};

// The FastPlay page's effects and their adjustable values, from the core's
// media_effects event (the same shape as the Windows page's).
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

// What the FastPlay page sends the core, at once.
struct MediaCallbacks {
    // media_settings_import / _export with a path, or media_settings_reset.
    std::function<void(const std::string& cmd, const std::string& path)> command;
    std::function<void(const std::string& effect, bool on)> set_effect;
    std::function<void(const std::string& key, float value)> set_param;
};

std::optional<fastsm::store::AppSettings>
show_settings_dialog(GtkWindow* parent, fastsm::store::AppSettings settings,
                     const AudioChoices& audio, MediaEffectChoices effects = {},
                     MediaCallbacks media = {});

} // namespace fastsmgtk
