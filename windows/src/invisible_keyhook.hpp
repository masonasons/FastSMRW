#pragma once

#include <map>
#include <string>

#include <windows.h>

// Low-level keyboard-hook driver for the invisible interface (mode "keyhook").
// Unlike RegisterHotKey, a WH_KEYBOARD_LL hook sees keystrokes before the shell
// and the focused app, so it can capture combos Windows reserves (e.g. Win+arrow
// Snap shortcuts) and swallow them so they don't also do their normal thing.
//
// Bound combos post their action id to the window (WM_APP_INV_ACTION) and are
// consumed; unbound keys pass straight through untouched.

namespace fastsmui {

class KeyhookDriver {
public:
    ~KeyhookDriver();

    void set_window(HWND hwnd) { hwnd_ = hwnd; }
    // Replace the active key-string -> action bindings.
    void set_bindings(const std::map<std::string, std::string>& key_to_action);
    void enable();  // install the hook
    void disable(); // remove the hook
    bool active() const { return hook_ != nullptr; }

private:
    static LRESULT CALLBACK hook_proc(int code, WPARAM wp, LPARAM lp);
    // Returns the bound action for a physical key event, or "" if none.
    std::string lookup(DWORD vk) const;

    HWND hwnd_ = nullptr;
    HHOOK hook_ = nullptr;
    std::map<std::string, std::string> bindings_; // canonical key -> action
};

} // namespace fastsmui
