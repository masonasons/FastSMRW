#include "fastsm/presentation/status_presenter.hpp"

#include <string>
#include <vector>

#include "fastsm/util/html_stripper.hpp"
#include "fastsm/util/relative_date.hpp"

namespace fastsm::present {
namespace {

// U+267A RECYCLING SYMBOL FOR GENERIC MATERIALS (♺) in UTF-8 — the boost mark,
// matching the Mac app's compactLine.
constexpr const char* kBoostMark = "\xE2\x99\xBA";

std::string one_line(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        out.push_back((c == '\n' || c == '\r' || c == '\t') ? ' ' : c);
    return out;
}

void add(std::vector<std::string>& parts, std::string s) {
    if (!s.empty())
        parts.push_back(std::move(s));
}

std::string join(const std::vector<std::string>& parts, const char* sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += sep;
        out += parts[i];
    }
    return out;
}

std::string media_summary(const std::vector<MediaAttachment>& media) {
    std::vector<std::string> described;
    for (const auto& m : media)
        if (!m.description.empty())
            described.push_back(m.description);
    if (described.empty()) {
        const size_t n = media.size();
        return std::to_string(n) + (n == 1 ? " attachment" : " attachments");
    }
    return "Attachments: " + join(described, "; ");
}

std::string stats(const Status& s) {
    auto plural = [](int n, const char* one, const char* many) {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    };
    return plural(s.replies_count, "reply", "replies") + ", " +
           plural(s.boosts_count, "boost", "boosts") + ", " +
           plural(s.favourites_count, "favorite", "favorites");
}

std::string visibility_name(Visibility v) {
    switch (v) {
    case Visibility::Public:
        return "Public";
    case Visibility::Unlisted:
        return "Unlisted";
    case Visibility::Private:
        return "Followers only";
    case Visibility::Direct:
        return "Direct";
    }
    return {};
}

} // namespace

std::string compact_line(const Status& s, std::int64_t now) {
    const Status& d = s.display_status();
    const std::string time = util::relative_compact(d.created_at, now);
    std::string prefix;
    if (s.is_boost())
        prefix = s.account.best_name() + " " + kBoostMark + " ";
    std::string body = d.has_content_warning()
                           ? ("[CW] " + (d.spoiler_text ? *d.spoiler_text : std::string()))
                           : one_line(d.text);
    return prefix + d.account.best_name() + " (" + time + "): " + body;
}

std::optional<std::string> status_field_string(StatusSpeechField field, const Status& s,
                                               std::int64_t now) {
    const Status& d = s.display_status();
    switch (field) {
    case StatusSpeechField::BoostedBy:
        return s.is_boost() ? std::optional(s.account.best_name() + " boosted") : std::nullopt;
    case StatusSpeechField::Author:
        return d.account.best_name();
    case StatusSpeechField::Handle:
        return d.account.acct.empty() ? std::nullopt : std::optional("@" + d.account.acct);
    case StatusSpeechField::ContentWarning:
        return d.has_content_warning() ? std::optional("Content warning: " + *d.spoiler_text)
                                       : std::nullopt;
    case StatusSpeechField::Text:
        return d.text.empty() ? std::nullopt : std::optional(one_line(d.text));
    case StatusSpeechField::Quote:
        return d.quote ? std::optional("Quoting " + d.quote->account.best_name() + ": " +
                                       one_line(d.quote->text))
                       : std::nullopt;
    case StatusSpeechField::Media:
        return d.media_attachments.empty() ? std::nullopt
                                           : std::optional(media_summary(d.media_attachments));
    case StatusSpeechField::Poll:
        return d.poll ? std::optional("Poll with " + std::to_string(d.poll->options.size()) +
                                      " options")
                      : std::nullopt;
    case StatusSpeechField::Time:
        return util::relative_spoken(d.created_at, now);
    case StatusSpeechField::Stats:
        return stats(d);
    case StatusSpeechField::Favorited:
        return d.favourited ? std::optional<std::string>("Favorited") : std::nullopt;
    case StatusSpeechField::Boosted:
        return d.boosted ? std::optional<std::string>("Boosted") : std::nullopt;
    case StatusSpeechField::Visibility:
        return d.visibility ? std::optional(visibility_name(*d.visibility)) : std::nullopt;
    case StatusSpeechField::ReplyIndicator:
        return d.in_reply_to_id ? std::optional<std::string>("Reply") : std::nullopt;
    case StatusSpeechField::Source:
        return d.application_name ? std::optional("via " + *d.application_name) : std::nullopt;
    }
    return std::nullopt;
}

std::string accessibility_label(const Status& s, std::int64_t now,
                                const std::vector<SpeechItem<StatusSpeechField>>& fields) {
    std::vector<std::string> parts;
    for (const auto& item : fields) {
        if (!item.enabled)
            continue;
        if (auto str = status_field_string(item.field, s, now); str && !str->empty())
            parts.push_back(std::move(*str));
    }
    return join(parts, ", ");
}

std::string accessibility_label(const Status& s, std::int64_t now) {
    return accessibility_label(s, now, SpeechConfig::current().status);
}

