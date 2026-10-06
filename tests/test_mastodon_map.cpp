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

void test_mastodon_notification_group_mapping() {
    // One entry of /api/v2/notifications' notification_groups, with side-loaded
    // accounts + statuses resolved by id.
    // most_recent_notification_id is a JSON *number* in the real API, so the string
    // helper yields nothing for it — the row identity must come from group_key, or
    // every group collapses onto one row. `id` instead carries page_min_id, a real
    // notification id, because that's the id space this feed paginates in.
    const char* kGroup = R"JSON({
      "group_key": "favourite-100",
      "notifications_count": 5,
      "type": "favourite",
      "most_recent_notification_id": 196014,
      "page_min_id": "195980",
      "page_max_id": "196014",
      "latest_page_notification_at": "2024-06-28T12:10:00.000Z",
      "sample_account_ids": ["300", "301"],
      "status_id": "100"
    })JSON";
    const json accts = json::parse(
        R"JSON([{"id":"300","acct":"alice","username":"alice","display_name":"Alice"},
                {"id":"301","acct":"bob","username":"bob","display_name":"Bob"}])JSON");
    const json stats =
        json::parse(R"JSON([{"id":"100","content":"<p>hi</p>","account":{"id":"1","acct":"me"}}])JSON");
    std::unordered_map<std::string, const json*> amap, smap;
    for (const auto& a : accts)
        amap[a.value("id", std::string{})] = &a;
    for (const auto& s : stats)
        smap[s.value("id", std::string{})] = &s;

    const Notification n = mastodon::map_notification_group(json::parse(kGroup), amap, smap);
    CHECK_EQ(n.id, std::string("195980")); // page_min_id: pages below this group
    CHECK_EQ(n.group_key, std::string("favourite-100"));
    // The row's identity is the group key, so a refresh (or a streamed notification,
    // which numbers the same group differently) updates the row in place instead of
    // renaming it out from under the reading position.
    CHECK_EQ(TimelineItem{n}.id(), std::string("n:favourite-100"));
    CHECK_EQ(TimelineItem{n}.pagination_id(), std::string("195980"));
    CHECK(n.type == Notification::Kind::Favourite);
    CHECK_EQ(n.notifications_count, 5);
    CHECK_EQ(n.account.display_name, std::string("Alice")); // first sample = most recent actor
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

void test_mastodon_notification_request_mapping() {
    // One entry of /api/v1/notifications/requests. The row is the requesting account,
    // but accept/dismiss act on the request's own id, so that has to survive mapping --
    // sending the account id to those endpoints would address the wrong object.
    // notifications_count is documented as a string; some servers send a number, so
    // both are accepted.
    const char* kAsString = R"JSON({
      "id": "4321",
      "created_at": "2026-09-30T10:00:00.000Z",
      "notifications_count": "7",
      "account": {"id": "900", "acct": "stranger@example.social",
                  "username": "stranger", "display_name": "A Stranger"}
    })JSON";
    const User u = mastodon::map_notification_request(json::parse(kAsString));
    CHECK_EQ(u.id, std::string("900")); // the account, for opening their profile
    CHECK_EQ(u.notification_request_id, std::string("4321")); // what accept/dismiss uses
    CHECK_EQ(u.pending_notifications, 7);
    CHECK_EQ(u.acct, std::string("stranger@example.social"));
    // The row keys off the account, so two requests from one person can't collide.
    CHECK_EQ(TimelineItem{u}.id(), std::string("u:900"));

    const char* kAsNumber = R"JSON({
      "id": "4322", "notifications_count": 2,
      "account": {"id": "901", "acct": "other", "username": "other"}
    })JSON";
    CHECK_EQ(mastodon::map_notification_request(json::parse(kAsNumber)).pending_notifications, 2);

    // A malformed entry must not throw out of the mapper: a missing account yields an
    // empty user rather than taking the whole timeline down.
    const User none = mastodon::map_notification_request(json::parse(R"JSON({"id":"5"})JSON"));
    CHECK_EQ(none.notification_request_id, std::string("5"));
    CHECK(none.id.empty());
    CHECK_EQ(none.pending_notifications, 0);
}

void test_notification_requests_source() {
    const TimelineSource src = TimelineSource::notification_requests();
    CHECK_EQ(src.cache_key(), std::string("notificationRequests"));
    CHECK_EQ(src.title(), std::string("Message Requests"));
    CHECK(src.is_user_list());      // rows are people: multi-select + batch actions
    CHECK(src.is_dismissable());    // it's a spawned buffer, so Delete closes it
    CHECK(!src.is_static());        // fetched and paged, unlike the analysis lists
    CHECK(!src.paginates_by_item_id()); // pages via the Link header
    CHECK(!src.new_items_sound_name().has_value()); // not a streaming feed
}

void test_directory_and_suggestions_sources() {
    const TimelineSource dir = TimelineSource::directory();
    CHECK_EQ(dir.cache_key(), std::string("directory"));
    CHECK_EQ(dir.title(), std::string("Profile Directory"));
    CHECK(dir.is_user_list());   // rows are people: the user actions apply
    CHECK(dir.is_dismissable()); // spawned, so Delete closes it
    CHECK(!dir.is_static());     // fetched and paged, unlike the analysis lists
    CHECK(!dir.new_items_sound_name().has_value()); // not a streaming feed

    const TimelineSource sug = TimelineSource::suggestions();
    CHECK_EQ(sug.cache_key(), std::string("suggestions"));
    CHECK_EQ(sug.title(), std::string("Suggested Follows"));
    CHECK(sug.is_user_list());
    CHECK(sug.is_dismissable());
    CHECK(!sug.new_items_sound_name().has_value());

    // Both are user lists, so neither re-sorts newest-first on merge: the server's
    // order IS the ranking, and re-sorting would throw away the suggestion order.
    CHECK(!dir.is_time_ordered());
    CHECK(!sug.is_time_ordered());
}

void test_suggestions_row_unwrapping() {
    // /api/v2/suggestions wraps each account: {source, account}. Reading the row as a
    // bare account yields a blank user, so the unwrap is what makes the list usable.
    const char* kEntry = R"JSON({
      "source": "staff",
      "account": {"id": "77", "acct": "amy@example.social", "username": "amy",
                  "display_name": "Amy"}
    })JSON";
    const json j = json::parse(kEntry);
    const auto acc = j.find("account");
    CHECK(acc != j.end());
    const User u = mastodon::map_user(*acc);
    CHECK_EQ(u.id, std::string("77"));
    CHECK_EQ(u.display_name, std::string("Amy"));
    // The wrapper's own keys must not be mistaken for the account's.
    CHECK(mastodon::map_user(j).id.empty());
}
