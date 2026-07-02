#pragma once

#include <functional>

#include <windows.h>

#include <nlohmann/json.hpp>

// The Lists manager (Application menu, Mastodon): open, create, edit, and delete
// your lists. Like the Server Filters manager it is modal but the core is async —
// it sends commands through `dispatch` and the main window forwards the `lists`
// events into on_lists() while the modal message loop pumps.

namespace fastsmui {

class ListsManagerDialog {
public:
    ListsManagerDialog(HINSTANCE inst, std::function<void(const nlohmann::json&)> dispatch);

    // Run modally. `initial` is the lists event that triggered opening.
    void run(HWND parent, const nlohmann::json& initial);
    // Fed by the main window when a `lists` event arrives (same UI thread).
    void on_lists(const nlohmann::json& e);

private:
    static INT_PTR CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR handle(HWND dlg, UINT msg, WPARAM wp, LPARAM lp);
    void refresh_list();
    void update_enabled();
    int selected_index() const;
    void do_open();
    void do_new();
    void do_edit();
    void do_delete();

    HINSTANCE inst_;
    std::function<void(const nlohmann::json&)> dispatch_;
    HWND dlg_ = nullptr;
    nlohmann::json lists_ = nlohmann::json::array(); // current lists [{id,title,replies_policy,exclusive}]
};

// Show the create/edit sub-dialog. `list` is prefilled (empty object for New)
// and, on OK, replaced with {title, replies_policy, exclusive} (plus id when
// editing). Returns true if the user confirmed.
bool show_list_edit_dialog(HWND parent, HINSTANCE inst, nlohmann::json& list);

} // namespace fastsmui
