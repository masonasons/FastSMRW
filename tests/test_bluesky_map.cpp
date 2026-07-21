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

void test_bluesky_facet_mapping() {
    // A post whose record carries mention + tag facets populates mentions/tags.
    const char* kFaceted = R"JSON({
      "post": {
        "uri": "at://did:plc:x/app.bsky.feed.post/2", "cid": "cid2",
        "author": { "did": "did:plc:x", "handle": "bob.test", "displayName": "Bob" },
        "record": {
          "text": "hi @alice.bsky.social #a11y",
          "createdAt": "2024-06-28T10:00:00Z",
          "facets": [
            { "index": { "byteStart": 3, "byteEnd": 21 },
              "features": [ { "$type": "app.bsky.richtext.facet#mention", "did": "did:plc:alice" } ] },
            { "index": { "byteStart": 22, "byteEnd": 27 },
              "features": [ { "$type": "app.bsky.richtext.facet#tag", "tag": "a11y" } ] }
          ]
        },
        "likeCount": 0, "repostCount": 0, "replyCount": 0
      }
    })JSON";
    const Status s = bluesky::map_feed_item(json::parse(kFaceted));
    CHECK_EQ(s.mentions.size(), size_t(1));
    if (!s.mentions.empty()) {
        CHECK_EQ(s.mentions[0].id, std::string("did:plc:alice"));
        CHECK_EQ(s.mentions[0].acct, std::string("alice.bsky.social")); // '@' stripped from the slice
    }
    CHECK_EQ(s.tags.size(), size_t(1));
    if (!s.tags.empty())
        CHECK_EQ(s.tags[0], std::string("a11y"));
}

void test_bluesky_embed_external_and_video() {
    // An external link-card view maps to a Card.
    const char* kExternal = R"JSON({
      "post": {
        "uri": "at://did:plc:x/app.bsky.feed.post/e", "cid": "cide",
        "author": { "did": "did:plc:x", "handle": "bob.test" },
        "record": { "text": "look", "createdAt": "2024-06-28T10:00:00Z" },
        "embed": {
          "$type": "app.bsky.embed.external#view",
          "external": { "uri": "https://ex.com/a", "title": "A title",
                        "description": "desc", "thumb": "https://ex.com/t.jpg" }
        }
      }
    })JSON";
    const Status e = bluesky::map_feed_item(json::parse(kExternal));
    CHECK(e.card.has_value());
    if (e.card) {
        CHECK_EQ(e.card->url, std::string("https://ex.com/a"));
        CHECK_EQ(e.card->title, std::string("A title"));
    }

    // A video view maps to a Video attachment (playlist + thumbnail + alt).
    const char* kVideo = R"JSON({
      "post": {
        "uri": "at://did:plc:x/app.bsky.feed.post/v", "cid": "cidv",
        "author": { "did": "did:plc:x", "handle": "bob.test" },
        "record": { "text": "clip", "createdAt": "2024-06-28T10:00:00Z" },
        "embed": {
          "$type": "app.bsky.embed.video#view",
          "playlist": "https://v/hls.m3u8", "thumbnail": "https://v/t.jpg", "alt": "a clip"
        }
      }
    })JSON";
    const Status v = bluesky::map_feed_item(json::parse(kVideo));
    CHECK_EQ(v.media_attachments.size(), size_t(1));
    if (!v.media_attachments.empty()) {
        CHECK(v.media_attachments[0].type == MediaAttachment::Kind::Video);
        CHECK_EQ(v.media_attachments[0].url, std::string("https://v/hls.m3u8"));
        CHECK_EQ(v.media_attachments[0].description, std::string("a clip"));
    }
}

void test_bluesky_embed_record_with_media() {
    // recordWithMedia carries both a quoted post and media.
    const char* kRWM = R"JSON({
      "post": {
        "uri": "at://did:plc:x/app.bsky.feed.post/rwm", "cid": "cidr",
        "author": { "did": "did:plc:x", "handle": "bob.test" },
        "record": { "text": "quoting with a pic", "createdAt": "2024-06-28T10:00:00Z" },
        "embed": {
          "$type": "app.bsky.embed.recordWithMedia#view",
          "media": {
            "$type": "app.bsky.embed.images#view",
            "images": [ { "thumb": "https://t/x.jpg", "fullsize": "https://f/x.jpg", "alt": "pic" } ]
          },
          "record": {
            "record": {
              "$type": "app.bsky.embed.record#viewRecord",
              "uri": "at://did:plc:q/app.bsky.feed.post/q1", "cid": "qcid",
              "author": { "did": "did:plc:q", "handle": "quoted.test", "displayName": "Quoted" },
              "value": { "text": "the quoted text" }
            }
          }
        }
      }
    })JSON";
    const Status s = bluesky::map_feed_item(json::parse(kRWM));
    CHECK_EQ(s.media_attachments.size(), size_t(1));
    CHECK(s.quote != nullptr);
    if (s.quote) {
        CHECK_EQ(s.quote->text, std::string("the quoted text"));
        CHECK_EQ(s.quote->account.display_name, std::string("Quoted"));
    }
}

void test_bluesky_labels_content_warning() {
    // A sensitive moderation label surfaces as a content warning.
    const char* kLabeled = R"JSON({
      "post": {
        "uri": "at://did:plc:x/app.bsky.feed.post/l", "cid": "cidl",
        "author": { "did": "did:plc:x", "handle": "bob.test" },
        "record": { "text": "nsfw", "createdAt": "2024-06-28T10:00:00Z" },
        "labels": [ { "val": "porn" } ]
      }
    })JSON";
    const Status s = bluesky::map_feed_item(json::parse(kLabeled));
    CHECK(s.has_content_warning());
    if (s.spoiler_text)
        CHECK(s.spoiler_text->find("porn") != std::string::npos);

    // A self-label in the record also counts.
    const char* kSelf = R"JSON({
      "post": {
        "uri": "at://did:plc:x/app.bsky.feed.post/l2", "cid": "cidl2",
        "author": { "did": "did:plc:x", "handle": "bob.test" },
        "record": { "text": "x", "createdAt": "2024-06-28T10:00:00Z",
                    "labels": { "$type": "com.atproto.label.defs#selfLabels",
                                "values": [ { "val": "nudity" } ] } }
      }
    })JSON";
    CHECK(bluesky::map_feed_item(json::parse(kSelf)).has_content_warning());

    // A non-sensitive label (e.g. spam) does NOT hide content behind a CW.
    const char* kSpam = R"JSON({
      "post": {
        "uri": "at://did:plc:x/app.bsky.feed.post/l3", "cid": "cidl3",
        "author": { "did": "did:plc:x", "handle": "bob.test" },
        "record": { "text": "y", "createdAt": "2024-06-28T10:00:00Z" },
        "labels": [ { "val": "spam" } ]
      }
    })JSON";
    CHECK(!bluesky::map_feed_item(json::parse(kSpam)).has_content_warning());
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
