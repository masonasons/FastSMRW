#include "fastsm/models/serialization.hpp"

#include <memory>
#include <optional>

using nlohmann::json;

namespace fastsm {
namespace {

// --- small helpers for optionals and the recursive Status pointer ---

template <class T>
void put_opt(json& j, const char* key, const std::optional<T>& o) {
    if (o)
        j[key] = *o;
}

template <class T>
void get_opt(const json& j, const char* key, std::optional<T>& o) {
    auto it = j.find(key);
    if (it != j.end() && !it->is_null())
        o = it->get<T>();
    else
        o.reset();
}

void put_status_ptr(json& j, const char* key, const std::shared_ptr<Status>& p);
void get_status_ptr(const json& j, const char* key, std::shared_ptr<Status>& p);

} // namespace

void to_json(json& j, const User& v) {
    j = json{{"id", v.id},
             {"acct", v.acct},
             {"username", v.username},
             {"display_name", v.display_name},
             {"note", v.note},
             {"avatar_url", v.avatar_url},
             {"header_url", v.header_url},
             {"url", v.url},
             {"followers_count", v.followers_count},
             {"following_count", v.following_count},
             {"statuses_count", v.statuses_count},
             {"created_at", v.created_at},
             {"bot", v.bot},
             {"locked", v.locked},
             {"platform", static_cast<int>(v.platform)}};
}

void from_json(const json& j, User& v) {
    v.id = j.value("id", "");
    v.acct = j.value("acct", "");
    v.username = j.value("username", "");
    v.display_name = j.value("display_name", "");
    v.note = j.value("note", "");
    v.avatar_url = j.value("avatar_url", "");
    v.header_url = j.value("header_url", "");
    v.url = j.value("url", "");
    v.followers_count = j.value("followers_count", 0);
    v.following_count = j.value("following_count", 0);
    v.statuses_count = j.value("statuses_count", 0);
    v.created_at = j.value("created_at", std::int64_t{0});
    v.bot = j.value("bot", false);
    v.locked = j.value("locked", false);
    v.platform = static_cast<Platform>(j.value("platform", 0));
}

void to_json(json& j, const MediaAttachment& v) {
    j = json{{"id", v.id},
             {"type", static_cast<int>(v.type)},
             {"url", v.url},
             {"preview_url", v.preview_url},
             {"description", v.description}};
}

void from_json(const json& j, MediaAttachment& v) {
    v.id = j.value("id", "");
    v.type = static_cast<MediaAttachment::Kind>(j.value("type", 4)); // Unknown
    v.url = j.value("url", "");
    v.preview_url = j.value("preview_url", "");
    v.description = j.value("description", "");
}

void to_json(json& j, const Mention& v) {
    j = json{{"id", v.id}, {"acct", v.acct}, {"username", v.username}, {"url", v.url}};
}

void from_json(const json& j, Mention& v) {
    v.id = j.value("id", "");
    v.acct = j.value("acct", "");
    v.username = j.value("username", "");
    v.url = j.value("url", "");
}

void to_json(json& j, const Card& v) {
    j = json{{"url", v.url},
             {"title", v.title},
             {"description", v.description},
             {"image_url", v.image_url}};
}

void from_json(const json& j, Card& v) {
    v.url = j.value("url", "");
    v.title = j.value("title", "");
    v.description = j.value("description", "");
    v.image_url = j.value("image_url", "");
}

void to_json(json& j, const Poll::Option& v) {
    j = json{{"title", v.title}, {"votes_count", v.votes_count}};
}

void from_json(const json& j, Poll::Option& v) {
    v.title = j.value("title", "");
    v.votes_count = j.value("votes_count", 0);
}

void to_json(json& j, const Poll& v) {
    j = json{{"id", v.id},
             {"expires_at", v.expires_at},
             {"expired", v.expired},
             {"multiple", v.multiple},
             {"votes_count", v.votes_count},
             {"voted", v.voted},
             {"options", v.options}};
}

void from_json(const json& j, Poll& v) {
    v.id = j.value("id", "");
    v.expires_at = j.value("expires_at", std::int64_t{0});
    v.expired = j.value("expired", false);
    v.multiple = j.value("multiple", false);
    v.votes_count = j.value("votes_count", 0);
    v.voted = j.value("voted", false);
    if (auto it = j.find("options"); it != j.end())
        v.options = it->get<std::vector<Poll::Option>>();
}

void to_json(json& j, const Status& v) {
    j = json{{"id", v.id},
             {"account", v.account},
             {"content", v.content},
             {"text", v.text},
             {"created_at", v.created_at},
             {"favourites_count", v.favourites_count},
             {"boosts_count", v.boosts_count},
             {"replies_count", v.replies_count},
             {"media_attachments", v.media_attachments},
             {"mentions", v.mentions},
             {"pinned", v.pinned},
             {"favourited", v.favourited},
             {"boosted", v.boosted},
             {"platform", static_cast<int>(v.platform)}};
    put_opt(j, "in_reply_to_id", v.in_reply_to_id);
    put_opt(j, "in_reply_to_account_id", v.in_reply_to_account_id);
    put_opt(j, "spoiler_text", v.spoiler_text);
    put_opt(j, "card", v.card);
    put_opt(j, "poll", v.poll);
    put_opt(j, "application_name", v.application_name);
    put_opt(j, "instance_url", v.instance_url);
    put_opt(j, "cid", v.cid);
    put_opt(j, "like_uri", v.like_uri);
    put_opt(j, "repost_uri", v.repost_uri);
    if (v.visibility)
        j["visibility"] = static_cast<int>(*v.visibility);
    put_status_ptr(j, "reblog", v.reblog);
    put_status_ptr(j, "quote", v.quote);
}

void from_json(const json& j, Status& v) {
    v.id = j.value("id", "");
    if (auto it = j.find("account"); it != j.end())
        v.account = it->get<User>();
    v.content = j.value("content", "");
    v.text = j.value("text", "");
    v.created_at = j.value("created_at", std::int64_t{0});
    v.favourites_count = j.value("favourites_count", 0);
    v.boosts_count = j.value("boosts_count", 0);
    v.replies_count = j.value("replies_count", 0);
    if (auto it = j.find("media_attachments"); it != j.end())
        v.media_attachments = it->get<std::vector<MediaAttachment>>();
    if (auto it = j.find("mentions"); it != j.end())
        v.mentions = it->get<std::vector<Mention>>();
    v.pinned = j.value("pinned", false);
    v.favourited = j.value("favourited", false);
    v.boosted = j.value("boosted", false);
    v.platform = static_cast<Platform>(j.value("platform", 0));
    get_opt(j, "in_reply_to_id", v.in_reply_to_id);
    get_opt(j, "in_reply_to_account_id", v.in_reply_to_account_id);
    get_opt(j, "spoiler_text", v.spoiler_text);
    get_opt(j, "card", v.card);
    get_opt(j, "poll", v.poll);
    get_opt(j, "application_name", v.application_name);
    get_opt(j, "instance_url", v.instance_url);
    get_opt(j, "cid", v.cid);
    get_opt(j, "like_uri", v.like_uri);
    get_opt(j, "repost_uri", v.repost_uri);
    if (auto it = j.find("visibility"); it != j.end() && !it->is_null())
        v.visibility = static_cast<Visibility>(it->get<int>());
    else
        v.visibility.reset();
    get_status_ptr(j, "reblog", v.reblog);
    get_status_ptr(j, "quote", v.quote);
}

void to_json(json& j, const Notification& v) {
    j = json{{"id", v.id},
             {"type", static_cast<int>(v.type)},
             {"account", v.account},
             {"created_at", v.created_at},
             {"platform", static_cast<int>(v.platform)}};
    put_status_ptr(j, "status", v.status);
}

void from_json(const json& j, Notification& v) {
    v.id = j.value("id", "");
    v.type = static_cast<Notification::Kind>(j.value("type", 8)); // Unknown
    if (auto it = j.find("account"); it != j.end())
        v.account = it->get<User>();
    v.created_at = j.value("created_at", std::int64_t{0});
    v.platform = static_cast<Platform>(j.value("platform", 0));
    get_status_ptr(j, "status", v.status);
}

void to_json(json& j, const TimelineItem& v) {
    if (const auto* s = std::get_if<Status>(&v.value))
        j = json{{"k", 0}, {"v", *s}};
    else if (const auto* n = std::get_if<Notification>(&v.value))
        j = json{{"k", 1}, {"v", *n}};
    else if (const auto* u = std::get_if<User>(&v.value))
        j = json{{"k", 2}, {"v", *u}};
}

void from_json(const json& j, TimelineItem& v) {
    const int k = j.value("k", 0);
    const json& payload = j.at("v");
    switch (k) {
    case 1:
        v.value = payload.get<Notification>();
        break;
    case 2:
        v.value = payload.get<User>();
        break;
    default:
        v.value = payload.get<Status>();
        break;
    }
}

namespace {

void put_status_ptr(json& j, const char* key, const std::shared_ptr<Status>& p) {
    if (p)
        j[key] = *p;
}

void get_status_ptr(const json& j, const char* key, std::shared_ptr<Status>& p) {
    auto it = j.find(key);
    if (it != j.end() && !it->is_null())
        p = std::make_shared<Status>(it->get<Status>());
    else
        p.reset();
}

} // namespace
} // namespace fastsm
