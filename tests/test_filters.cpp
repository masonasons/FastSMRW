#include "check.hpp"

#include <nlohmann/json.hpp>

#include "fastsm/models/models.hpp"
#include "fastsm/platform/mastodon/mastodon_map.hpp"
#include "fastsm/presentation/status_presenter.hpp"
#include "fastsm/timeline/client_filter.hpp"

using namespace fastsm;

namespace {

TimelineItem make_status(const std::string& id, const std::string& author_id) {
    Status s;
    s.id = id;
    s.account.id = author_id;
    s.text = "hello world";
    return TimelineItem{s};
}

} // namespace

void test_client_filter_post_types() {
    const std::string me = "me";

    // Original post by someone else.
    TimelineItem original = make_status("1", "other");

    // A reply to another user.
    TimelineItem reply = make_status("2", "other");
    reply.mutable_status()->in_reply_to_id = "1";
    reply.mutable_status()->in_reply_to_account_id = "stranger";

    // A self-reply (thread continuation).
    TimelineItem thread = make_status("3", "other");
    thread.mutable_status()->in_reply_to_id = "2";
    thread.mutable_status()->in_reply_to_account_id = "other"; // == author

    // A boost.
    TimelineItem boost = make_status("4", "booster");
    boost.mutable_status()->reblog = std::make_shared<Status>();
    boost.mutable_status()->reblog->id = "40";
    boost.mutable_status()->reblog->account.id = "orig";

    // Everything on by default: all pass.
    ClientFilter all;
    CHECK(client_filter_should_show(all, original, me));
    CHECK(client_filter_should_show(all, reply, me));
    CHECK(client_filter_should_show(all, thread, me));
    CHECK(client_filter_should_show(all, boost, me));

    // Hide originals.
    ClientFilter f = all;
    f.original = false;
    CHECK(!client_filter_should_show(f, original, me));
    CHECK(client_filter_should_show(f, reply, me)); // a reply is not "original"

    // Hide replies to others (thread should still pass).
    f = all;
    f.replies = false;
    CHECK(!client_filter_should_show(f, reply, me));
    CHECK(client_filter_should_show(f, thread, me));
    CHECK(client_filter_should_show(f, original, me));

    // Hide threads (self-replies).
    f = all;
    f.threads = false;
    CHECK(!client_filter_should_show(f, thread, me));
    CHECK(client_filter_should_show(f, reply, me));

    // Hide boosts.
    f = all;
    f.boosts = false;
    CHECK(!client_filter_should_show(f, boost, me));
    CHECK(client_filter_should_show(f, original, me));
}

void test_client_filter_media_and_me() {
    const std::string me = "me";

    TimelineItem with_media = make_status("1", "other");
    with_media.mutable_status()->media_attachments.push_back(MediaAttachment{});
    TimelineItem no_media = make_status("2", "other");

    ClientFilter f;
    f.media = false; // hide posts with media
    CHECK(!client_filter_should_show(f, with_media, me));
    CHECK(client_filter_should_show(f, no_media, me));

    f = ClientFilter{};
    f.no_media = false; // hide posts without media
    CHECK(client_filter_should_show(f, with_media, me));
    CHECK(!client_filter_should_show(f, no_media, me));

    // Replies to me.
    TimelineItem to_me = make_status("3", "other");
    to_me.mutable_status()->in_reply_to_id = "0";
    to_me.mutable_status()->in_reply_to_account_id = me;
    f = ClientFilter{};
    f.replies_to_me = false;
    CHECK(!client_filter_should_show(f, to_me, me));

    // My own post.
    TimelineItem mine = make_status("4", me);
    f = ClientFilter{};
    f.my_posts = false;
    CHECK(!client_filter_should_show(f, mine, me));
    CHECK(client_filter_should_show(f, to_me, me));
}

void test_client_filter_text() {
    const std::string me = "me";
    Status s;
    s.id = "1";
    s.account.id = "other";
    s.account.display_name = "Alice";
    s.account.acct = "alice@example.social";
    s.text = "The quick brown fox";
    TimelineItem item{s};

    ClientFilter f;
    f.text = "brown"; // matches post text (case-insensitive)
    CHECK(client_filter_should_show(f, item, me));
    f.text = "BROWN";
    CHECK(client_filter_should_show(f, item, me));
    f.text = "alice"; // matches display name / handle
    CHECK(client_filter_should_show(f, item, me));
    f.text = "zebra"; // no match -> hidden
    CHECK(!client_filter_should_show(f, item, me));

    // An all-pass filter (default flags, empty text) is inactive.
    CHECK(!ClientFilter{}.is_active());
    ClientFilter active;
    active.boosts = false;
    CHECK(active.is_active());
}

void test_server_filter_metadata() {
    using nlohmann::json;
    // A status the server tagged with a "hide" filter.
    json j = {
        {"id", "9"},
        {"content", "<p>spoiler talk</p>"},
        {"account", {{"id", "a"}, {"acct", "a@host"}, {"username", "a"}}},
        {"created_at", "2023-01-01T00:00:00.000Z"},
        {"filtered", json::array({{{"filter", {{"title", "Spoilers"}, {"filter_action", "hide"}}},
                                   {"keyword_matches", json::array({"spoiler"})}}})},
    };
    Status s = mastodon::map_status(j);
    CHECK_EQ(s.filtered.size(), static_cast<size_t>(1));
    CHECK_EQ(s.filtered[0].title, std::string("Spoilers"));
    CHECK(s.filtered[0].hide);
    CHECK(s.any_filter_hides());

    // A "warn" filter: not hidden, but the spoken label gets a "Filtered:" prefix.
    Status w;
    w.id = "10";
    w.text = "body";
    w.filtered.push_back(StatusFilterMatch{"Politics", false});
    CHECK(!w.any_filter_hides());
    const std::string label = present::accessibility_label(w, 0);
    CHECK(label.rfind("Filtered: Politics", 0) == 0); // starts with the prefix
}
