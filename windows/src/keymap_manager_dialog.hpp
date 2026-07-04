#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <windows.h>

#include <nlohmann/json.hpp>

// The Keyboard Manager: create/edit/delete keymaps for the invisible interface.
// It mirrors the Python client's manager (inheritance from the read-only default,
// unbind support, collision detection) and edits only the user's overrides, so
// saved .keymap files stay interchangeable between the two clients.
//
// The dialog is modal but the core is async: it sends commands through the
// `dispatch` callback and receives keymap events via on_keymap(), which the main
// window forwards while the modal message loop pumps.

namespace fastsmui {

// One bindable action, as sent by the core's action_catalog event.
struct KmAction {
    std::string id;
    std::string label;
    std::string default_key;
};

class KeymapManagerDialog {
public:
    KeymapManagerDialog(HINSTANCE inst, std::vector<KmAction> catalog,
                        std::string active_keymap,
                        std::function<void(const nlohmann::json&)> dispatch);

    // Run modally against `parent`. Returns when the user closes it.
    void run(HWND parent);

    // Fed by the main window when a keymap event arrives (same UI thread).
    void on_keymap(const nlohmann::json& e);

private:
    static INT_PTR CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    INT_PTR handle(HWND dlg, UINT msg, WPARAM wp, LPARAM lp);

    void populate_keymap_combo();
    void refresh_list();
    void update_enabled();
    void set_status();
    // (key, source) for an action: source is "default", "custom", or "unbound".
    std::pair<std::string, std::string> effective(const std::string& action) const;
    std::string selected_action() const;
    bool is_builtin(const std::string& name) const {
        return name == "default" || builtins_.count(name) > 0;
    }
    bool editable() const { return !current_name_.empty() && !is_builtin(current_name_); }
    void switch_keymap(const std::string& name); // request + edit another keymap
    void do_set_binding();
    void do_unbind();
    void do_reset();
    void do_new();
    void do_delete();
    void do_save();
    void do_import(); // import a FastSM / FastSMRW .keymap file as a new keymap
    bool confirm_discard(); // true if OK to lose unsaved changes

    HINSTANCE inst_;
    std::vector<KmAction> catalog_;
    std::map<std::string, std::string> default_key_; // action -> default key
    std::function<void(const nlohmann::json&)> dispatch_;

    HWND dlg_ = nullptr;
    std::vector<std::string> keymaps_{"default"};
    std::set<std::string> builtins_;       // read-only keymaps (default + shipped built-ins)
    std::string current_name_;             // keymap being edited
    std::string requested_name_;           // name of the last get_keymap we sent
    std::map<std::string, std::string> overrides_; // action -> key (custom layer)
    std::set<std::string> unbinds_;
    bool dirty_ = false;
    bool suppress_combo_ = false; // ignore CBN_SELCHANGE during programmatic set
};

// Render a canonical key-string ("control+shift+win+r") for display
// ("Ctrl+Shift+Win+R"). Empty string -> "(unbound)".
std::wstring format_key_display(const std::string& key);

// Show the modal key-capture dialog (modifier checkboxes + a base key), prefilled
// from `current_key`. Returns the chosen canonical key-string, or nullopt if
// cancelled. Used both by the manager and to set the layer activation key.
std::optional<std::string> capture_key_binding(HWND parent, HINSTANCE inst,
                                               const std::string& current_key);

} // namespace fastsmui
