#include "invisible_hotkeys.hpp"

#include <sstream>
#include <unordered_map>

namespace fastsmui {
namespace {

// Named base keys -> virtual-key codes (parallels keymap.cpp's named_keys()).
UINT vk_for_name(const std::string& name) {
    static const std::unordered_map<std::string, UINT> map = {
        {"up", VK_UP},         {"down", VK_DOWN},     {"left", VK_LEFT},
        {"right", VK_RIGHT},   {"home", VK_HOME},     {"end", VK_END},
        {"pageup", VK_PRIOR},  {"pagedown", VK_NEXT}, {"insert", VK_INSERT},
        {"delete", VK_DELETE}, {"return", VK_RETURN}, {"escape", VK_ESCAPE},
        {"space", VK_SPACE},   {"tab", VK_TAB},       {"back", VK_BACK},
        {"apps", VK_APPS},     {"pause", VK_PAUSE},   {"printscreen", VK_SNAPSHOT},
    };
    if (auto it = map.find(name); it != map.end())
        return it->second;
    if (name.size() >= 2 && name[0] == 'f') { // f1..f24
        int n = 0;
        for (size_t i = 1; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9')
                return 0;
            n = n * 10 + (name[i] - '0');
        }
        if (n >= 1 && n <= 24)
            return static_cast<UINT>(VK_F1 + (n - 1));
    }
    return 0;
}

} // namespace

bool parse_hotkey(const std::string& key, UINT& mods_out, UINT& vk_out) {
    UINT mods = 0;
    UINT vk = 0;
    std::stringstream ss(key);
    std::string chunk;
    while (std::getline(ss, chunk, '+')) {
        if (chunk.empty())
            continue;
        if (chunk == "control")
            mods |= MOD_CONTROL;
        else if (chunk == "alt")
            mods |= MOD_ALT;
        else if (chunk == "shift")
            mods |= MOD_SHIFT;
        else if (chunk == "win")
            mods |= MOD_WIN;
        else { // the single base key (keymap strings are already canonical)
            if (chunk.size() == 1) {
                const char c = chunk[0];
                if (c >= 'a' && c <= 'z')
                    vk = static_cast<UINT>('A' + (c - 'a'));
                else if (c >= '0' && c <= '9')
                    vk = static_cast<UINT>(c);
                else {
                    const SHORT s = VkKeyScanW(static_cast<wchar_t>(c));
                    if (s != -1)
                        vk = static_cast<UINT>(s & 0xFF);
                }
            } else {
                vk = vk_for_name(chunk);
            }
        }
    }
    if (vk == 0)
        return false;
    mods_out = mods;
    vk_out = vk;
    return true;
}

void HotkeyDriver::set_bindings(const std::map<std::string, std::string>& key_to_action) {
    clear();
    if (!hwnd_)
        return;
    int id = kBaseId;
    for (const auto& [key, action] : key_to_action) {
        UINT mods = 0, vk = 0;
        if (!parse_hotkey(key, mods, vk))
            continue;
        // MOD_NOREPEAT (Win7+) stops held keys from firing repeatedly. A combo
        // already owned by another process or the Windows shell (e.g. Win+arrow
        // Snap shortcuts) is refused here; the keyhook mode captures those.
        if (RegisterHotKey(hwnd_, id, mods, vk))
            id_to_action_[id++] = action;
    }
}

void HotkeyDriver::clear() {
    if (hwnd_)
        for (const auto& [id, action] : id_to_action_)
            UnregisterHotKey(hwnd_, id);
    id_to_action_.clear();
}

std::string HotkeyDriver::action_for(int hotkey_id) const {
    auto it = id_to_action_.find(hotkey_id);
    return it == id_to_action_.end() ? std::string{} : it->second;
}

} // namespace fastsmui
