#include "check.hpp"

#include "fastsm/store/timeline_codec.hpp"

#include <memory>

using namespace fastsm;

// Build a representative status: a boost of a post that has a CW, media, a
// quote, a poll, a reply target, and Bluesky strong-ref fields — exercising
// optionals, the recursive shared_ptr, and the variant through the binary codec.
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
    s.cid = "bafyabc";
    s.like_uri = "at://like/1";
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

    const std::string blob = store::encode_items({TimelineItem{boost}});
    const std::vector<TimelineItem> back_items = store::decode_items(blob);
    CHECK_EQ(back_items.size(), size_t(1));
    const Status* bp = back_items[0].status();
    CHECK(bp != nullptr);
    const Status& back = *bp;

    CHECK_EQ(back.id, std::string("200"));
    CHECK(back.is_boost());
    CHECK(back.reblog != nullptr);
    CHECK_EQ(back.reblog->text, std::string("hello & welcome"));
    CHECK(back.reblog->spoiler_text.has_value());
    CHECK_EQ(back.reblog->spoiler_text.value(), std::string("cw"));
    CHECK(back.reblog->visibility.value() == Visibility::Unlisted);
    CHECK_EQ(back.reblog->media_attachments.size(), size_t(1));
    CHECK(back.reblog->media_attachments[0].type == MediaAttachment::Kind::Image);
    CHECK(back.reblog->poll.has_value());
    CHECK_EQ(back.reblog->poll->options.size(), size_t(2));
    CHECK_EQ(back.reblog->poll->options[1].votes_count, 1);
    CHECK(back.reblog->quote != nullptr);
    CHECK_EQ(back.reblog->quote->id, std::string("50"));
    CHECK_EQ(back.reblog->application_name.value(), std::string("FastSMRW"));
    CHECK_EQ(back.reblog->cid.value(), std::string("bafyabc"));
    CHECK_EQ(back.reblog->like_uri.value(), std::string("at://like/1"));
}

void test_timeline_item_roundtrip() {
    std::vector<TimelineItem> items;
    items.push_back(TimelineItem{sample_inner()}); // status row

    Notification n;
    n.id = "300";
    n.type = Notification::Kind::Favourite;
    n.account.acct = "dave";
    n.created_at = 1719600200;
    n.status = std::make_shared<Status>(sample_inner());
    items.push_back(TimelineItem{n}); // notification row

    User u;
    u.id = "u9";
    u.acct = "erin";
    items.push_back(TimelineItem{u}); // user row

    const std::vector<TimelineItem> back = store::decode_items(store::encode_items(items));
    CHECK_EQ(back.size(), size_t(3));

    CHECK(back[0].is_status());
    CHECK_EQ(back[0].id(), std::string("s:100"));

    CHECK(back[1].is_notification());
    CHECK_EQ(back[1].id(), std::string("n:300"));
    CHECK(back[1].status() != nullptr); // resolved from the notification's post
    CHECK_EQ(back[1].sort_date(), std::int64_t(1719600200));

    CHECK(back[2].is_user());
    CHECK_EQ(back[2].id(), std::string("u:u9"));
}

void test_codec_corrupt_is_miss() {
    CHECK_EQ(store::decode_items("not a cache").size(), size_t(0));
    CHECK_EQ(store::decode_items("").size(), size_t(0));
    // Right magic but truncated body -> treated as a miss.
    std::string blob = store::encode_items({TimelineItem{sample_inner()}});
    blob.resize(blob.size() - 5);
    CHECK_EQ(store::decode_items(blob).size(), size_t(0));
}
