#pragma once

#include <nlohmann/json.hpp>

#include "fastsm/models/models.hpp"

// Bluesky (AT Protocol) DTO -> models. Pure functions, unit-testable.
namespace fastsm::bluesky {

// Map an author profile (app.bsky.actor.defs#profileView*).
User map_author(const nlohmann::json& j);

// Map a single post view (app.bsky.feed.defs#postView).
Status map_post(const nlohmann::json& post);

// Map a feed item (app.bsky.feed.defs#feedViewPost), honoring a repost "reason"
// by wrapping the post in a boost.
Status map_feed_item(const nlohmann::json& item);

} // namespace fastsm::bluesky
