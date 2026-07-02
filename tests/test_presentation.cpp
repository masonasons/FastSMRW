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

void test_presenter_wrap_and_separator() {
    const std::int64_t now = 1000;
    Status s;
    s.account.display_name = "Alice";
    s.account.acct = "alice@x.social";
    s.text = "hello";
    s.created_at = now - 60;

    // Just author + handle, handle wrapped in parentheses, joined with " | ".
    present::SpeechSettings cfg;
    cfg.separator = " | ";
    cfg.status = {{present::StatusSpeechField::Author, true},
                  {present::StatusSpeechField::Handle, true}};
    cfg.status[1].before = "(";
    cfg.status[1].after = ")";
    present::SpeechConfig::set_current(cfg);
    present::TextConfig::set_current({});

    const std::string label = present::accessibility_label(s, now);
    CHECK_EQ(label, std::string("Alice | (@alice@x.social)"));

    // No separator after the author: the next item runs straight on.
    cfg.status[0].no_separator_after = true;
    present::SpeechConfig::set_current(cfg);
    CHECK_EQ(present::accessibility_label(s, now), std::string("Alice(@alice@x.social)"));

    present::SpeechConfig::set_current(present::SpeechSettings::defaults()); // restore
}

void test_post_links() {
    // A Mastodon post: HTML anchors (skipping @mention / #hashtag), a titled card,
    // a labeled media attachment, and finally the post's own URL.
    Status s;
    s.content =
        "<p>read <a href=\"https://example.com/path\">example.com/path</a> and "
        "<a href=\"https://tags.example/foo\" class=\"mention hashtag\">#foo</a> "
        "<a href=\"https://x.social/@bob\" class=\"u-url mention\">@bob</a></p>";
    s.text = "read example.com/path and #foo @bob";
    s.url = "https://x.social/@me/123";
    Card c;
    c.url = "https://card.example/article";
    c.title = "Great Article";
    s.card = c;
    MediaAttachment m;
    m.url = "https://media.example/pic.jpg";
    m.type = MediaAttachment::Kind::Image;
    m.description = "A cat";
    s.media_attachments.push_back(m);

    std::vector<present::PostLink> links = present::post_links(s);
    CHECK_EQ(links.size(), static_cast<size_t>(4)); // text link + card + media + post
    CHECK_EQ(links[0].url, std::string("https://example.com/path"));
    CHECK_EQ(links[0].title, std::string("example.com/path")); // the anchor's visible text
    CHECK_EQ(links[1].url, std::string("https://card.example/article"));
    CHECK_EQ(links[1].title, std::string("Great Article"));
    CHECK_EQ(links[2].url, std::string("https://media.example/pic.jpg"));
    CHECK_EQ(links[2].title, std::string("A cat (image)"));
    CHECK_EQ(links[3].url, std::string("https://x.social/@me/123"));
    CHECK_EQ(links[3].title, std::string("Open this post in browser"));

    // A boost unwraps to the boosted post's links.
    Status boost;
    boost.reblog = std::make_shared<Status>(s);
    CHECK_EQ(present::post_links(boost).size(), static_cast<size_t>(4));

    // Bluesky (no HTML content): raw URLs come from the plain text; post URL last.
    Status bsky;
    bsky.text = "check https://bsky.example/x here";
    bsky.url = "https://bsky.app/profile/a/post/1";
    std::vector<present::PostLink> bl = present::post_links(bsky);
    CHECK_EQ(bl.size(), static_cast<size_t>(2));
    CHECK_EQ(bl[0].url, std::string("https://bsky.example/x"));
    CHECK_EQ(bl[1].title, std::string("Open this post in browser"));

    // A post with only its own URL still offers that one link.
    Status plain;
    plain.text = "just some text, no links here.";
    plain.url = "https://x.social/@me/9";
    CHECK_EQ(present::post_links(plain).size(), static_cast<size_t>(1));
}
