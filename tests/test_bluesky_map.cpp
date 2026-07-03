#include "check.hpp"

#include <nlohmann/json.hpp>

#include "fastsm/platform/bluesky/bluesky_map.hpp"

using namespace fastsm;
using nlohmann::json;

// A reposted post with an image embed and the viewer's own like.
static const char* kFeedItem = R"JSON({
  "post": {
    "uri": "at://did:plc:author/app.bsky.feed.post/abc",
    "cid": "bafyabc",
    "author": {
      "did": "did:plc:author", "handle": "alice.bsky.social", "displayName": "Alice"
    },
    "record": {
      "$type": "app.bsky.feed.post",
      "text": "hello bluesky",
      "createdAt": "2024-06-28T12:00:00.000Z",
      "reply": { "parent": { "uri": "at://did:plc:other/app.bsky.feed.post/parent" } }
    },
    "replyCount": 2, "repostCount": 5, "likeCount": 9,
    "viewer": { "like": "at://did:plc:me/app.bsky.feed.like/like1" },
    "embed": {
      "$type": "app.bsky.embed.images#view",
      "images": [ { "thumb": "https://t/x.jpg", "fullsize": "https://f/x.jpg", "alt": "a view" } ]
    }
  },
  "reason": {
    "$type": "app.bsky.feed.defs#reasonRepost",
    "by": { "did": "did:plc:carol", "handle": "carol.bsky.social", "displayName": "Carol" },
    "indexedAt": "2024-06-28T12:30:00.000Z"
  }
})JSON";

void test_bluesky_feed_mapping() {
    const Status s = bluesky::map_feed_item(json::parse(kFeedItem));

    // Outer row is a boost authored by the reposter.
    CHECK(s.platform == Platform::Bluesky);
    CHECK(s.is_boost());
    CHECK_EQ(s.account.display_name, std::string("Carol"));

    const Status& inner = s.display_status();
    CHECK_EQ(inner.id, std::string("at://did:plc:author/app.bsky.feed.post/abc"));
    CHECK_EQ(inner.cid.value(), std::string("bafyabc"));
    CHECK_EQ(inner.text, std::string("hello bluesky"));
    CHECK_EQ(inner.account.acct, std::string("alice.bsky.social"));
    CHECK_EQ(inner.favourites_count, 9); // likeCount
    CHECK_EQ(inner.boosts_count, 5);     // repostCount
    CHECK_EQ(inner.replies_count, 2);
    CHECK(inner.favourited);
    CHECK(inner.like_uri.has_value());
    CHECK_EQ(inner.like_uri.value(), std::string("at://did:plc:me/app.bsky.feed.like/like1"));
    CHECK(inner.in_reply_to_id.has_value());
    CHECK_EQ(inner.media_attachments.size(), size_t(1));
    CHECK(inner.media_attachments[0].type == MediaAttachment::Kind::Image);
    CHECK_EQ(inner.media_attachments[0].description, std::string("a view"));
}

void test_bluesky_notification_mapping() {
    // A like notification carries the actor but no attached post.
    const char* kLike = R"JSON({
      "uri": "at://did:plc:me/app.bsky.feed.like/n1", "cid": "c1",
      "author": { "did": "did:plc:dana", "handle": "dana.test", "displayName": "Dana" },
      "reason": "like",
      "reasonSubject": "at://did:plc:me/app.bsky.feed.post/mine",
      "indexedAt": "2024-06-28T12:00:00.000Z"
    })JSON";
    const Notification like = bluesky::map_notification(json::parse(kLike));
    CHECK(like.platform == Platform::Bluesky);
    CHECK(like.type == Notification::Kind::Favourite);
    CHECK_EQ(like.account.display_name, std::string("Dana"));
    CHECK(like.status == nullptr); // like/repost carry no incoming post

    // A reply notification reads as a Mention and carries the incoming post text.
    const char* kReply = R"JSON({
      "uri": "at://did:plc:eve/app.bsky.feed.post/r1", "cid": "c2",
      "author": { "did": "did:plc:eve", "handle": "eve.test", "displayName": "Eve" },
      "reason": "reply",
      "record": { "text": "@me hi there", "createdAt": "2024-06-28T13:00:00.000Z" },
      "indexedAt": "2024-06-28T13:00:01.000Z"
    })JSON";
    const Notification reply = bluesky::map_notification(json::parse(kReply));
    CHECK(reply.type == Notification::Kind::Mention); // reply/quote read as mention
    CHECK(reply.status != nullptr);
    if (reply.status) {
        CHECK_EQ(reply.status->text, std::string("@me hi there"));
        CHECK_EQ(reply.status->id, std::string("at://did:plc:eve/app.bsky.feed.post/r1"));
    }
}

void test_bluesky_plain_post() {
    const char* kPlain = R"JSON({
      "post": {
        "uri": "at://did:plc:x/app.bsky.feed.post/1", "cid": "cid1",
        "author": { "did": "did:plc:x", "handle": "bob.test", "displayName": "Bob" },
        "record": { "text": "just a post", "createdAt": "2024-06-28T10:00:00Z" },
        "likeCount": 0, "repostCount": 0, "replyCount": 0
      }
    })JSON";
    const Status s = bluesky::map_feed_item(json::parse(kPlain));
    CHECK(!s.is_boost());
    CHECK_EQ(s.text, std::string("just a post"));
    CHECK(!s.favourited);
    CHECK(!s.like_uri.has_value());
}
