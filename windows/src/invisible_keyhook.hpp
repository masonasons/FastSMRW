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
    // Hotkey mode: bound key-strings (with modifiers) are swallowed + fire actions.
    void set_hotkeys(const std::map<std::string, std::string>& key_to_action);
    // Layer mode: `activation_key` toggles a modal layer in which bare keys from
    // `layer_bindings` fire actions and everything is swallowed; Escape or the
    // activation combo exits. Enter/exit post the sentinel actions below.
    void set_layer(const std::string& activation_key,
                   const std::map<std::string, std::string>& layer_bindings);
    void enable();  // install the hook
    void disable(); // remove the hook
    bool active() const { return hook_ != nullptr; }
    // Drop out of the layer (e.g. an action opened a modal dialog). No-op if not
    // in the layer. Called on the UI thread (same as the hook proc).
    void exit_layer() { in_layer_ = false; }

    // Sentinel "actions" posted via WM_APP_INV_ACTION for layer open/close/help.
    static constexpr const char* kLayerEnter = "__layer_enter__";
    static constexpr const char* kLayerExit = "__layer_exit__";
    static constexpr const char* kLayerHelp = "__layer_help__";

private:
    enum class Mode { Hotkeys, Layer };
    static LRESULT CALLBACK hook_proc(int code, WPARAM wp, LPARAM lp);
    std::string key_string(DWORD vk) const;  // canonical modifiered key for a VK
    std::string lookup(DWORD vk) const;      // hotkeys-mode action for a VK, or ""

    HWND hwnd_ = nullptr;
    HHOOK hook_ = nullptr;
    Mode mode_ = Mode::Hotkeys;
    std::map<std::string, std::string> bindings_;       // hotkeys: canonical key -> action
    std::map<std::string, std::string> layer_bindings_; // layer: bare base key -> action
    std::string activation_key_;                        // layer toggle combo
    bool in_layer_ = false;
};

} // namespace fastsmui
