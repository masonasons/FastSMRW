#pragma once

#include <functional>
#include <string>

#include <windows.h>

#include <nlohmann/json.hpp>

// The Server Filters manager (Mastodon /api/v2/filters): list, add, edit, delete.
// Like the Keyboard Manager it is modal but the core is async — it sends commands
// through `dispatch` and the main window forwards the server_filters events into
// on_server_filters() while the modal message loop pumps.

namespace fastsmui {

class ServerFiltersDialog {
public:
    ServerFiltersDialog(HINSTANCE inst, std::function<void(const nlohmann::json&)> dispatch);

    // Run modally. `initial` is the server_filters event that triggered opening.
    void run(HWND parent, const nlohmann::json& initial);
    // Fed by the main window when a server_filters event arrives (same UI thread).
    void on_server_filters(const nlohmann::json& e);

private:
    static INT_PTR CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR handle(HWND dlg, UINT msg, WPARAM wp, LPARAM lp);
    void refresh_list();
    void update_enabled();
    int selected_index() const;
    void do_add();
    void do_edit();
    void do_delete();

    HINSTANCE inst_;
    std::function<void(const nlohmann::json&)> dispatch_;
    HWND dlg_ = nullptr;
    nlohmann::json filters_ = nlohmann::json::array(); // current server filters
};

// Show the add/edit sub-dialog. `filter` is prefilled (empty object for Add) and,
// on Save, replaced with the edited filter as JSON (title/action/context/
// expires_in/keywords, plus id when editing). Returns true if saved.
bool show_edit_filter_dialog(HWND parent, HINSTANCE inst, nlohmann::json& filter);

} // namespace fastsmui
