#pragma once

// AppSettings <-> JSON. Used by the config store, the C ABI session, and front
// ends that translate the "settings" event / "update_settings" command to and
// from the AppSettings struct.

#include <nlohmann/json.hpp>

#include "fastsm/store/app_settings.hpp"

namespace fastsm::store {

nlohmann::json settings_to_json(const AppSettings& settings);
AppSettings settings_from_json(const nlohmann::json& root);

} // namespace fastsm::store
