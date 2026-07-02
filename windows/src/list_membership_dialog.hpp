#pragma once

#include <optional>
#include <string>
#include <vector>

#include <windows.h>

namespace fastsmui {

// One of the account's lists plus whether the user is currently a member.
struct ListMembershipItem {
    std::string id;      // list id
    std::wstring title;  // list name
    bool member = false; // is the user in this list
};

// Modal checklist of the account's lists with the user's current membership
// checked. Returns the (possibly edited) items with updated `member` flags, or
// nullopt if cancelled. The caller diffs against the originals to apply changes.
std::optional<std::vector<ListMembershipItem>>
show_list_membership_dialog(HWND parent, HINSTANCE inst, const std::wstring& heading,
                            std::vector<ListMembershipItem> items);

} // namespace fastsmui
