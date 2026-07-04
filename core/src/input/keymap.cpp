#include "fastsm/input/keymap.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace fastsm::input {
namespace {

std::string to_lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Base key names the keymap accepts (the Win32 driver maps these to VK codes).
const std::unordered_set<std::string>& named_keys() {
    static const std::unordered_set<std::string> keys = [] {
        std::unordered_set<std::string> s = {
            "up",     "down",   "left",     "right",  "home",   "end",    "pageup",
            "pagedown", "insert", "delete",  "return", "enter",  "escape", "space",
            "tab",    "back",   "backspace", "apps",   "pause",  "printscreen",
        };
        for (int i = 1; i <= 24; ++i)
            s.insert("f" + std::to_string(i));
        return s;
    }();
    return keys;
}

bool is_single_char_key(const std::string& s) {
    if (s.size() != 1)
        return false;
    char c = s[0];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        return true;
    return std::string("/;[]\\'=,-.`").find(c) != std::string::npos;
}

} // namespace

std::optional<std::string> normalize_key(const std::string& key) {
    bool control = false, alt = false, shift = false, win = false;
    std::string base;
    int base_count = 0;
    std::stringstream ss(key);
    std::string chunk;
    while (std::getline(ss, chunk, '+')) {
        const std::string low = to_lower(trim(chunk));
        if (low.empty())
            continue;
        if (low == "control" || low == "ctrl") {
            control = true;
        } else if (low == "alt") {
            alt = true;
        } else if (low == "shift") {
            shift = true;
        } else if (low == "win" || low == "windows" || low == "cmd" || low == "command" ||
                   low == "super") {
            win = true;
        } else {
            ++base_count;
            base = low;
        }
    }
    if (base_count != 1)
        return std::nullopt; // need exactly one base key
    if (!is_single_char_key(base) && named_keys().count(base) == 0)
        return std::nullopt;
    if (base == "enter")
        base = "return"; // canonicalize aliases
    if (base == "backspace")
        base = "back";
    std::string out;
    if (control)
        out += "control+";
    if (alt)
        out += "alt+";
    if (shift)
        out += "shift+";
    if (win)
        out += "win+";
    out += base;
    return out;
}

ParsedKeymap parse_keymap(const std::string& text) {
    ParsedKeymap out;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (line.rfind("unbind:", 0) == 0) {
            const std::string action = trim(line.substr(7));
            if (!action.empty())
                out.unbinds.insert(action);
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string action = trim(line.substr(eq + 1));
        if (action.empty())
            continue;
        if (auto norm = normalize_key(key))
            out.bindings[*norm] = action;
    }
    return out;
}

std::string serialize_keymap(const std::map<std::string, std::string>& action_to_key,
                             const std::set<std::string>& unbinds) {
    std::vector<std::string> lines;
    for (const auto& [action, key] : action_to_key)
        if (!key.empty())
            lines.push_back(key + "=" + action);
    std::sort(lines.begin(), lines.end());
    std::vector<std::string> ub(unbinds.begin(), unbinds.end());
    std::sort(ub.begin(), ub.end());
    for (const auto& a : ub)
        lines.push_back("unbind:" + a);
    std::string out;
    for (const auto& l : lines) {
        out += l;
        out += "\n";
    }
    return out;
}

KeyBindings layer_keymap() {
    // Bare keys mapped to actions while inside the layer. Escape and the
    // activation combo exit the layer (handled by the driver, not here).
    // Bare keys mirror the in-window shortcuts where possible: Space opens the
    // thread, Enter shows post info, single letters act on the current post.
    return {
        {"up", "prev_item"},         {"down", "next_item"},
        {"left", "prev_tl"},         {"right", "next_tl"},
        {"home", "top_item"},        {"end", "bottom_item"},
        {"pageup", "prev_item_jump"}, {"pagedown", "next_item_jump"},
        {"space", "open_thread"},    {"return", "Enter"},
        {"shift+return", "SecondaryAction"},
        {"r", "Reply"},              {"b", "BoostToggle"},
        {"f", "LikeToggle"},         {"q", "Quote"},
        {"e", "Edit"},               {"n", "Post"},
        {"p", "PinPost"},            {"t", "NewTimeline"},
        {"u", "UserTimeline"},        {"h", "FollowHashtag"},
        {"l", "FollowToggle"},        // L follows/unfollows the post author
        {"shift+h", "ToggleWindow"}, // Shift+H shows/hides the window
        {"o", "Url"},                {"w", "CloseTimeline"}, // W closes the current timeline
    };
}

