#pragma once

// Internal: AppSettings <-> JSON, shared by SettingsStore (legacy settings.json)
// and AppConfigStore (the unified config.json). Not part of the public API.

#include <nlohmann/json.hpp>

#include "fastsm/store/app_settings.hpp"

namespace fastsm::store {

nlohmann::json settings_to_json(const AppSettings& settings);
AppSettings settings_from_json(const nlohmann::json& root);

} // namespace fastsm::store
