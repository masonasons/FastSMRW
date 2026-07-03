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

// Map a notification (app.bsky.notification.listNotifications). The `reason`
// becomes the Notification kind (like->Favourite, repost->Reblog, follow->Follow,
// mention/reply/quote->Mention). For mention/reply/quote the notification's own
// record is the incoming post, so a light Status is attached.
Notification map_notification(const nlohmann::json& j);

} // namespace fastsm::bluesky
