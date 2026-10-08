#pragma once

#include <string>
#include <vector>

#include "fastsm/models/timeline_item.hpp"
#include "fastsm/models/user.hpp"

namespace fastsm {

// Everyone this account has seen go past, newest first.
//
// Ported from FastSM's UserCache, and it exists for one reason: @-mention
// autocomplete has to answer instantly and offer the people you actually talk to.
// Asking the server is both slower and worse -- its account search indexes who it
// knows about, not who is in front of you, so the person whose post you are replying
// to may not come back at all.
//
// In memory only, discarded on exit, and capped: the point is recency, not history.
// Lives on the account, because handles and ids mean nothing across servers.
class UserCache {
public:
    // Matches FastSM's cap. Big enough to cover a session's worth of timelines,
    // small enough that a prefix scan over it stays trivial.
    static constexpr size_t kMaxUsers = 500;

    // Add or refresh one person, moving them to the front. Ignores a user with no
    // id, which is what a half-filled row yields.
    void add(const User& user);
    // Everyone a row refers to: a post's author, the booster of a boost, the author
    // of a quoted post, and a notification's actor.
    void add_from(const TimelineItem& item);

    // Newest first, so a prefix match naturally prefers whoever you saw last.
    const std::vector<User>& all() const { return users_; }
    size_t size() const { return users_.size(); }

    // The people whose handle, handle's local part, or display name begins with
    // `prefix` (case-insensitively), newest first, up to `limit`. An empty prefix
    // matches nobody: autocompleting on nothing would just list the cache.
    std::vector<User> starting_with(const std::string& prefix, size_t limit) const;

private:
    std::vector<User> users_;
};

} // namespace fastsm
