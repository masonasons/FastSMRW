#pragma once

#include <functional>
#include <optional>
#include <string>

#include <windows.h>

#include <nlohmann/json.hpp>

// User aliases UI (Windows): a small prompt to add/edit/clear the alias for one
// user, and a manager listing every alias with edit / remove. Aliases live in
// the core (global, cross-account); these dialogs only send commands and render
// the `aliases_list` event.

namespace fastsmui {

// Prompt for a user's alias. `handle` is shown in the label (e.g. "alice@x");
// `current` prefills the field. Returns nullopt if cancelled, otherwise the new
// alias (an empty string means "clear the alias").
std::optional<std::string> show_alias_dialog(HWND parent, HINSTANCE inst, const std::string& handle,
                                             const std::string& current);

class AliasesManagerDialog {
public:
    AliasesManagerDialog(HINSTANCE inst, std::function<void(const nlohmann::json&)> dispatch);

    // Run modally. `initial` is the aliases_list event that triggered opening.
    void run(HWND parent, const nlohmann::json& initial);
    // Fed by the main window when an `aliases_list` event arrives (same UI thread).
    void on_aliases(const nlohmann::json& e);

private:
    static INT_PTR CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR handle(HWND dlg, UINT msg, WPARAM wp, LPARAM lp);
    void refresh_list();
    void update_enabled();
    int selected_index() const;
    void do_edit();
    void do_remove();

    HINSTANCE inst_;
    std::function<void(const nlohmann::json&)> dispatch_;
    HWND dlg_ = nullptr;
    nlohmann::json aliases_ = nlohmann::json::array(); // [{key, handle, alias}]
};

} // namespace fastsmui
