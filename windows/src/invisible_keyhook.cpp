#include "invisible_keyhook.hpp"

#include <string>

#include "app_messages.hpp"

namespace fastsmui {
namespace {

KeyhookDriver* g_active = nullptr; // the driver whose hook is installed

bool is_modifier_vk(DWORD vk) {
    switch (vk) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

// Physical virtual-key -> the base token used in keymap strings (inverse of
// invisible_hotkeys' parse). Returns "" for keys we don't bind.
std::string vk_to_base(DWORD vk) {
    if (vk >= 'A' && vk <= 'Z')
        return std::string(1, static_cast<char>('a' + (vk - 'A')));
    if (vk >= '0' && vk <= '9')
        return std::string(1, static_cast<char>('0' + (vk - '0')));
    if (vk >= VK_F1 && vk <= VK_F24)
        return "f" + std::to_string(static_cast<int>(vk - VK_F1) + 1);
    switch (vk) {
    case VK_UP: return "up";
    case VK_DOWN: return "down";
    case VK_LEFT: return "left";
    case VK_RIGHT: return "right";
    case VK_HOME: return "home";
    case VK_END: return "end";
    case VK_PRIOR: return "pageup";
    case VK_NEXT: return "pagedown";
    case VK_INSERT: return "insert";
    case VK_DELETE: return "delete";
    case VK_RETURN: return "return";
    case VK_ESCAPE: return "escape";
    case VK_SPACE: return "space";
    case VK_TAB: return "tab";
    case VK_BACK: return "back";
    case VK_APPS: return "apps";
    case VK_PAUSE: return "pause";
    case VK_SNAPSHOT: return "printscreen";
    case VK_OEM_1: return ";";
    case VK_OEM_2: return "/";
    case VK_OEM_3: return "`";
    case VK_OEM_4: return "[";
    case VK_OEM_5: return "\\";
    case VK_OEM_6: return "]";
    case VK_OEM_7: return "'";
    case VK_OEM_PLUS: return "=";
    case VK_OEM_MINUS: return "-";
    case VK_OEM_COMMA: return ",";
    case VK_OEM_PERIOD: return ".";
    default: return "";
    }
}

bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

} // namespace

KeyhookDriver::~KeyhookDriver() { disable(); }

std::string KeyhookDriver::key_string(DWORD vk) const {
    const std::string base = vk_to_base(vk);
    if (base.empty())
        return {};
    std::string key;
    if (down(VK_CONTROL))
        key += "control+";
    if (down(VK_MENU))
        key += "alt+";
    if (down(VK_SHIFT))
        key += "shift+";
    if (down(VK_LWIN) || down(VK_RWIN))
        key += "win+";
    return key + base;
}

std::string KeyhookDriver::lookup(DWORD vk) const {
    const std::string key = key_string(vk);
    if (key.empty())
        return {};
    auto it = bindings_.find(key);
    return it == bindings_.end() ? std::string{} : it->second;
}

LRESULT CALLBACK KeyhookDriver::hook_proc(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION || !g_active || (wp != WM_KEYDOWN && wp != WM_SYSKEYDOWN))
        return CallNextHookEx(nullptr, code, wp, lp);
    auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
    if ((kb->flags & LLKHF_INJECTED) || is_modifier_vk(kb->vkCode))
        return CallNextHookEx(nullptr, code, wp, lp); // never swallow modifiers/injected

    auto post = [&](const char* action) {
        PostMessageW(g_active->hwnd_, WM_APP_INV_ACTION, 0,
                     reinterpret_cast<LPARAM>(new std::string(action)));
    };

    if (g_active->mode_ == Mode::Layer) {
        const std::string base = vk_to_base(kb->vkCode);
        if (!g_active->in_layer_) {
            if (g_active->key_string(kb->vkCode) == g_active->activation_key_) {
                g_active->in_layer_ = true;
                post(kLayerEnter);
                return 1;
            }
            return CallNextHookEx(nullptr, code, wp, lp); // outside the layer: pass through
        }
        // Inside the layer: Escape or the activation combo exits; mapped keys fire
        // their action; anything else passes through so the keyboard is never fully
        // locked (a safety net if the layer ever gets stuck open).
        if (base == "escape" || g_active->key_string(kb->vkCode) == g_active->activation_key_) {
            g_active->in_layer_ = false;
            post(kLayerExit);
            return 1;
        }
        if (base == "/") { // speak the keystroke help
            post(kLayerHelp);
            return 1;
        }
        auto it = g_active->layer_bindings_.find(base);
        if (it != g_active->layer_bindings_.end()) {
            post(it->second.c_str());
            return 1;
        }
        return CallNextHookEx(nullptr, code, wp, lp); // unmapped key: pass through
    }

    // Hotkeys mode.
    const std::string action = g_active->lookup(kb->vkCode);
    if (!action.empty()) {
        // Keep the hook proc fast and side-effect-free: never call SendInput here
        // (doing so made Windows ignore the swallow and let Win+arrow Snap through).
        post(action.c_str());
        return 1; // swallow: don't let the shell / focused app see it
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void KeyhookDriver::set_hotkeys(const std::map<std::string, std::string>& key_to_action) {
    mode_ = Mode::Hotkeys;
    bindings_ = key_to_action;
    in_layer_ = false;
}

void KeyhookDriver::set_layer(const std::string& activation_key,
                              const std::map<std::string, std::string>& layer_bindings) {
    mode_ = Mode::Layer;
    activation_key_ = activation_key;
    layer_bindings_ = layer_bindings;
    in_layer_ = false;
}

void KeyhookDriver::enable() {
    if (hook_)
        return;
    g_active = this;
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &KeyhookDriver::hook_proc, GetModuleHandleW(nullptr), 0);
    if (!hook_)
        g_active = nullptr;
}

void KeyhookDriver::disable() {
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    if (g_active == this)
        g_active = nullptr;
}

} // namespace fastsmui
