#pragma once

#include <string>

#include "fastsm/models/status.hpp"
#include "fastsm/models/user.hpp"

namespace fastsm::present {

// Ordered, de-duplicated "@a @b " mention prefix for replying to `status` as
// `me` (author + everyone they mentioned, minus yourself). "" if no one to
// mention. Mastodon convention; Bluesky uses structural replies.
std::string mention_prefix(const Status& status, const User& me);

} // namespace fastsm::present
