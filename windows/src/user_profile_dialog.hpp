#pragma once

#include <optional>
#include <string>

#include <windows.h>

namespace fastsmui {

enum class UserProfileAction {
    ViewPosts,
    Followers,
    Following,
    OpenBrowser,
    ToggleFollow,
    ToggleMute,
    ToggleBlock,
    ToggleBoosts,
    Lists
};

// The viewer's relationship to the user (for relationship-aware button labels).
struct UserProfileRelationship {
    bool known = false; // false = the relationship couldn't be loaded
    bool following = false;
    bool muting = false;
    bool blocking = false;
    bool requested = false;      // follow request pending
    bool showing_reblogs = true; // their boosts are shown
    bool can_hide_boosts = false; // platform supports hiding boosts
    bool can_use_lists = false;   // platform supports lists (Mastodon)
};

// Modal Open User Profile dialog (Mac parity): a read-only review of the user's
// profile plus navigation and follow/mute/block buttons. Returns the chosen
// action, or nullopt if dismissed. The caller performs the action.
std::optional<UserProfileAction> show_user_profile_dialog(HWND parent, HINSTANCE inst,
                                                          const std::wstring& text,
                                                          const UserProfileRelationship& rel);

} // namespace fastsmui
