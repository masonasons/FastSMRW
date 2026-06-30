#pragma once

#include <optional>
#include <string>

#include <windows.h>

namespace fastsmui {

enum class UserProfileAction { ViewPosts, OpenBrowser };

// Modal Open User Profile dialog (Mac parity): a read-only review of the user's
// profile (name, handle, bio, counts) plus navigation buttons. Returns the
// chosen action, or nullopt if dismissed. The caller performs the action.
std::optional<UserProfileAction> show_user_profile_dialog(HWND parent, HINSTANCE inst,
                                                          const std::wstring& text);

} // namespace fastsmui
