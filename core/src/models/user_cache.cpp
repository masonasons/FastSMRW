#include "fastsm/models/user_cache.hpp"

#include <algorithm>
#include <cctype>

namespace fastsm {
namespace {

std::string lowered(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// "alice@example.social" -> "alice". A local Mastodon handle has no domain, so this
// is the whole thing; a remote one is matched on both forms, because people type the
// name they know and not always the domain with it.
std::string local_part(const std::string& acct) {
    const auto at = acct.find('@');
    return at == std::string::npos ? acct : acct.substr(0, at);
}

bool starts_with(const std::string& haystack, const std::string& needle) {
    return haystack.size() >= needle.size() &&
           haystack.compare(0, needle.size(), needle) == 0;
}

} // namespace

void UserCache::add(const User& user) {
    if (user.id.empty())
        return;
    // Replace any existing entry, then push to the front: seeing someone again makes
    // them a better suggestion, and refreshes a display name they have since changed.
    const auto same = std::find_if(users_.begin(), users_.end(),
                                   [&](const User& u) { return u.id == user.id; });
    if (same != users_.end())
        users_.erase(same);
    users_.insert(users_.begin(), user);
    if (users_.size() > kMaxUsers)
        users_.resize(kMaxUsers);
}

void UserCache::add_from(const TimelineItem& item) {
    if (const User* u = item.user()) {
        add(*u);
        return;
    }
    if (const Notification* n = item.notification()) {
        add(n->account); // whoever did the thing
        if (n->status)
            add(n->status->account);
        return;
    }
    if (const Status* s = item.status()) {
        add(s->account); // the booster, on a boost
        if (s->reblog)
            add(s->reblog->account); // the person actually being read
        if (s->quote)
            add(s->quote->account);
    }
}

std::vector<User> UserCache::starting_with(const std::string& prefix, size_t limit) const {
    std::vector<User> out;
    if (prefix.empty() || limit == 0)
        return out;
    const std::string want = lowered(prefix);
    for (const User& u : users_) {
        const std::string acct = lowered(u.acct.empty() ? u.username : u.acct);
        // Display name as well as handle, because that is often all you remember --
        // and it is what FastSM matches on too.
        if (starts_with(acct, want) || starts_with(local_part(acct), want) ||
            starts_with(lowered(u.display_name), want)) {
            out.push_back(u);
            if (out.size() >= limit)
                break;
        }
    }
    return out;
}

} // namespace fastsm