namespace {
std::optional<std::string> user_field_string(UserSpeechField f, const User& u) {
    switch (f) {
    case UserSpeechField::Author:
        return u.best_name().empty() ? std::nullopt : std::optional(u.best_name());
    case UserSpeechField::Handle:
        return u.acct.empty() ? std::nullopt : std::optional("@" + u.acct);
    case UserSpeechField::Bot:
        return u.bot ? std::optional<std::string>("Bot") : std::nullopt;
    case UserSpeechField::Locked:
        return u.locked ? std::optional<std::string>("Locked") : std::nullopt;
    case UserSpeechField::Bio: {
        const std::string bio = one_line(util::strip_html(u.note));
        return bio.empty() ? std::nullopt : std::optional(bio);
    }
    case UserSpeechField::Followers:
        return std::optional(std::to_string(u.followers_count) + " followers");
    case UserSpeechField::Following:
        return std::optional(std::to_string(u.following_count) + " following");
    case UserSpeechField::Posts:
        return std::optional(std::to_string(u.statuses_count) + " posts");
    }
    return std::nullopt;
}

const char* notification_action_phrase(Notification::Kind k) {
    switch (k) {
    case Notification::Kind::Follow:
        return "followed you";
    case Notification::Kind::FollowRequest:
        return "requested to follow you";
    case Notification::Kind::Favourite:
        return "favorited your post";
    case Notification::Kind::Reblog:
        return "boosted your post";
    case Notification::Kind::Mention:
        return "mentioned you";
    case Notification::Kind::Poll:
        return "ran a poll that ended";
    case Notification::Kind::Status:
        return "posted";
    case Notification::Kind::Update:
        return "edited a post";
    case Notification::Kind::Unknown:
        return "sent a notification";
    }
    return "sent a notification";
}

std::optional<std::string> notification_field_string(NotificationSpeechField f, const Notification& n,
                                                     std::int64_t now) {
    switch (f) {
    case NotificationSpeechField::Actor:
        return n.account.best_name().empty() ? std::nullopt : std::optional(n.account.best_name());
    case NotificationSpeechField::Action:
        return std::optional<std::string>(notification_action_phrase(n.type));
    case NotificationSpeechField::Handle:
        return n.account.acct.empty() ? std::nullopt : std::optional("@" + n.account.acct);
    case NotificationSpeechField::Text:
        return (n.status && !n.status->display_status().text.empty())
                   ? std::optional(one_line(n.status->display_status().text))
                   : std::nullopt;
    case NotificationSpeechField::Time:
        return util::relative_spoken(n.created_at, now);
    }
    return std::nullopt;
}
} // namespace

std::string accessibility_label(const User& u) {
    std::vector<std::string> parts;
    for (const auto& item : SpeechConfig::current().user) {
        if (!item.enabled)
            continue;
        if (auto str = user_field_string(item.field, u); str && !str->empty())
            parts.push_back(std::move(*str));
    }
    if (parts.empty())
        parts.push_back(u.best_name()); // never read a blank row
    return join(parts, ", ");
}

std::string accessibility_label(const Notification& n, std::int64_t now) {
    std::vector<std::string> parts;
    for (const auto& item : SpeechConfig::current().notification) {
        if (!item.enabled)
            continue;
        if (auto str = notification_field_string(item.field, n, now); str && !str->empty())
            parts.push_back(std::move(*str));
    }
    if (parts.empty())
        parts.push_back(n.account.best_name());
    return join(parts, ", ");
}

std::string compact_line(const TimelineItem& item, std::int64_t now) {
    if (const Status* s = std::get_if<Status>(&item.value))
        return compact_line(*s, now);
    if (const Notification* n = std::get_if<Notification>(&item.value)) {
        std::string line = n->account.best_name();
        if (n->status)
            line += ": " + one_line(n->status->text);
        return line;
    }
    if (const User* u = std::get_if<User>(&item.value))
        return u->best_name() + " (@" + u->acct + ")";
    return {};
}

std::string accessibility_label(const TimelineItem& item, std::int64_t now) {
    if (const Status* s = std::get_if<Status>(&item.value))
        return accessibility_label(*s, now);
    if (const Notification* n = std::get_if<Notification>(&item.value))
        return accessibility_label(*n, now);
    if (const User* u = std::get_if<User>(&item.value))
        return accessibility_label(*u);
    return compact_line(item, now);
}

std::string post_info(const Status& s, std::int64_t now) {
    std::string out;
    out += s.account.best_name() + " (@" + s.account.acct + ")\n";
    out += util::relative_spoken(s.created_at, now) + "\n";
    if (s.has_content_warning())
        out += "Content warning: " + *s.spoiler_text + "\n";
    out += "\n" + s.text + "\n";
    if (!s.media_attachments.empty()) {
        std::vector<std::string> descs;
        for (const auto& m : s.media_attachments)
            if (!m.description.empty())
                descs.push_back(m.description);
        out += "\n";
        if (descs.empty())
            out += std::to_string(s.media_attachments.size()) + " attachment(s)\n";
        else
            out += "Attachments: " + join(descs, "; ") + "\n";
    }
    out += "\n" + std::to_string(s.replies_count) + " replies, " +
           std::to_string(s.boosts_count) + " boosts, " + std::to_string(s.favourites_count) +
           " favorites";
    return out;
}

std::string user_profile(const User& u) {
    std::string out;
    out += u.best_name() + "\n@" + u.acct + "\n";
    std::vector<std::string> flags;
    if (u.bot)
        flags.push_back("Bot");
    if (u.locked)
        flags.push_back("Locked account");
    if (!flags.empty())
        out += join(flags, " \xC2\xB7 ") + "\n"; // " · " (middle dot)
    const std::string bio = util::strip_html(u.note);
    if (!bio.empty())
        out += "\n" + bio + "\n";
    out += "\n" + std::to_string(u.followers_count) + " followers, " +
           std::to_string(u.following_count) + " following, " +
           std::to_string(u.statuses_count) + " posts";
    return out;
}

} // namespace fastsm::present