std::string layer_enter_message() { return "FastSM layer. Press slash for help."; }

std::string layer_help_text() {
    // One key per line for the layer help window. Kept in sync with layer_keymap().
    return "Up arrow: Previous post\n"
           "Down arrow: Next post\n"
           "Left arrow: Previous timeline\n"
           "Right arrow: Next timeline\n"
           "Home: Top of timeline\n"
           "End: Bottom of timeline\n"
           "Page Up: Jump up\n"
           "Page Down: Jump down\n"
           "Space: Open thread\n"
           "Enter: Default action (view post, or your Behavior choice)\n"
           "Shift+Enter: Secondary action (play media, or your Behavior choice)\n"
           "R: Reply\n"
           "Q: Quote\n"
           "E: Edit\n"
           "N: New post\n"
           "B: Boost or unboost\n"
           "F: Like or unlike\n"
           "P: Pin or unpin your post to your profile\n"
           "T: New timeline\n"
           "U: User's timeline\n"
           "L: Follow or unfollow the user\n"
           "H: Follow a hashtag\n"
           "O: Open links\n"
           "W: Close timeline\n"
           "Shift+H: Show or hide window\n"
           "Slash: Show these keys\n"
           "Escape: Leave the layer";
}

std::map<std::string, std::string> import_fastsm_keymap(const std::string& text, int* dropped,
                                                        std::set<std::string>* unbinds) {
    // A few FastSM action tokens differ from FastSMRW's; the rest are identical.
    static const std::unordered_map<std::string, std::string> kRename = {
        {"Delete", "DeletePost"},          // FastSM's "Delete" = delete your post
        {"AccountOptions", "AccountSettings"},
        {"PinToggle", "PinPost"},
    };
    // Valid FastSMRW action ids + each action's normalized default key.
    std::unordered_set<std::string> valid;
    std::unordered_map<std::string, std::string> defaults;
    for (const auto& a : action_catalog()) {
        valid.insert(a.id);
        if (auto n = normalize_key(a.default_key))
            defaults[a.id] = *n;
    }
    std::map<std::string, std::string> out; // action -> key
    int skipped = 0;
    const ParsedKeymap parsed = parse_keymap(text); // key -> action (keys normalized)
    for (const auto& [key, action] : parsed.bindings) {
        std::string act = action;
        if (auto it = kRename.find(act); it != kRename.end())
            act = it->second;
        if (!valid.count(act)) { // no FastSMRW equivalent -> skip
            ++skipped;
            continue;
        }
        if (auto d = defaults.find(act); d != defaults.end() && d->second == key)
            continue; // already the FastSMRW default -> nothing to override
        out[act] = key;
    }
    // Carry over unbind: lines (FastSMRW keymaps) for actions we recognize.
    if (unbinds)
        for (const auto& a : parsed.unbinds) {
            std::string act = a;
            if (auto it = kRename.find(act); it != kRename.end())
                act = it->second;
            if (valid.count(act))
                unbinds->insert(act);
        }
    if (dropped)
        *dropped = skipped;
    return out;
}

KeyBindings default_bindings() {
    KeyBindings out;
    for (const auto& a : action_catalog()) {
        if (a.default_key.empty())
            continue;
        if (auto norm = normalize_key(a.default_key))
            out[*norm] = a.id;
    }
    return out;
}

KeyBindings resolve_bindings(const ParsedKeymap& custom) {
    KeyBindings defaults = default_bindings();
    std::unordered_set<std::string> custom_actions(custom.unbinds.begin(), custom.unbinds.end());
    for (const auto& [key, action] : custom.bindings)
        custom_actions.insert(action);
    KeyBindings out;
    for (const auto& [key, action] : defaults)
        if (custom_actions.count(action) == 0)
            out[key] = action;
    for (const auto& [key, action] : custom.bindings)
        out[key] = action;
    return out;
}

