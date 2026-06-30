#include "fastsm/platform/mastodon/mastodon_account.hpp"

#include <nlohmann/json.hpp>

#include "fastsm/platform/mastodon/mastodon_map.hpp"
#include "fastsm/util/date_parsing.hpp"
#include "fastsm/util/url.hpp"

using nlohmann::json;

namespace fastsm {
namespace {

// Mastodon paginates some endpoints (bookmarks/favourites, ...) via the Link
// header: Link: <https://host/api/v1/bookmarks?max_id=109>; rel="next", <...>.
// Return the rel="next" link's max_id, if present.
std::optional<std::string> parse_next_max_id(const std::string& link) {
    size_t pos = 0;
    while (true) {
        const size_t lt = link.find('<', pos);
        if (lt == std::string::npos)
            break;
        const size_t gt = link.find('>', lt);
        if (gt == std::string::npos)
            break;
        const std::string url = link.substr(lt + 1, gt - lt - 1);
        const size_t comma = link.find(',', gt);
        const std::string params =
            link.substr(gt, (comma == std::string::npos ? link.size() : comma) - gt);
        if (params.find("rel=\"next\"") != std::string::npos) {
            if (const size_t m = url.find("max_id="); m != std::string::npos) {
                const size_t start = m + 7;
                const size_t end = url.find('&', start);
                return url.substr(start, (end == std::string::npos ? url.size() : end) - start);
            }
        }
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    return std::nullopt;
}

} // namespace

MastodonAccount::MastodonAccount(MastodonCredentials credentials, User me, net::IHttpClient* http,
                                 int max_chars)
    : credentials_(std::move(credentials)), me_(std::move(me)), http_(http),
      max_chars_(max_chars) {}

PlatformFeatures MastodonAccount::features() const {
    PlatformFeatures f;
    f.visibility = true;
    f.content_warning = true;
    f.quote_posts = false; // not on vanilla Mastodon
    f.polls = true;
    f.editing = true;
    f.scheduling = true;
    return f;
}

std::vector<TimelineSource> MastodonAccount::default_timelines() const {
    return {TimelineSource::home(), TimelineSource::notifications()};
}

std::vector<TimelineSource> MastodonAccount::spawnable_timelines() const {
    return {TimelineSource::local(),     TimelineSource::federated(),
            TimelineSource::mentions(),  TimelineSource::bookmarks(),
            TimelineSource::favorites()};
}

bool MastodonAccount::request(const std::string& method, const std::string& url,
                              const std::string& body, const std::string& content_type,
                              std::string& out_body, long& out_status) {
    net::HttpRequest req;
    req.method = method;
    req.url = url;
    req.headers.push_back({"Authorization", "Bearer " + credentials_.access_token});
    if (!body.empty()) {
        req.headers.push_back({"Content-Type", content_type});
        req.body = body;
    }
    const net::HttpResponse res = http_->send(req);
    out_status = res.status;
    out_body = res.body;
    return res.ok();
}

TimelinePage MastodonAccount::items(const TimelineSource& source, int limit,
                                    const PageCursor& cursor) {
    TimelinePage page;

    std::string path;
    std::string extra;
    switch (source.kind) {
    case TimelineSource::Kind::Home:
        path = "/api/v1/timelines/home";
        break;
    case TimelineSource::Kind::Notifications:
        path = "/api/v1/notifications";
        break;
    case TimelineSource::Kind::Local:
    case TimelineSource::Kind::Federated:
        path = "/api/v1/timelines/public";
        break;
    case TimelineSource::Kind::Mentions:
        path = "/api/v1/notifications";
        extra = "&types[]=mention"; // only @-mentions
        break;
    case TimelineSource::Kind::Bookmarks:
        path = "/api/v1/bookmarks";
        break;
    case TimelineSource::Kind::Favorites:
        path = "/api/v1/favourites";
        break;
    }

    std::string url = credentials_.instance_url + path + "?limit=" + std::to_string(limit) + extra;
    if (source.kind == TimelineSource::Kind::Local)
        url += "&local=true";
    if (cursor.kind == CursorKind::MaxID)
        url += "&max_id=" + cursor.value;

    net::HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers.push_back({"Authorization", "Bearer " + credentials_.access_token});
    const net::HttpResponse res = http_->send(req);
    if (!res.ok())
        return page;

    json j;
    try {
        j = json::parse(res.body);
    } catch (...) {
        return page;
    }
    if (!j.is_array())
        return page;

    const bool notifications = source.is_notification_timeline();
    std::string last_id;
    for (const auto& entry : j) {
        if (notifications) {
            Notification n = mastodon::map_notification(entry);
            last_id = n.id;
            page.items.push_back(TimelineItem{std::move(n)});
        } else {
            Status s = mastodon::map_status(entry);
            last_id = s.id;
            page.items.push_back(TimelineItem{std::move(s)});
        }
    }
    // Prefer the Link header's rel="next" max_id (required for bookmarks/
    // favourites, which don't paginate by status id); fall back to the last id.
    if (auto link = res.header("Link")) {
        if (auto next = parse_next_max_id(*link))
            page.next_cursor = PageCursor::max_id(*next);
    }
    if (!page.next_cursor && !last_id.empty())
        page.next_cursor = PageCursor::max_id(last_id);
    return page;
}

std::optional<Status> MastodonAccount::post(const PostDraft& draft) {
    std::vector<std::pair<std::string, std::string>> params;
    params.push_back({"status", draft.text});
    if (draft.reply_to_id)
        params.push_back({"in_reply_to_id", *draft.reply_to_id});
    if (draft.visibility)
        params.push_back({"visibility", visibility_tag(*draft.visibility)});
    if (draft.spoiler_text && !draft.spoiler_text->empty())
        params.push_back({"spoiler_text", *draft.spoiler_text});
    if (draft.language)
        params.push_back({"language", *draft.language});
    if (draft.poll && draft.poll->options.size() >= 2) {
        for (const auto& opt : draft.poll->options)
            params.push_back({"poll[options][]", opt});
        params.push_back({"poll[expires_in]", std::to_string(draft.poll->expires_in_seconds)});
        params.push_back({"poll[multiple]", draft.poll->multiple ? "true" : "false"});
    }
    if (draft.scheduled_at)
        params.push_back({"scheduled_at", util::format_iso8601(*draft.scheduled_at)});

    const std::string url = credentials_.instance_url + "/api/v1/statuses";
    std::string body;
    long status = 0;
    if (!request("POST", url, util::form_encode(params), "application/x-www-form-urlencoded", body,
                 status))
        return std::nullopt;
    try {
        // A scheduled post returns a ScheduledStatus (no "content"); nothing to
        // insert into the timeline.
        json j = json::parse(body);
        if (draft.scheduled_at)
            return std::nullopt;
        return mastodon::map_status(j);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Status> MastodonAccount::edit_post(const std::string& id, const PostDraft& draft) {
    std::vector<std::pair<std::string, std::string>> params;
    params.push_back({"status", draft.text});
    if (draft.spoiler_text)
        params.push_back({"spoiler_text", *draft.spoiler_text});
    if (draft.language)
        params.push_back({"language", *draft.language});

    const std::string url = credentials_.instance_url + "/api/v1/statuses/" + id;
    std::string body;
    long status = 0;
    if (!request("PUT", url, util::form_encode(params), "application/x-www-form-urlencoded", body,
                 status))
        return std::nullopt;
    try {
        return mastodon::map_status(json::parse(body));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<PostSource> MastodonAccount::post_source(const std::string& id) {
    const std::string url = credentials_.instance_url + "/api/v1/statuses/" + id + "/source";
    std::string body;
    long status = 0;
    if (!request("GET", url, "", "", body, status))
        return std::nullopt;
    try {
        const json j = json::parse(body);
        PostSource src;
        src.text = j.value("text", "");
        src.spoiler_text = j.value("spoiler_text", "");
        return src;
    } catch (...) {
        return std::nullopt;
    }
}

bool MastodonAccount::status_action(const std::string& status_id, const char* verb) {
    const std::string url =
        credentials_.instance_url + "/api/v1/statuses/" + status_id + "/" + verb;
    std::string body;
    long status = 0;
    return request("POST", url, "", "", body, status);
}

bool MastodonAccount::boost(const Status& status) {
    return status_action(status.display_status().id, "reblog");
}
bool MastodonAccount::unboost(const Status& status) {
    return status_action(status.display_status().id, "unreblog");
}
bool MastodonAccount::favorite(const Status& status) {
    return status_action(status.display_status().id, "favourite");
}
bool MastodonAccount::unfavorite(const Status& status) {
    return status_action(status.display_status().id, "unfavourite");
}

std::optional<StreamRequest> MastodonAccount::user_stream_request() const {
    // The user stream delivers home-timeline updates + notifications over SSE.
    StreamRequest r;
    r.url = credentials_.instance_url + "/api/v1/streaming/user";
    r.headers = {{"Authorization", "Bearer " + credentials_.access_token}};
    return r;
}

std::optional<StreamItem> MastodonAccount::parse_stream_event(const std::string& event,
                                                              const std::string& data) const {
    try {
        if (event == "update") {
            return StreamItem{TimelineItem{mastodon::map_status(json::parse(data))},
                              TimelineSource::Kind::Home};
        }
        if (event == "notification") {
            return StreamItem{TimelineItem{mastodon::map_notification(json::parse(data))},
                              TimelineSource::Kind::Notifications};
        }
    } catch (...) {
        // Malformed payload — ignore this event.
    }
    return std::nullopt; // delete / filters_changed / keep-alives are ignored
}

} // namespace fastsm
