#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fastsm/models/card.hpp"
#include "fastsm/models/media.hpp"
#include "fastsm/models/platform.hpp"
#include "fastsm/models/user.hpp"

namespace fastsm {

// A post/status. Boosts and quotes reference another Status via shared_ptr to
// break the recursion (the Swift core uses a boxed indirect enum).
struct Status {
    std::string id;
    User account; // author of THIS status (the booster, when is_boost())
    std::string content;         // raw HTML (Mastodon); empty on Bluesky
    std::string text;            // display-ready plain text (HTML-stripped)
    std::int64_t created_at = 0; // unix seconds
    int favourites_count = 0;
    int boosts_count = 0;
    int replies_count = 0;
    std::optional<std::string> in_reply_to_id;
    std::optional<std::string> in_reply_to_account_id;
    std::shared_ptr<Status> reblog; // the boosted status, when this is a boost
    std::shared_ptr<Status> quote;  // the quoted status
    std::vector<MediaAttachment> media_attachments;
    std::vector<Mention> mentions;
    std::optional<Visibility> visibility; // Mastodon only
    std::optional<std::string> spoiler_text;
    std::optional<Card> card;
    std::optional<Poll> poll;
    bool pinned = false;
    bool favourited = false;
    bool boosted = false;
    std::optional<std::string> application_name; // posting client ("via ...")
    std::optional<std::string> instance_url;     // remote instance, if fetched abroad
    Platform platform = Platform::Mastodon;

    bool is_boost() const { return reblog != nullptr; }
    bool has_content_warning() const { return spoiler_text && !spoiler_text->empty(); }

    // The status to actually display: the boosted one if this is a boost.
    const Status& display_status() const { return reblog ? *reblog : *this; }
    Status& display_status() { return reblog ? *reblog : *this; }
};

} // namespace fastsm
