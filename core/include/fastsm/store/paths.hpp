#pragma once

#include <filesystem>

namespace fastsm::store {

// %APPDATA%\FastSMRW (created if missing) — holds config.json.
std::filesystem::path config_dir();

// %APPDATA%\FastSMRW\cache (created if missing) — the timeline cache, kept in
// the same FastSM data folder as config.json.
std::filesystem::path cache_dir();

} // namespace fastsm::store
