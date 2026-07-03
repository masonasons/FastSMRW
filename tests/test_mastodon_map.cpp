#include "check.hpp"

#include <nlohmann/json.hpp>

#include "fastsm/platform/mastodon/mastodon_map.hpp"
#include "fastsm/timeline/timeline_source.hpp"
#include "fastsm/util/url.hpp"

using namespace fastsm;
using nlohmann::json;

// A boost of a post with a CW, one image, and a poll — a representative
// Mastodon timeline entry.
static const char* kSampleBoost = R"JSON({
  "id": "boost1",
  "created_at": "2024-06-28T12:05:00.000Z",
  "reblogged": false,
  "favourited": false,
  "content": "",
  "account": {
    "id": "200", "acct": "carol@example.social", "username": "carol",
    "display_name": "Carol", "followers_count": 10, "statuses_count": 5
  },
  "reblog": {
    "id": "100",
    "created_at": "2024-06-28T12:00:00.000Z",
    "content": "<p>hello &amp; <a href=\"https://x\">welcome</a></p>",
    "spoiler_text": "cw text",
    "visibility": "unlisted",
    "favourites_count": 3,
    "reblogs_count": 1,
    "replies_count": 2,
    "favourited": true,
    "reblogged": false,
    "in_reply_to_id": "99",
    "application": { "name": "FastSMRW" },
    "account": {
      "id": "300", "acct": "alice@example.social", "username": "alice",
      "display_name": "Alice"
    },
    "media_attachments": [
      { "id": "m1", "type": "image", "url": "https://img/x.png", "description": "a cat" }
    ],
    "tags": [ {"name": "cats", "url": "https://x/tags/cats"}, {"name": "welcome"} ],
    "poll": {
      "id": "p1", "multiple": true, "votes_count": 3,
      "options": [ {"title": "yes", "votes_count": 2}, {"title": "no", "votes_count": 1} ]
    }
  }
})JSON";

void test_mastodon_status_mapping() {
    const Status s = mastodon::map_status(json::parse(kSampleBoost));

    CHECK_EQ(s.id, std::string("boost1"));
    CHECK(s.platform == Platform::Mastodon);
    CHECK(s.is_boost());
    CHECK_EQ(s.account.display_name, std::string("Carol"));
    CHECK(s.created_at > 0);

    const Status& inner = s.display_status();
    CHECK_EQ(inner.id, std::string("100"));
    CHECK_EQ(inner.text, std::string("hello & welcome")); // HTML stripped + entity decoded
    CHECK(inner.has_content_warning());
    CHECK_EQ(inner.spoiler_text.value(), std::string("cw text"));
    CHECK(inner.visibility.value() == Visibility::Unlisted);
    CHECK_EQ(inner.favourites_count, 3);
    CHECK_EQ(inner.boosts_count, 1); // mapped from reblogs_count
    CHECK(inner.favourited);
    CHECK(inner.in_reply_to_id.has_value());
    CHECK_EQ(inner.in_reply_to_id.value(), std::string("99"));
    CHECK_EQ(inner.application_name.value(), std::string("FastSMRW"));
    CHECK_EQ(inner.account.display_name, std::string("Alice"));
    CHECK_EQ(inner.media_attachments.size(), size_t(1));
    CHECK(inner.media_attachments[0].type == MediaAttachment::Kind::Image);
    CHECK_EQ(inner.media_attachments[0].description, std::string("a cat"));
    CHECK_EQ(inner.tags.size(), size_t(2)); // hashtag names parsed from the tags array
    CHECK_EQ(inner.tags[0], std::string("cats"));
    CHECK_EQ(inner.tags[1], std::string("welcome"));
    CHECK(inner.poll.has_value());
    CHECK(inner.poll->multiple);
    CHECK_EQ(inner.poll->options.size(), size_t(2));
    CHECK_EQ(inner.poll->options[0].title, std::string("yes"));
}

void test_mastodon_notification_mapping() {
    const char* kNotif = R"JSON({
      "id": "n1", "type": "favourite",
      "created_at": "2024-06-28T12:10:00.000Z",
      "account": { "id": "300", "acct": "alice", "username": "alice", "display_name": "Alice" },
      "status": { "id": "100", "content": "<p>hi</p>", "account": {"id":"1","acct":"me"} }
    })JSON";
    const Notification n = mastodon::map_notification(json::parse(kNotif));
    CHECK_EQ(n.id, std::string("n1"));
    CHECK(n.type == Notification::Kind::Favourite);
    CHECK_EQ(n.account.display_name, std::string("Alice"));
    CHECK(n.status != nullptr);
    CHECK_EQ(n.status->text, std::string("hi"));
}

