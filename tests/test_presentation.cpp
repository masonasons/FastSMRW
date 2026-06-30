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
}

void test_presenter_accessibility_default_config() {
    const std::int64_t now = 1000;
    const Status s = sample_status(now);
    // Default config: handle, source, visibility, replyIndicator are OFF.
    const std::string label = present::accessibility_label(s, now);
    CHECK(contains(label, "Alice"));       // author on
    CHECK(contains(label, "hello world")); // text on
    CHECK(!contains(label, "@alice"));     // handle off by default
    CHECK(!contains(label, "via "));       // source off by default
}
