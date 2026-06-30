#include "check.hpp"

#include "fastsm/models/serialization.hpp"

#include <memory>

using namespace fastsm;

// Build a representative status: a boost of a post that has a CW, media, a
// quote, a poll, and a reply target — exercising optionals, the recursive
// shared_ptr, and the variant.
static Status sample_inner() {
    Status s;
    s.id = "100";
    s.account.id = "u1";
    s.account.acct = "alice@example.social";
    s.account.display_name = "Alice";
    s.account.platform = Platform::Mastodon;
    s.content = "<p>hello &amp; welcome</p>";
    s.text = "hello & welcome";
    s.created_at = 1719600000;
    s.favourites_count = 3;
    s.boosts_count = 1;
    s.in_reply_to_id = "99";
    s.spoiler_text = "cw";
    s.visibility = Visibility::Unlisted;
    s.favourited = true;
    s.application_name = "FastSMRW";
    s.platform = Platform::Mastodon;

    MediaAttachment m;
    m.id = "m1";
    m.type = MediaAttachment::Kind::Image;
    m.url = "https://example/i.png";
    m.description = "a cat";
    s.media_attachments.push_back(m);

    Poll p;
    p.id = "p1";
    p.multiple = true;
    p.options.push_back({"yes", 2});
    p.options.push_back({"no", 1});
    s.poll = p;

    Status q;
    q.id = "50";
    q.text = "quoted text";
    q.account.acct = "bob";
    s.quote = std::make_shared<Status>(q);
    return s;
}

void test_status_roundtrip() {
    Status boost;
    boost.id = "200";
    boost.account.acct = "carol";
    boost.created_at = 1719600100;
    boost.reblog = std::make_shared<Status>(sample_inner());

    nlohmann::json j = boost;
    Status back = j.get<Status>();

    CHECK_EQ(back.id, std::string("200"));
    CHECK(back.is_boost());
    CHECK(back.reblog != nullptr);
    CHECK_EQ(back.reblog->text, std::string("hello & welcome"));
    CHECK(back.reblog->spoiler_text.has_value());
    CHECK_EQ(back.reblog->spoiler_text.value(), std::string("cw"));
    CHECK(back.reblog->visibility.has_value());
    CHECK(back.reblog->visibility.value() == Visibility::Unlisted);
    CHECK_EQ(back.reblog->media_attachments.size(), size_t(1));
    CHECK(back.reblog->media_attachments[0].type == MediaAttachment::Kind::Image);
    CHECK(back.reblog->poll.has_value());
    CHECK_EQ(back.reblog->poll->options.size(), size_t(2));
    CHECK_EQ(back.reblog->poll->options[1].votes_count, 1);
    CHECK(back.reblog->quote != nullptr);
    CHECK_EQ(back.reblog->quote->id, std::string("50"));
    CHECK(back.reblog->application_name.has_value());
    CHECK_EQ(back.reblog->application_name.value(), std::string("FastSMRW"));
}

void test_timeline_item_roundtrip() {
    // Status row
    TimelineItem si{sample_inner()};
    nlohmann::json j1 = si;
    TimelineItem b1 = j1.get<TimelineItem>();
    CHECK(b1.is_status());
    CHECK_EQ(b1.id(), std::string("s:100"));
    CHECK(b1.status() != nullptr);

    // Notification row carrying a status
    Notification n;
    n.id = "300";
    n.type = Notification::Kind::Favourite;
    n.account.acct = "dave";
    n.created_at = 1719600200;
    n.status = std::make_shared<Status>(sample_inner());
    TimelineItem ni{n};
    nlohmann::json j2 = ni;
    TimelineItem b2 = j2.get<TimelineItem>();
    CHECK(b2.is_notification());
    CHECK_EQ(b2.id(), std::string("n:300"));
    CHECK(b2.status() != nullptr); // resolved from the notification's post
    CHECK_EQ(b2.sort_date(), std::int64_t(1719600200));

    // User row
    User u;
    u.id = "u9";
    u.acct = "erin";
    TimelineItem ui{u};
    nlohmann::json j3 = ui;
    TimelineItem b3 = j3.get<TimelineItem>();
    CHECK(b3.is_user());
    CHECK_EQ(b3.id(), std::string("u:u9"));
}
