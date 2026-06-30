#pragma once

#include <nlohmann/json.hpp>

#include "fastsm/models/models.hpp"

// Mastodon REST DTO -> platform-agnostic models. Pure functions (no I/O), so
// they're unit-testable against captured sample payloads.
namespace fastsm::mastodon {

User map_user(const nlohmann::json& j);
Status map_status(const nlohmann::json& j);
Notification map_notification(const nlohmann::json& j);

} // namespace fastsm::mastodon
