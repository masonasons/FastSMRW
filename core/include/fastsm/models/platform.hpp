#pragma once

#include <string_view>

namespace fastsm {

// Which social network a model/account belongs to.
enum class Platform {
    Mastodon,
    Bluesky,
};

inline const char* platform_tag(Platform p) {
    switch (p) {
    case Platform::Mastodon:
        return "mastodon";
    case Platform::Bluesky:
        return "bluesky";
    }
    return "unknown";
}

inline Platform platform_from_tag(std::string_view s) {
    return s == "bluesky" ? Platform::Bluesky : Platform::Mastodon;
}

// Post visibility. Mastodon-only; Bluesky posts are always public.
enum class Visibility {
    Public,
    Unlisted,
    Private,
    Direct,
};

inline const char* visibility_tag(Visibility v) {
    switch (v) {
    case Visibility::Public:
        return "public";
    case Visibility::Unlisted:
        return "unlisted";
    case Visibility::Private:
        return "private";
    case Visibility::Direct:
        return "direct";
    }
    return "public";
}

inline Visibility visibility_from_tag(std::string_view s) {
    if (s == "unlisted")
        return Visibility::Unlisted;
    if (s == "private")
        return Visibility::Private;
    if (s == "direct")
        return Visibility::Direct;
    return Visibility::Public;
}

} // namespace fastsm
