#include "fastsm/platform/mastodon/mastodon_account.hpp"

#include <nlohmann/json.hpp>

#include "fastsm/platform/mastodon/mastodon_map.hpp"
#include "fastsm/util/url.hpp"

using nlohmann::json;

namespace fastsm {

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
        path = "/api/v1/notifications"; // filtered client-side in M2
        break;
    }

    std::string url = credentials_.instance_url + path + "?limit=" + std::to_string(limit);
    if (source.kind == TimelineSource::Kind::Local)
        url += "&local=true";
    if (cursor.kind == CursorKind::MaxID)
        url += "&max_id=" + cursor.value;

    std::string body;
    long status = 0;
    if (!request("GET", url, "", "", body, status))
        return page;

    json j;
    try {
        j = json::parse(body);
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
    if (!last_id.empty())
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

    const std::string url = credentials_.instance_url + "/api/v1/statuses";
    std::string body;
    long status = 0;
    if (!request("POST", url, util::form_encode(params), "application/x-www-form-urlencoded", body,
                 status))
        return std::nullopt;
    try {
        return mastodon::map_status(json::parse(body));
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

} // namespace fastsm
