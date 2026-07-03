#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include <nlohmann/json.hpp>

// Hashtag UI (Mastodon):
//  - show_follow_hashtag_dialog: a small prompt with an editable combo pre-filled
//    with the hashtags in the focused post (blank if none). Returns the entered
//    tag name (no '#'), or nullopt if cancelled / left blank.
//  - FollowedHashtagsDialog: the Followed Hashtags manager (Application menu).
//    Modal but async — it sends commands through `dispatch`, and the main window
//    forwards `followed_hashtags` events into on_followed() while the modal loop
//    pumps (same pattern as the Lists manager).

namespace fastsmui {

std::optional<std::string> show_follow_hashtag_dialog(HWND parent, HINSTANCE inst,
                                                      const std::vector<std::wstring>& prefill);

class FollowedHashtagsDialog {
public:
    FollowedHashtagsDialog(HINSTANCE inst, std::function<void(const nlohmann::json&)> dispatch);

    // Run modally. `initial` is the followed_hashtags event that triggered opening.
    void run(HWND parent, const nlohmann::json& initial);
    // Fed by the main window when a `followed_hashtags` event arrives (same thread).
    void on_followed(const nlohmann::json& e);

private:
    static INT_PTR CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR handle(HWND dlg, UINT msg, WPARAM wp, LPARAM lp);
    void refresh_list();
    void update_enabled();
    int selected_index() const;
    void do_open();
    void do_unfollow();

    HINSTANCE inst_;
    std::function<void(const nlohmann::json&)> dispatch_;
    HWND dlg_ = nullptr;
    nlohmann::json tags_ = nlohmann::json::array(); // [{name,url,following}]
};

} // namespace fastsmui
