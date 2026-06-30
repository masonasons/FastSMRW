#pragma once

#include <filesystem>

namespace fastsm::store {

// %APPDATA%\FastSMRW (created if missing) — holds config.json.
std::filesystem::path config_dir();

// %LOCALAPPDATA%\FastSMRW\timelines (created if missing) — the timeline cache.
std::filesystem::path cache_dir();

} // namespace fastsm::store