const std::vector<ActionDef>& action_catalog() {
    // Defaults mirror the Python original's alt+win / control+win combos, which
    // are screen-reader-friendly and unlikely to collide with other apps under
    // RegisterHotKey. The Win32 driver maps these strings to VK codes.
    // Action ids match the Python FastSM tokens (Reply, BoostToggle, next_item,
    // ...) so .keymap files are interchangeable between the two clients for easy
    // sharing. Actions one client lacks are simply ignored by the other.
    static const std::vector<ActionDef> catalog = {
        // --- navigation ---
        {"next_item", "Next item", "control+alt+win+down"},
        {"prev_item", "Previous item", "control+alt+win+up"},
        {"next_item_jump", "Jump forward (20 items)", "control+win+pagedown"},
        {"prev_item_jump", "Jump back (20 items)", "control+win+pageup"},
        {"top_item", "Top of timeline", "alt+win+home"},
        {"bottom_item", "Bottom of timeline", "alt+win+end"},
        {"next_tl", "Next timeline", "control+alt+win+right"},
        {"prev_tl", "Previous timeline", "control+alt+win+left"},
        {"NextAccount", "Next account", "control+shift+win+pagedown"},
        {"PrevAccount", "Previous account", "control+shift+win+pageup"},
        {"speak_item", "Speak current item", "alt+win+space"},
        {"UndoNavigation", "Undo navigation (go back)", "alt+win+z"},
        {"refresh", "Refresh timeline", "control+alt+win+u"},
        {"Find", "Find in timeline", ""},   // unbound by default (in-app Ctrl+F)
        {"FindNext", "Find next", ""},      // unbound by default (in-app F3)
        {"FindPrev", "Find previous", ""},  // unbound by default (in-app Shift+F3)
        // --- post actions ---
        {"Reply", "Reply", "control+win+r"},
        {"Quote", "Quote post", "alt+win+q"},
        {"Edit", "Edit post", "alt+win+e"},
        {"Post", "New post", "alt+win+n"},
        {"BoostToggle", "Boost / Unboost", "control+shift+win+r"},
        {"LikeToggle", "Like / Unlike", "alt+win+i"},
        {"View", "Post info", "alt+win+v"},
        {"Enter", "Default action (like Enter)", "alt+win+return"},
        {"SecondaryAction", "Secondary action (like Shift+Enter)", "alt+shift+win+return"},
        {"Url", "Open link in post", ""}, // unbound by default (was alt+win+return)
        {"open_thread", "View thread", "alt+win+t"},
        // --- user actions ---
        {"UserTimeline", "Open user timeline", "alt+win+u"},
        {"UserProfile", "Open user profile", "alt+win+shift+u"},
        {"FollowToggle", "Follow / Unfollow", "alt+win+l"},
        {"MuteToggle", "Mute / Unmute user", "alt+win+shift+l"},
        {"BlockToggle", "Block / Unblock user", "control+shift+win+b"},
        // --- timeline / app ---
        {"NewTimeline", "New timeline", ""}, // unbound by default (in-app Ctrl+T); opens the dialog
        {"MoveTimelineUp", "Move timeline up", ""},       // unbound (in-app Shift+Up)
        {"MoveTimelineDown", "Move timeline down", ""},   // unbound (in-app Shift+Down)
        {"TogglePin", "Pin / unpin timeline", ""},        // unbound (in-app Ctrl+P)
        {"PinPost", "Pin / unpin post to profile", ""},   // unbound (in-app / layer P)
        {"DeletePost", "Delete your post", "alt+win+delete"}, // in-app Delete; Win8.1 overrides
        {"FollowHashtag", "Follow a hashtag", "alt+win+h"}, // in-app / layer H
        {"ManageHashtags", "Manage followed hashtags", ""}, // unbound by default
        {"CloseTimeline", "Close timeline", "alt+win+'"},
        {"ToggleWindow", "Show / hide window", "control+win+w"},
        {"EnterLayer", "Open the layer", ""}, // unbound: call up the layer from hotkey/keyhook mode
        {"Options", "Settings", "alt+win+o"},
        {"AccountSettings", "Account settings", ""}, // unbound by default (in-app Ctrl+Shift+,)
        {"KeymapManager", "Keyboard manager", "control+alt+win+k"},
        {"StopMedia", "Stop media playback", "control+alt+win+s"},
    };
    return catalog;
}

const ActionDef* find_action(const std::string& id) {
    for (const auto& a : action_catalog())
        if (a.id == id)
            return &a;
    return nullptr;
}

} // namespace fastsm::input
