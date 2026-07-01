#pragma once

#include <filesystem>

namespace fastsm::store {

// The data folder (created if missing) — holds config.json. Normally
// %APPDATA%\FastSMRW, but a "userdata" folder next to the executable overrides
// it (portable mode), in which case that folder is used directly.
std::filesystem::path config_dir();

// <data folder>\cache (created if missing) — the timeline cache, kept in the
// same FastSM data folder as config.json.
std::filesystem::path cache_dir();

} // namespace fastsm::store
