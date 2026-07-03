#include "fastsm/platform/bluesky/bluesky_account.hpp"

#include <nlohmann/json.hpp>

#include "fastsm/platform/bluesky/bluesky_map.hpp"
#include "fastsm/util/date_parsing.hpp"
#include "fastsm/util/url.hpp"

using nlohmann::json;

namespace fastsm {
namespace {

std::string rkey_from_uri(const std::string& at_uri) {
    const size_t slash = at_uri.find_last_of('/');
    return slash == std::string::npos ? at_uri : at_uri.substr(slash + 1);
}

// Flatten a getPostThread replies tree into rows, depth-first.
void append_bsky_replies(const json& replies, std::vector<TimelineItem>& out) {
    if (!replies.is_array())
        return;
    for (const auto& node : replies) {
        if (auto p = node.find("post"); p != node.end() && p->is_object())
            out.push_back(TimelineItem{bluesky::map_post(*p)});
        if (auto r = node.find("replies"); r != node.end())
            append_bsky_replies(*r, out);
    }
}

} // namespace

BlueskyAccount::BlueskyAccount(BlueskyCredentials credentials, BlueskySession session, User me,
                               net::IHttpClient* http)
    : credentials_(std::move(credentials)), session_(std::move(session)), me_(std::move(me)),
      http_(http) {}

PlatformFeatures BlueskyAccount::features() const {
    PlatformFeatures f;
    f.quote_posts = true;
    return f;
}

std::vector<TimelineSource> BlueskyAccount::default_timelines() const {
    return {TimelineSource::home(), TimelineSource::notifications()};
}

std::vector<TimelineSource> BlueskyAccount::spawnable_timelines() const {
    return {TimelineSource::mentions()};
}

bool BlueskyAccount::refresh_session() {
    net::HttpRequest req;
    req.method = "POST";
    req.url = session_.pds_url + "/xrpc/com.atproto.server.refreshSession";
    req.headers.push_back({"Authorization", "Bearer " + session_.refresh_jwt});
    const net::HttpResponse res = http_->send(req);
    if (!res.ok())
        return false;
    try {
        const json j = json::parse(res.body);
        session_.access_jwt = j.value("accessJwt", session_.access_jwt);
        session_.refresh_jwt = j.value("refreshJwt", session_.refresh_jwt);
        return true;
    } catch (...) {
        return false;
    }
}

net::HttpResponse BlueskyAccount::send_authed(const std::string& method, const std::string& url,
                                              const std::string& json_body) {
    auto build = [&](const std::string& token) {
        net::HttpRequest req;
        req.method = method;
        req.url = url;
        req.headers.push_back({"Authorization", "Bearer " + token});
        if (!json_body.empty()) {
            req.headers.push_back({"Content-Type", "application/json"});
            req.body = json_body;
        }
        return req;
    };

    net::HttpResponse res = http_->send(build(session_.access_jwt));
    const bool expired = res.status == 401 ||
                         (res.status == 400 && res.body.find("ExpiredToken") != std::string::npos);
    if (expired && refresh_session())
        res = http_->send(build(session_.access_jwt));
    return res;
}

TimelinePage BlueskyAccount::items(const TimelineSource& source, int limit,
                                   const PageCursor& cursor) {
    TimelinePage page;
    const std::string base = session_.pds_url + "/xrpc/";
    const std::string cur = (cursor.kind == CursorKind::Token && !cursor.value.empty())
                                ? "&cursor=" + util::percent_encode(cursor.value)
                                : std::string{};
    auto fetch = [&](const std::string& url) -> std::optional<json> {
        const net::HttpResponse res = send_authed("GET", url, "");
        if (!res.ok())
            return std::nullopt;
        try {
            return json::parse(res.body);
        } catch (...) {
            return std::nullopt;
        }
    };
    auto take_feed = [&](const json& j) {
        if (auto f = j.find("feed"); f != j.end() && f->is_array())
            for (const auto& e : *f)
                page.items.push_back(TimelineItem{bluesky::map_feed_item(e)});
        if (auto c = j.find("cursor"); c != j.end() && c->is_string())
            page.next_cursor = PageCursor::token(c->get<std::string>());
    };

    switch (source.kind) {
    case TimelineSource::Kind::Home:
        if (auto j = fetch(base + "app.bsky.feed.getTimeline?limit=" + std::to_string(limit) + cur))
            take_feed(*j);
        break;
    case TimelineSource::Kind::UserPosts:
        if (auto j = fetch(base + "app.bsky.feed.getAuthorFeed?actor=" +
                           util::percent_encode(source.param) + "&limit=" + std::to_string(limit) +
                           cur))
            take_feed(*j);
        break;
    case TimelineSource::Kind::Thread: {
        auto j = fetch(base + "app.bsky.feed.getPostThread?uri=" +
                       util::percent_encode(source.param));
        if (!j)
            break;
        auto t = j->find("thread");
        if (t == j->end() || !t->is_object())
            break;
        // Ancestors: walk the parent chain, then reverse into chronological order.
        std::vector<TimelineItem> ancestors;
        const json* node = nullptr;
        if (auto pit = t->find("parent"); pit != t->end() && pit->is_object())
            node = &(*pit);
        while (node) {
            if (auto p = node->find("post"); p != node->end() && p->is_object())
                ancestors.push_back(TimelineItem{bluesky::map_post(*p)});
            const json* next = nullptr;
            if (auto pit = node->find("parent"); pit != node->end() && pit->is_object())
                next = &(*pit);
            node = next;
        }
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
            page.items.push_back(std::move(*it));
        if (auto p = t->find("post"); p != t->end() && p->is_object())
            page.items.push_back(TimelineItem{bluesky::map_post(*p)});
        if (auto r = t->find("replies"); r != t->end())
            append_bsky_replies(*r, page.items);
        break;
    }
    case TimelineSource::Kind::Followers:
    case TimelineSource::Kind::Following: {
        const bool followers = source.kind == TimelineSource::Kind::Followers;
        const std::string ep =
            followers ? "app.bsky.graph.getFollowers" : "app.bsky.graph.getFollows";
        const char* key = followers ? "followers" : "follows";
        if (auto j = fetch(base + ep + "?actor=" + util::percent_encode(source.param) +
                           "&limit=" + std::to_string(limit) + cur)) {
            if (auto a = j->find(key); a != j->end() && a->is_array())
                for (const auto& prof : *a)
                    page.items.push_back(TimelineItem{bluesky::map_author(prof)});
            if (auto c = j->find("cursor"); c != j->end() && c->is_string())
                page.next_cursor = PageCursor::token(c->get<std::string>());
        }
        break;
    }
    case TimelineSource::Kind::Notifications: {
        // Every notification, including mention/reply/quote (as Mention rows) so
        // the "show mentions in notifications" setting can filter them client-side.
        if (auto j = fetch(base + "app.bsky.notification.listNotifications?limit=" +
                           std::to_string(limit) + cur)) {
            if (auto a = j->find("notifications"); a != j->end() && a->is_array())
                for (const auto& e : *a)
                    page.items.push_back(TimelineItem{bluesky::map_notification(e)});
            if (auto c = j->find("cursor"); c != j->end() && c->is_string())
                page.next_cursor = PageCursor::token(c->get<std::string>());
        }
        break;
    }
    case TimelineSource::Kind::Mentions: {
        // Keep only mention/reply/quote notifications, then hydrate the actual
        // posts via getPosts so a @-mention shows as the post itself (Mastodon
        // parity). getPosts takes at most 25 uris per call, so batch them.
        auto j = fetch(base + "app.bsky.notification.listNotifications?limit=" +
                       std::to_string(limit) + cur);
        if (!j)
            break;
        std::vector<std::string> uris;
        if (auto a = j->find("notifications"); a != j->end() && a->is_array())
            for (const auto& e : *a) {
                const std::string reason = e.value("reason", std::string{});
                if (reason == "mention" || reason == "reply" || reason == "quote")
                    if (std::string uri = e.value("uri", std::string{}); !uri.empty())
                        uris.push_back(std::move(uri));
            }
        if (auto c = j->find("cursor"); c != j->end() && c->is_string())
            page.next_cursor = PageCursor::token(c->get<std::string>());
        for (size_t i = 0; i < uris.size(); i += 25) {
            std::string q;
            for (size_t k = i; k < i + 25 && k < uris.size(); ++k)
                q += "&uris=" + util::percent_encode(uris[k]);
            if (q.empty())
                continue;
            if (auto pj = fetch(base + "app.bsky.feed.getPosts?" + q.substr(1)))
                if (auto pa = pj->find("posts"); pa != pj->end() && pa->is_array())
                    for (const auto& p : *pa)
                        page.items.push_back(TimelineItem{bluesky::map_post(p)});
        }
        break;
    }
    default:
        break; // other kinds aren't supported on Bluesky yet
    }
    return page;
}

std::optional<Status> BlueskyAccount::post(const PostDraft& draft) {
    json record;
    record["$type"] = "app.bsky.feed.post";
    record["text"] = draft.text;
    record["createdAt"] = util::format_iso8601(util::now_unix());
    if (draft.language)
        record["langs"] = json::array({*draft.language});
    // Quote post: embed a strong reference (uri + cid) to the quoted post.
    if (draft.quoted_status_id && !draft.quoted_status_id->empty() && draft.quoted_status_cid &&
        !draft.quoted_status_cid->empty()) {
        record["embed"] = {{"$type", "app.bsky.embed.record"},
                           {"record",
                            {{"uri", *draft.quoted_status_id}, {"cid", *draft.quoted_status_cid}}}};
    }

    json body;
    body["repo"] = credentials_.did;
    body["collection"] = "app.bsky.feed.post";
    body["record"] = record;

    const net::HttpResponse res = send_authed(
        "POST", session_.pds_url + "/xrpc/com.atproto.repo.createRecord", body.dump());
    if (!res.ok())
        return std::nullopt;

    Status s;
    s.platform = Platform::Bluesky;
    s.account = me_;
    s.text = draft.text;
    s.created_at = util::now_unix();
    try {
        const json j = json::parse(res.body);
        s.id = j.value("uri", "");
        s.cid = j.value("cid", "");
    } catch (...) {
    }
    return s;
}

bool BlueskyAccount::create_record(const char* collection, const std::string& record_json) {
    json body;
    body["repo"] = credentials_.did;
    body["collection"] = collection;
    body["record"] = json::parse(record_json);
    const net::HttpResponse res = send_authed(
        "POST", session_.pds_url + "/xrpc/com.atproto.repo.createRecord", body.dump());
    return res.ok();
}

bool BlueskyAccount::delete_record(const char* collection, const std::string& at_uri) {
    json body;
    body["repo"] = credentials_.did;
    body["collection"] = collection;
    body["rkey"] = rkey_from_uri(at_uri);
    const net::HttpResponse res = send_authed(
        "POST", session_.pds_url + "/xrpc/com.atproto.repo.deleteRecord", body.dump());
    return res.ok();
}

std::optional<std::string> BlueskyAccount::get_profile_body(const std::string& actor) {
    const std::string url =
        session_.pds_url + "/xrpc/app.bsky.actor.getProfile?actor=" + util::percent_encode(actor);
    const net::HttpResponse res = send_authed("GET", url, "");
    if (!res.ok())
        return std::nullopt;
    return res.body;
}

std::optional<User> BlueskyAccount::fetch_profile(const std::string& id) {
    auto body = get_profile_body(id);
    if (!body)
        return std::nullopt;
    try {
        return bluesky::map_author(json::parse(*body));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Relationship> BlueskyAccount::relationship(const std::string& id) {
    auto body = get_profile_body(id);
    if (!body)
        return std::nullopt;
    try {
        const json j = json::parse(*body);
        Relationship r;
        r.id = j.value("did", id);
        if (auto v = j.find("viewer"); v != j.end() && v->is_object()) {
            r.following = v->contains("following"); // a record uri present == following
            r.muting = v->value("muted", false);
            r.blocking = v->contains("blocking");
        }
        return r;
    } catch (...) {
        return std::nullopt;
    }
}

bool BlueskyAccount::follow(const std::string& id) {
    const json rec = {{"$type", "app.bsky.graph.follow"},
                      {"subject", id},
                      {"createdAt", util::format_iso8601(util::now_unix())}};
    return create_record("app.bsky.graph.follow", rec.dump());
}

bool BlueskyAccount::block(const std::string& id) {
    const json rec = {{"$type", "app.bsky.graph.block"},
                      {"subject", id},
                      {"createdAt", util::format_iso8601(util::now_unix())}};
    return create_record("app.bsky.graph.block", rec.dump());
}

// Bluesky follow/block are records; undo by deleting the record whose URI the
// profile's viewer state carries.
bool BlueskyAccount::unfollow(const std::string& id) {
    auto body = get_profile_body(id);
    if (!body)
        return false;
    try {
        const json j = json::parse(*body);
        if (auto v = j.find("viewer"); v != j.end()) {
            const std::string uri = v->value("following", std::string{});
            if (!uri.empty())
                return delete_record("app.bsky.graph.follow", uri);
        }
    } catch (...) {
    }
    return false;
}

bool BlueskyAccount::unblock(const std::string& id) {
    auto body = get_profile_body(id);
    if (!body)
        return false;
    try {
        const json j = json::parse(*body);
        if (auto v = j.find("viewer"); v != j.end()) {
            const std::string uri = v->value("blocking", std::string{});
            if (!uri.empty())
                return delete_record("app.bsky.graph.block", uri);
        }
    } catch (...) {
    }
    return false;
}

bool BlueskyAccount::mute_actor(const std::string& did, bool mute) {
    const json body = {{"actor", did}};
    const std::string ep = mute ? "app.bsky.graph.muteActor" : "app.bsky.graph.unmuteActor";
    const net::HttpResponse res = send_authed("POST", session_.pds_url + "/xrpc/" + ep, body.dump());
    return res.ok();
}
bool BlueskyAccount::mute(const std::string& id) { return mute_actor(id, true); }
bool BlueskyAccount::unmute(const std::string& id) { return mute_actor(id, false); }

bool BlueskyAccount::favorite(const Status& status) {
    const Status& t = status.display_status();
    if (!t.cid)
        return false;
    json record;
    record["$type"] = "app.bsky.feed.like";
    record["subject"] = json{{"uri", t.id}, {"cid", *t.cid}};
    record["createdAt"] = util::format_iso8601(util::now_unix());
    return create_record("app.bsky.feed.like", record.dump());
}

bool BlueskyAccount::unfavorite(const Status& status) {
    const Status& t = status.display_status();
    if (!t.like_uri)
        return false;
    return delete_record("app.bsky.feed.like", *t.like_uri);
}

bool BlueskyAccount::boost(const Status& status) {
    const Status& t = status.display_status();
    if (!t.cid)
        return false;
    json record;
    record["$type"] = "app.bsky.feed.repost";
    record["subject"] = json{{"uri", t.id}, {"cid", *t.cid}};
    record["createdAt"] = util::format_iso8601(util::now_unix());
    return create_record("app.bsky.feed.repost", record.dump());
}

bool BlueskyAccount::unboost(const Status& status) {
    const Status& t = status.display_status();
    if (!t.repost_uri)
        return false;
    return delete_record("app.bsky.feed.repost", *t.repost_uri);
}

} // namespace fastsm
