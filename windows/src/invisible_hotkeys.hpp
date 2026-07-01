#pragma once

#include <map>
#include <string>

#include <windows.h>

// Global-hotkey driver for the invisible interface (mode "hotkey"). Translates
// the core's canonical key-strings (e.g. "control+alt+shift+win+r",
// "alt+win+down", "alt+win+'") into Win32 RegisterHotKey registrations on the
// main window, and maps WM_HOTKEY ids back to the core action they fire.
//
// The low-level keyhook driver (mode "keyhook") and layer mode arrive in later
// phases; they will share the same key-string parsing.

namespace fastsmui {

// Parse a canonical key-string into RegisterHotKey (fsModifiers, vk). Returns
// false if the string has no mappable base key.
bool parse_hotkey(const std::string& key, UINT& mods_out, UINT& vk_out);

class HotkeyDriver {
public:
    void set_window(HWND hwnd) { hwnd_ = hwnd; }

    // Register global hotkeys for these key-string -> action bindings, replacing
    // any previously registered set. Bindings that fail to parse or that Windows
    // refuses (already taken) are skipped.
    void set_bindings(const std::map<std::string, std::string>& key_to_action);
    void clear();

    // The action id bound to a WM_HOTKEY id, or "" if unknown.
    std::string action_for(int hotkey_id) const;
    bool active() const { return !id_to_action_.empty(); }

private:
    HWND hwnd_ = nullptr;
    std::map<int, std::string> id_to_action_;
    static constexpr int kBaseId = 0xB000; // above menu/accelerator command ids
};

} // namespace fastsmui
