#include "check.hpp"

#include <nlohmann/json.hpp>

#include "fastsm/platform/mastodon/mastodon_map.hpp"
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

void test_form_encode() {
    std::vector<std::pair<std::string, std::string>> p = {{"status", "hi there & you"},
                                                          {"visibility", "public"}};
    CHECK_EQ(fastsm::util::form_encode(p),
             std::string("status=hi%20there%20%26%20you&visibility=public"));
}
