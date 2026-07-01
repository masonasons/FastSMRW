#include "check.hpp"

#include "fastsm/input/keymap.hpp"

using namespace fastsm;
using namespace fastsm::input;

void test_keymap_normalize() {
    // Modifier ordering is canonicalized regardless of input order/case.
    CHECK_EQ(*normalize_key("WIN+Shift+Control+R"), std::string("control+shift+win+r"));
    CHECK_EQ(*normalize_key("ctrl+alt+win+u"), std::string("control+alt+win+u"));
    CHECK_EQ(*normalize_key("alt+win+down"), std::string("alt+win+down"));
    // Aliases canonicalize.
    CHECK_EQ(*normalize_key("alt+win+enter"), std::string("alt+win+return"));
    // Single-char + punctuation base keys.
    CHECK_EQ(*normalize_key("alt+win+'"), std::string("alt+win+'"));
    CHECK_EQ(*normalize_key("alt+win+/"), std::string("alt+win+/"));
    // No base key, or two base keys -> invalid.
    CHECK(!normalize_key("control+shift").has_value());
    CHECK(!normalize_key("a+b").has_value());
    CHECK(!normalize_key("alt+win+notakey").has_value());
}

void test_keymap_default_and_catalog() {
    const auto& cat = action_catalog();
    CHECK(cat.size() > 10);
    CHECK(find_action("Reply") != nullptr); // tokens match the Python client for sharing
    CHECK(find_action("does_not_exist") == nullptr);

    KeyBindings def = default_bindings();
    // Reply default is control+win+r -> "Reply".
    auto it = def.find("control+win+r");
    CHECK(it != def.end());
    CHECK_EQ(it->second, std::string("Reply"));
    // No two default bindings collide on the same key: every catalog entry with a
    // default produced a distinct binding, so the map size equals the count.
    size_t with_default = 0;
    for (const auto& a : action_catalog())
        if (!a.default_key.empty())
            ++with_default;
    CHECK_EQ(def.size(), with_default);
}

void test_keymap_parse_and_serialize() {
    const std::string text =
        "# a comment\n"
        "control+win+r=reply\n"
        "WIN+alt+k=favorite_toggle\n"  // normalized on parse
        "unbind:refresh\n"
        "\n"
        "garbage line without equals\n";
    ParsedKeymap p = parse_keymap(text);
    CHECK_EQ(p.bindings.size(), size_t(2));
    CHECK_EQ(p.bindings["control+win+r"], std::string("reply"));
    // key got normalized to canonical modifier order.
    CHECK_EQ(p.bindings["alt+win+k"], std::string("favorite_toggle"));
    CHECK(p.unbinds.count("refresh") == 1);

    const std::string out = serialize_keymap({{"reply", "control+win+r"}}, {"refresh"});
    CHECK(out.find("control+win+r=reply\n") != std::string::npos);
    CHECK(out.find("unbind:refresh\n") != std::string::npos);
}

void test_keymap_inheritance() {
    // Custom rebinds Reply to a new key and unbinds refresh; everything else
    // inherits from default.
    ParsedKeymap custom;
    custom.bindings["alt+win+y"] = "Reply"; // move Reply off its default key
    custom.unbinds.insert("refresh");

    KeyBindings eff = resolve_bindings(custom);
    // Reply is now on the custom key, and its default key no longer maps to Reply.
    CHECK_EQ(eff["alt+win+y"], std::string("Reply"));
    CHECK(eff.find("control+win+r") == eff.end()); // old default key dropped
    // refresh's default key is gone (unbound).
    CHECK(eff.find("control+alt+win+u") == eff.end());
    // An untouched default (Like) still present.
    CHECK_EQ(eff["alt+win+i"], std::string("LikeToggle"));
}
