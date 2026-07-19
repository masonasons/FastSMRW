#pragma once

#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "fastsm/models/models.hpp"

// Mastodon REST DTO -> platform-agnostic models. Pure functions (no I/O), so
// they're unit-testable against captured sample payloads.
namespace fastsm::mastodon {

User map_user(const nlohmann::json& j);
Status map_status(const nlohmann::json& j);
Notification map_notification(const nlohmann::json& j);
// Map one entry of /api/v2/notifications' `notification_groups`, resolving its
// sample account + status from that response's side-loaded `accounts`/`statuses`
// (indexed by id).
Notification map_notification_group(const nlohmann::json& group,
                                    const std::unordered_map<std::string, const nlohmann::json*>& accounts,
                                    const std::unordered_map<std::string, const nlohmann::json*>& statuses);
Poll map_poll(const nlohmann::json& j);

// Tag a status (and its boosted inner status) as fetched from a remote instance:
// set instance_url to `base` ("https://<domain>"), add the domain to bare handles,
// and synthesize a canonical URL if the server omitted one. Interactions on a
// tagged status resolve to a local copy first.
void mark_remote(Status& s, const std::string& base, const std::string& domain);

} // namespace fastsm::mastodon
