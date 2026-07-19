#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "fastsm/models/platform.hpp"
#include "fastsm/models/status.hpp"
#include "fastsm/models/user.hpp"

namespace fastsm {

// A notification (follow, favourite, boost, mention, ...).
struct Notification {
    enum class Kind {
        Follow,
        FollowRequest,
        Favourite,
        Reblog,
        Mention,
        Poll,
        Status,
        Update,
        Unknown,
    };

    std::string id;
    Kind type = Kind::Unknown;
    User account; // the user who performed the action (the most recent, for a group)
    std::int64_t created_at = 0;
    std::shared_ptr<fastsm::Status> status; // referenced post, may be null
    Platform platform = Platform::Mastodon;

    // Grouping (Mastodon 4.3+ grouped notifications). `group_key` identifies the
    // server-side group a streamed notification belongs to (empty = ungrouped);
    // `notifications_count` is how many actors it represents (1 = a lone
    // notification). The UI composes "A and N others …" from account + this count.
    std::string group_key;
    int notifications_count = 1;
};

} // namespace fastsm
