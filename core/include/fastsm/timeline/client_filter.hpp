#pragma once

#include <string>

#include "fastsm/models/timeline_item.hpp"

namespace fastsm {

// A per-timeline, client-side display filter (ported from FastSM for Windows).
// It only decides what the UI shows; the timeline still fetches and caches every
// item (see TimelineController's raw vs. visible split). Each flag INCLUDES that
// category when true; a false flag hides matching posts. `text` is a
// case-insensitive substring matched against the post text, display name and
// handle. When every flag is true and `text` is empty the filter is inactive.
struct ClientFilter {
    bool original = true;      // original posts (not replies, not boosts)
    bool replies = true;       // replies to other people
    bool replies_to_me = true; // replies to the current user
    bool threads = true;       // self-replies (thread continuations)
    bool boosts = true;        // boosts / reposts
    bool quotes = true;        // quote posts
    bool media = true;         // posts with media attachments
    bool no_media = true;      // posts without media
    bool my_posts = true;      // posts by the current user
    bool my_replies = true;    // replies by the current user
    std::string text;          // substring match (post text + display name + handle)

    // Whether the filter would hide anything (else it can be dropped entirely).
    bool is_active() const {
        return !(original && replies && replies_to_me && threads && boosts && quotes && media &&
                 no_media && my_posts && my_replies && text.empty());
    }
};

// True if `item` should be shown under `filter`. Non-status rows (users, bare
// notifications) always pass. `me_id` is the current account's user id (for the
// replies-to-me / your-posts checks); may be empty.
bool client_filter_should_show(const ClientFilter& filter, const TimelineItem& item,
                               const std::string& me_id);

} // namespace fastsm
