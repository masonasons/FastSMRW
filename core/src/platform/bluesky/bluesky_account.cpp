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
    return {TimelineSource::home()};
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
    if (source.kind != TimelineSource::Kind::Home)
        return page; // only Home in M1

    std::string url =
        session_.pds_url + "/xrpc/app.bsky.feed.getTimeline?limit=" + std::to_string(limit);
    if (cursor.kind == CursorKind::Token && !cursor.value.empty())
        url += "&cursor=" + util::percent_encode(cursor.value);

    const net::HttpResponse res = send_authed("GET", url, "");
    if (!res.ok())
        return page;

    json j;
    try {
        j = json::parse(res.body);
    } catch (...) {
        return page;
    }
    if (auto it = j.find("feed"); it != j.end() && it->is_array()) {
        for (const auto& entry : *it)
            page.items.push_back(TimelineItem{bluesky::map_feed_item(entry)});
    }
    if (auto it = j.find("cursor"); it != j.end() && it->is_string())
        page.next_cursor = PageCursor::token(it->get<std::string>());
    return page;
}

std::optional<Status> BlueskyAccount::post(const PostDraft& draft) {
    json record;
    record["$type"] = "app.bsky.feed.post";
    record["text"] = draft.text;
    record["createdAt"] = util::format_iso8601(util::now_unix());
    if (draft.language)
        record["langs"] = json::array({*draft.language});

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
