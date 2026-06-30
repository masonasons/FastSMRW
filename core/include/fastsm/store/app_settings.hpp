#pragma once

#include <filesystem>
#include <string>

#include "fastsm/presentation/speech_settings.hpp"

namespace fastsm::store {

// User preferences. Grows over time (Mac has many more); for now the pieces M1
// needs plus the configurable speech field order/visibility.
struct AppSettings {
    bool sounds_enabled = true;
    bool enter_to_send = false; // Return sends (else Ctrl+Return sends)
    std::string soundpack = "Default";
    present::SpeechSettings speech = present::SpeechSettings::defaults();
};

// Reads/writes settings.json (plain JSON; no secrets here).
class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path path);

    AppSettings load() const; // speech is normalized so old files keep working
    bool save(const AppSettings& settings) const;

    static SettingsStore default_store(); // config_dir()/settings.json

private:
    std::filesystem::path path_;
};

} // namespace fastsm::store
