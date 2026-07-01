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

// A server-side filter (Mastodon /api/v2/filters) that matched this status, as
// reported by the server in the status's `filtered` array.
struct StatusFilterMatch {
    std::string title;  // the filter's human-readable title
    bool hide = false;  // filter_action: true = "hide", false = "warn"
};

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
    std::vector<StatusFilterMatch> filtered;     // server-side filters that matched (Mastodon)
    std::optional<std::string> instance_url;     // remote instance, if fetched abroad
    std::string url;                             // canonical web URL (open in browser)
    Platform platform = Platform::Mastodon;

    // Bluesky strong reference + viewer record URIs (empty on Mastodon).
    std::optional<std::string> cid;         // post cid (for like/repost subject)
    std::optional<std::string> like_uri;    // viewer's like record (to unlike)
    std::optional<std::string> repost_uri;  // viewer's repost record (to unrepost)

    bool is_boost() const { return reblog != nullptr; }
    bool has_content_warning() const { return spoiler_text && !spoiler_text->empty(); }

    // True if any server-side filter with action "hide" matched (the row should be
    // removed from the visible list). Considers the boosted post too.
    bool any_filter_hides() const {
        for (const auto& f : filtered)
            if (f.hide)
                return true;
        return reblog ? reblog->any_filter_hides() : false;
    }

    // The status to actually display: the boosted one if this is a boost.
    const Status& display_status() const { return reblog ? *reblog : *this; }
    Status& display_status() { return reblog ? *reblog : *this; }
};

} // namespace fastsm
