#pragma once

#include <cstdint>
#include <string>

#include "fastsm/models/platform.hpp"

namespace fastsm {

// A platform-agnostic profile.
struct User {
    std::string id;           // platform-native id (Mastodon id / Bluesky did)
    std::string acct;         // "user@instance" (Mastodon) or handle (Bluesky)
    std::string username;     // local part
    std::string display_name; // may be empty
    std::string note;         // bio (may contain HTML on Mastodon)
    std::string avatar_url;
    std::string header_url;
    std::string url;
    int followers_count = 0;
    int following_count = 0;
    int statuses_count = 0;
    std::int64_t created_at = 0; // unix seconds; 0 = unknown
    bool bot = false;
    bool locked = false;
    Platform platform = Platform::Mastodon;

    // displayName, falling back to acct when empty.
    const std::string& best_name() const {
        return display_name.empty() ? acct : display_name;
    }
};

} // namespace fastsm
