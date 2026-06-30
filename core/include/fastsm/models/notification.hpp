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
    User account; // the user who performed the action
    std::int64_t created_at = 0;
    std::shared_ptr<fastsm::Status> status; // referenced post, may be null
    Platform platform = Platform::Mastodon;
};

} // namespace fastsm
