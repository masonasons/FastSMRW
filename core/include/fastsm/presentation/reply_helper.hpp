#pragma once

#include <string>
#include <vector>

#include "fastsm/models/status.hpp"
#include "fastsm/models/user.hpp"

namespace fastsm::present {

// One person to (optionally) mention on a reply: their handle plus a display
// label for the compose dialog's recipient checklist.
struct ReplyParticipant {
    std::string acct;         // handle without the leading '@'
    std::string display_name; // label for the checklist (falls back to the handle)
    bool checked = true;       // pre-checked in the dialog (the booster is offered unchecked)
};

// Ordered, de-duplicated reply participants for replying to `status` as `me`
// (author + everyone they mentioned, minus yourself). When `booster` is given
// (the account that boosted the post being replied to), they're appended
// unchecked so you can opt into mentioning them but don't by default.
std::vector<ReplyParticipant> reply_participants(const Status& status, const User& me,
                                                 const User* booster = nullptr);

// Ordered, de-duplicated "@a @b " mention prefix for replying to `status` as
// `me` (author + everyone they mentioned, minus yourself). "" if no one to
// mention. Mastodon convention; Bluesky uses structural replies.
std::string mention_prefix(const Status& status, const User& me);

// "@a @b " prefix from an explicit set of handles (e.g. the ones the user left
// checked in the compose dialog). Empty handles are skipped; "" if none.
std::string mention_prefix(const std::vector<std::string>& accts);

} // namespace fastsm::present
