#pragma once

#include <optional>
#include <string>
#include <vector>

#include <windows.h>

namespace fastsmui {

// One profile metadata row (label + content).
struct ProfileEditField {
    std::string name;
    std::string value;
};

// The full editable profile the dialog shows and returns.
struct ProfileEdit {
    std::string display_name;
    std::string note;                    // bio (plain text; may contain newlines)
    std::vector<ProfileEditField> fields; // metadata rows the dialog collected
    bool locked = false;                 // require follow requests
    bool bot = false;                    // automated account
    bool discoverable = false;           // list in the profile directory
    bool sensitive = false;              // mark media sensitive by default
    std::string privacy = "public";      // default post visibility
    int max_fields = 4;                  // how many metadata rows to show
};

// Modal "Edit Profile" dialog, prefilled from `current` (its max_fields controls
// how many metadata rows are shown). Returns the edited profile, or nullopt if
// cancelled.
std::optional<ProfileEdit> show_edit_profile_dialog(HWND parent, HINSTANCE inst,
                                                    const ProfileEdit& current);

} // namespace fastsmui
