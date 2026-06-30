#pragma once

#include <string>

#include "fastsm/models/platform.hpp"

// SocialAccount is the central abstraction that unifies Mastodon and Bluesky.
// In M1 this gains the full surface (timeline paging, posting, boost/favorite,
// user resolution, etc.); for now it anchors the platform layer so the rest of
// the structure can compile and link.

namespace fastsm {

class SocialAccount {
public:
    virtual ~SocialAccount() = default;

    virtual Platform platform() const = 0;

    // Maximum characters allowed in a post on this platform/instance.
    virtual int max_chars() const = 0;

    // Persistence key, e.g. "mastodon:<id>" / "bluesky:<did>".
    virtual std::string account_key() const = 0;
};

} // namespace fastsm
