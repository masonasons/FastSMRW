#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Keymap model for the invisible interface (global hotkeys / keyhook / layer).
// This is the portable, testable core of the feature: the catalog of bindable
// actions, the .keymap text format, and the inheritance rules (a custom keymap
// overrides the read-only default by action, with `unbind:` lines disabling a
// default). The OS-specific input capture and the editor dialog live in the
// Win32 app; everything here is pure string/data logic.
//
// Key strings look like "control+win+r": zero or more modifiers
// (control/alt/shift/win, in that canonical order) joined by '+' to a single
// base key (a character like 'r'/'/' or a name like up/return/f5/space),
// all lowercase.

namespace fastsm::input {

// One bindable action. `id` is stable and used in keymap files + commands.
struct ActionDef {
    std::string id;          // e.g. "reply", "next_item"
    std::string label;       // human-readable, e.g. "Reply"
    std::string default_key; // default binding ("" = unbound by default)
};

// The canonical catalog of invisible-interface actions, in display order.
const std::vector<ActionDef>& action_catalog();
// Look up an action by id, or nullptr.
const ActionDef* find_action(const std::string& id);

// A resolved set of bindings: canonical key-string -> action id.
using KeyBindings = std::map<std::string, std::string>;

// The parsed contents of a .keymap file.
struct ParsedKeymap {
    std::map<std::string, std::string> bindings; // key -> action
    std::set<std::string> unbinds;               // action ids explicitly unbound
};

// Parse keymap file *contents* (not a path). Unknown/blank/`#` lines are skipped;
// keys are normalized. `unbind:Action` lines populate `unbinds`.
ParsedKeymap parse_keymap(const std::string& text);

// Serialize a custom keymap (action->key overrides + unbinds) back to file text,
// sorted for stable diffs. Only the user's overrides are written (inheritance
// fills the rest at load time).
std::string serialize_keymap(const std::map<std::string, std::string>& action_to_key,
                             const std::set<std::string>& unbinds);

// The default bindings (key -> action) built from the catalog's default_key.
KeyBindings default_bindings();

// Convert a shared .keymap file's contents (an old FastSM keymap, or a FastSMRW
// keymap a friend sent) into the overrides (action -> key) to save as a FastSMRW
// keymap. FastSM action tokens are renamed to the FastSMRW equivalents where they
// differ; tokens with no FastSMRW action are dropped; and any binding that already
// matches this action's FastSMRW default is dropped (so the result only carries
// genuine changes). `dropped`, if non-null, receives the count of skipped
// (unrecognized) bindings; `unbinds`, if non-null, receives the `unbind:` lines
// for recognized actions (present in FastSMRW keymaps, not FastSM ones).
std::map<std::string, std::string> import_fastsm_keymap(const std::string& text,
                                                        int* dropped = nullptr,
                                                        std::set<std::string>* unbinds = nullptr);

// The Layer-mode keymap: bare keys (no modifiers, e.g. "up", "r") -> action,
// used while the user is inside the "FastSM layer". Fixed (not user-editable).
KeyBindings layer_keymap();
// Spoken on entering the layer, and the full keystroke help (spoken on "/").
std::string layer_enter_message();
std::string layer_help_text();
// Default combo that toggles the FastSM layer on/off.
constexpr const char* kDefaultLayerKey = "control+win+space";

// Effective bindings = defaults, with any action redefined or unbound in the
// custom keymap removed from the defaults, then the custom bindings layered on
// top. Mirrors the Python loader's inheritance.
KeyBindings resolve_bindings(const ParsedKeymap& custom);

// Normalize a key-string to canonical form (lowercase, modifier order
// control+alt+shift+win, single recognized base key). Returns nullopt if the
// string has no base key, an unknown base key, or duplicate/none modifiers only.
std::optional<std::string> normalize_key(const std::string& key);

} // namespace fastsm::input
