#pragma once

#include <optional>
#include <string>

#include <windows.h>

namespace fastsmui {

struct AddAccountData {
    int platform = 0; // 0 = Mastodon, 1 = Bluesky
    std::string service;
    std::string handle;
    std::string app_password;
};

// Modal dialog collecting account details. Returns the inputs if OK'd; the
// actual sign-in (which may open a browser) is started by the caller.
std::optional<AddAccountData> show_add_account_dialog(HWND parent, HINSTANCE inst);

} // namespace fastsmui
