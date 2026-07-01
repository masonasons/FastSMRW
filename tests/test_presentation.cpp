#include "check.hpp"

#include <memory>

#include "fastsm/presentation/status_presenter.hpp"

using namespace fastsm;

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void test_presenter_compact() {
    const std::int64_t now = 1000;
    Status s;
    s.account.display_name = "Alice";
    s.account.acct = "alice@x.social";
    s.text = "hello world";
    s.created_at = now - 300; // 5 minutes
    CHECK_EQ(present::compact_line(s, now), std::string("Alice (5m): hello world"));
}

void test_presenter_boost_compact() {
    const std::int64_t now = 1000;
    Status inner;
    inner.account.display_name = "Alice";
    inner.text = "original";
    inner.created_at = now - 60;
    Status boost;
    boost.account.display_name = "Carol";
    boost.created_at = now - 30;
    boost.reblog = std::make_shared<Status>(inner);
    const std::string line = present::compact_line(boost, now);
    CHECK(contains(line, "Carol"));
    CHECK(contains(line, "Alice"));
    CHECK(contains(line, "original"));
}

static Status sample_status(std::int64_t now) {
    Status s;
    s.account.display_name = "Alice";
    s.account.acct = "alice@x.social";
    s.text = "hello world";
    s.created_at = now - 300;
    s.replies_count = 1;
    s.boosts_count = 0;
    s.favourites_count = 2;
    s.favourited = true;
    s.spoiler_text = "cw";
    s.application_name = "FastSMRW";
    return s;
}

void test_presenter_accessibility_all_fields() {
    const std::int64_t now = 1000;
    const Status s = sample_status(now);

    // "Show" mode so both the warning and the body render for this check.
    present::TextPresentation tp;
    tp.cw = present::CwMode::Show;
    present::TextConfig::set_current(tp);

    // Enable every field so all render paths are exercised.
    auto fields = present::SpeechSettings::defaults().status;
    for (auto& f : fields)
        f.enabled = true;

    const std::string label = present::accessibility_label(s, now, fields);
    CHECK(contains(label, "Alice"));
    CHECK(contains(label, "@alice@x.social"));
    CHECK(contains(label, "Content warning: cw"));
    CHECK(contains(label, "hello world"));
    CHECK(contains(label, "5 minutes ago"));
    CHECK(contains(label, "1 reply"));
    CHECK(contains(label, "2 favorites"));
    CHECK(contains(label, "Favorited"));
    CHECK(contains(label, "via FastSMRW"));
    present::TextConfig::set_current({}); // restore default for later tests
}

void test_presenter_accessibility_default_config() {
    const std::int64_t now = 1000;
    Status s = sample_status(now);
    s.spoiler_text.reset(); // no content warning, so the body always reads
    present::TextConfig::set_current({});
    // Default config: handle, source, visibility, replyIndicator are OFF.
    const std::string label = present::accessibility_label(s, now);
    CHECK(contains(label, "Alice"));       // author on
    CHECK(contains(label, "hello world")); // text on
    CHECK(!contains(label, "@alice"));     // handle off by default
    CHECK(!contains(label, "via "));       // source off by default
}

// Content-warning modes: hide (body suppressed), show (both), ignore (body only).
void test_presenter_cw_modes() {
    const std::int64_t now = 1000;
    const Status s = sample_status(now); // has spoiler "cw", text "hello world"
    auto fields = present::SpeechSettings::defaults().status;
    for (auto& f : fields)
        f.enabled = true;

    present::TextPresentation tp;
    tp.cw = present::CwMode::Hide;
    present::TextConfig::set_current(tp);
    std::string hide = present::accessibility_label(s, now, fields);
    CHECK(contains(hide, "Content warning: cw"));
    CHECK(!contains(hide, "hello world"));

    tp.cw = present::CwMode::Show;
    present::TextConfig::set_current(tp);
    std::string show = present::accessibility_label(s, now, fields);
    CHECK(contains(show, "Content warning: cw"));
    CHECK(contains(show, "hello world"));

    tp.cw = present::CwMode::Ignore;
    present::TextConfig::set_current(tp);
    std::string ignore = present::accessibility_label(s, now, fields);
    CHECK(!contains(ignore, "Content warning"));
    CHECK(contains(ignore, "hello world"));

    // Compact line mirrors the mode with a "[CW]" marker.
    tp.cw = present::CwMode::Hide;
    present::TextConfig::set_current(tp);
    std::string compact = present::compact_line(s, now);
    CHECK(contains(compact, "[CW] cw"));
    CHECK(!contains(compact, "hello world"));
    present::TextConfig::set_current({});
}

// Demojify + leading-mention collapse flow through the presenter's text.
void test_presenter_demojify_and_mentions() {
    const std::int64_t now = 1000;
    Status s;
    s.account.display_name = "Bob \xF0\x9F\x98\x80"; // "Bob 😀"
    s.text = "@a @b @c @d hi :wave: \xF0\x9F\x91\x8B"; // trailing custom + unicode emoji
    s.created_at = now - 60;

    present::TextPresentation tp;
    tp.cw = present::CwMode::Show;
    tp.post_emoji = present::EmojiRemoval::Both;
    tp.name_emoji = present::EmojiRemoval::Unicode;
    tp.max_mentions = 2;
    present::TextConfig::set_current(tp);

    auto fields = present::SpeechSettings::defaults().status;
    for (auto& f : fields)
        f.enabled = true;
    const std::string label = present::accessibility_label(s, now, fields);
    CHECK(contains(label, "Bob"));
    CHECK(!contains(label, "\xF0\x9F\x98\x80"));   // 😀 stripped from name
    CHECK(contains(label, "@a @b and 2 others"));   // mentions collapsed
    CHECK(!contains(label, ":wave:"));              // custom emoji stripped
    CHECK(!contains(label, "\xF0\x9F\x91\x8B"));   // 👋 stripped
    present::TextConfig::set_current({});
}