void test_mastodon_quote_mapping() {
    // Mastodon 4.4 wraps the quote as { state, quoted_status: Status }.
    const char* kQuote = R"JSON({
      "id": "q1", "content": "<p>check this out</p>",
      "created_at": "2024-06-28T12:00:00.000Z",
      "account": {"id":"1","acct":"me","username":"me"},
      "quote": {
        "state": "accepted",
        "quoted_status": {
          "id": "700", "content": "<p>original post</p>",
          "created_at": "2024-06-28T11:00:00.000Z",
          "account": {"id":"2","acct":"alice","username":"alice","display_name":"Alice"}
        }
      }
    })JSON";
    const Status s = mastodon::map_status(json::parse(kQuote));
    CHECK(s.quote != nullptr);
    if (s.quote) {
        CHECK_EQ(s.quote->id, std::string("700"));
        CHECK_EQ(s.quote->text, std::string("original post")); // HTML stripped
        CHECK_EQ(s.quote->account.display_name, std::string("Alice"));
    }

    // A pending/rejected quote has no quoted_status -> no quote attached.
    const char* kPending = R"JSON({
      "id": "q2", "content": "<p>hi</p>", "created_at": "2024-06-28T12:00:00.000Z",
      "account": {"id":"1","acct":"me"}, "quote": { "state": "pending" }
    })JSON";
    CHECK(mastodon::map_status(json::parse(kPending)).quote == nullptr);
}

void test_mark_remote() {
    // A local-looking author (bare acct) and a missing URL, plus a boost of a
    // post that already carries a full URL.
    const char* kRemote = R"JSON({
      "id": "500", "created_at": "2024-06-28T12:00:00.000Z", "content": "<p>hi</p>",
      "url": "",
      "account": { "id": "1", "acct": "bob", "username": "bob", "display_name": "Bob" },
      "reblog": {
        "id": "600", "created_at": "2024-06-28T11:00:00.000Z", "content": "<p>orig</p>",
        "url": "https://other.social/@dana/600",
        "account": { "id": "2", "acct": "dana@other.social", "username": "dana" }
      }
    })JSON";
    Status s = mastodon::map_status(json::parse(kRemote));
    mastodon::mark_remote(s, "https://mastodon.social", "mastodon.social");

    CHECK(s.instance_url.has_value());
    CHECK_EQ(s.instance_url.value(), std::string("https://mastodon.social"));
    // A bare local handle gains the instance domain.
    CHECK_EQ(s.account.acct, std::string("bob@mastodon.social"));
    // A missing URL is synthesized from the instance + username + id.
    CHECK_EQ(s.url, std::string("https://mastodon.social/@bob/500"));
    // The boosted inner post is tagged too, but keeps its own real URL and its
    // already-qualified handle.
    CHECK(s.reblog->instance_url.has_value());
    CHECK_EQ(s.reblog->url, std::string("https://other.social/@dana/600"));
    CHECK_EQ(s.reblog->account.acct, std::string("dana@other.social"));
}

void test_remote_timeline_source() {
    const auto local = TimelineSource::remote_local("mastodon.social");
    CHECK(local.kind == TimelineSource::Kind::RemoteLocal);
    CHECK_EQ(local.title(), std::string("mastodon.social (Local)"));
    CHECK_EQ(local.cache_key(), std::string("remoteLocal:mastodon.social"));
    CHECK(local.is_cacheable()); // every timeline now caches (unique per account+key)
    CHECK(local.is_dismissable());
    CHECK(local.paginates_by_item_id());
    CHECK(!local.is_user_list());
    CHECK(local.new_items_sound_name().value() == "home");

    const auto user = TimelineSource::remote_user("dana@other.social");
    CHECK(user.kind == TimelineSource::Kind::RemoteUser);
    CHECK_EQ(user.title(), std::string("@dana@other.social"));
    CHECK_EQ(user.cache_key(), std::string("remoteUser:dana@other.social"));
    CHECK(user.is_cacheable());
    CHECK(user.paginates_by_item_id());
}

void test_form_encode() {
    std::vector<std::pair<std::string, std::string>> p = {{"status", "hi there & you"},
                                                          {"visibility", "public"}};
    CHECK_EQ(fastsm::util::form_encode(p),
             std::string("status=hi%20there%20%26%20you&visibility=public"));
}
