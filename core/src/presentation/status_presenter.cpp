#include "fastsm/presentation/status_presenter.hpp"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "fastsm/util/demojify.hpp"
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

// Strip emoji per the configured mode (custom first, then unicode — matching the
// Mac port's strippingEmoji order).
std::string apply_emoji(std::string s, EmojiRemoval mode) {
    if (mode == EmojiRemoval::Mastodon || mode == EmojiRemoval::Both)
        s = util::strip_custom_emoji(s);
    if (mode == EmojiRemoval::Unicode || mode == EmojiRemoval::Both)
        s = util::strip_unicode_emoji(s);
    return s;
}

// A post body, cleaned for display/speech: single line, collapsed leading
// @mention run, and emoji stripped per settings.
std::string present_text(const std::string& raw) {
    const TextPresentation& t = TextConfig::current();
    std::string s = one_line(raw);
    s = util::truncate_leading_mentions(s, t.max_mentions);
    return apply_emoji(std::move(s), t.post_emoji);
}

// A display name, cleaned for display/speech: emoji stripped per settings.
std::string present_name(const std::string& name) {
    return apply_emoji(name, TextConfig::current().name_emoji);
}

// Time, honoring the absolute-vs-relative setting. `spoken` picks the wording
// for the relative case ("5 minutes ago" vs the compact "5m").
std::string present_time(std::int64_t when, std::int64_t now, bool spoken) {
    if (TextConfig::current().absolute_time)
        return util::absolute_time(when, now);
    return spoken ? util::relative_spoken(when, now) : util::relative_compact(when, now);
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

// Compose a row's spoken label from its ordered/toggled fields: each enabled,
// non-empty field is wrapped with its before/after text and joined by the
// configured separator -- except an item flagged no_separator_after runs
// straight into the next one. `get` maps a field to its optional string value.
template <class Field, class Getter>
std::string compose_fields(const std::vector<SpeechItem<Field>>& fields, Getter get) {
    const std::string& sep = SpeechConfig::current().separator;
    std::string out;
    std::string pending; // separator to place before the next part
    bool first = true;
    for (const auto& item : fields) {
        if (!item.enabled)
            continue;
        auto str = get(item.field);
        if (!str || str->empty())
            continue;
        if (!first)
            out += pending;
        out += item.before + *str + item.after;
        pending = item.no_separator_after ? std::string() : sep;
        first = false;
    }
    return out;
}

std::string media_type_noun(MediaAttachment::Kind k); // defined below
std::string capitalize(std::string s);

// Each attachment as "Type" or "Type: description", so the media kind is always
// spoken (and shown in Post Info).
std::string media_summary(const std::vector<MediaAttachment>& media) {
    std::vector<std::string> parts;
    for (const auto& m : media) {
        std::string part = capitalize(media_type_noun(m.type));
        if (!m.description.empty())
            part += ": " + m.description;
        parts.push_back(std::move(part));
    }
    return join(parts, "; ");
}

std::string stats(const Status& s) {
    auto plural = [](int n, const char* one, const char* many) {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    };
    // Only mention non-zero counts — "0 boosts" is just noise (Mac parity).
    std::vector<std::string> parts;
    if (s.replies_count > 0)
        parts.push_back(plural(s.replies_count, "reply", "replies"));
    if (s.boosts_count > 0)
        parts.push_back(plural(s.boosts_count, "boost", "boosts"));
    if (s.favourites_count > 0)
        parts.push_back(plural(s.favourites_count, "favorite", "favorites"));
    return join(parts, ", ");
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
    const std::string time = present_time(d.created_at, now, false);
    std::string prefix;
    if (s.is_boost())
        prefix = present_name(s.account.best_name()) + " " + kBoostMark + " ";
    const CwMode cw = TextConfig::current().cw;
    std::string body;
    if (d.has_content_warning() && cw != CwMode::Ignore) {
        body = "[CW] " + (d.spoiler_text ? *d.spoiler_text : std::string());
        if (cw == CwMode::Show)
            body += " " + present_text(d.text);
    } else {
        body = present_text(d.text);
    }
    return prefix + present_name(d.account.best_name()) + " (" + time + "): " + body;
}

std::optional<std::string> status_field_string(StatusSpeechField field, const Status& s,
                                               std::int64_t now) {
    const Status& d = s.display_status();
    switch (field) {
    case StatusSpeechField::BoostedBy:
        return s.is_boost() ? std::optional(s.account.best_name() + " boosted") : std::nullopt;
    case StatusSpeechField::Author:
        return present_name(d.account.best_name());
    case StatusSpeechField::Handle:
        return d.account.acct.empty() ? std::nullopt : std::optional("@" + d.account.acct);
    case StatusSpeechField::ContentWarning:
        // "Ignore" mode drops the warning entirely.
        return (d.has_content_warning() && TextConfig::current().cw != CwMode::Ignore)
                   ? std::optional("Content warning: " + *d.spoiler_text)
                   : std::nullopt;
    case StatusSpeechField::Text:
        // "Hide" mode hides the body behind the warning.
        if (d.has_content_warning() && TextConfig::current().cw == CwMode::Hide)
            return std::nullopt;
        return d.text.empty() ? std::nullopt : std::optional(present_text(d.text));
    case StatusSpeechField::Quote:
        return d.quote ? std::optional("Quoting " + present_name(d.quote->account.best_name()) +
                                       ": " + present_text(d.quote->text))
                       : std::nullopt;
    case StatusSpeechField::Media:
        return d.media_attachments.empty() ? std::nullopt
                                           : std::optional(media_summary(d.media_attachments));
    case StatusSpeechField::Poll:
        return d.poll ? std::optional("Poll with " + std::to_string(d.poll->options.size()) +
                                      " options")
                      : std::nullopt;
    case StatusSpeechField::Time:
        return present_time(d.created_at, now, true);
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

namespace {
// Titles of the server-side filters that matched (warn action), unwrapping a
// boost. Hide-action rows are removed before display, so what remains is "warn".
void collect_filter_titles(const Status& s, std::vector<std::string>& out) {
    for (const auto& f : s.filtered)
        if (!f.title.empty())
            out.push_back(f.title);
    if (s.reblog)
        collect_filter_titles(*s.reblog, out);
}
} // namespace

std::string accessibility_label(const Status& s, std::int64_t now,
                                const std::vector<SpeechItem<StatusSpeechField>>& fields) {
    std::string label = compose_fields(
        fields, [&](StatusSpeechField f) { return status_field_string(f, s, now); });
    std::vector<std::string> titles;
    collect_filter_titles(s, titles);
    if (!titles.empty()) {
        std::string prefix = "Filtered: " + join(titles, ", ");
        return label.empty() ? prefix : prefix + ". " + label;
    }
    return label;
}

std::string accessibility_label(const Status& s, std::int64_t now) {
    return accessibility_label(s, now, SpeechConfig::current().status);
}

namespace {
std::optional<std::string> user_field_string(UserSpeechField f, const User& u) {
    switch (f) {
    case UserSpeechField::Author:
        return u.best_name().empty() ? std::nullopt : std::optional(present_name(u.best_name()));
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
        return n.account.best_name().empty() ? std::nullopt
                                             : std::optional(present_name(n.account.best_name()));
    case NotificationSpeechField::Action:
        return std::optional<std::string>(notification_action_phrase(n.type));
    case NotificationSpeechField::Handle:
        return n.account.acct.empty() ? std::nullopt : std::optional("@" + n.account.acct);
    case NotificationSpeechField::Text:
        return (n.status && !n.status->display_status().text.empty())
                   ? std::optional(present_text(n.status->display_status().text))
                   : std::nullopt;
    case NotificationSpeechField::Time:
        return present_time(n.created_at, now, true);
    }
    return std::nullopt;
}
} // namespace

std::string accessibility_label(const User& u) {
    std::string label = compose_fields(SpeechConfig::current().user,
                                       [&](UserSpeechField f) { return user_field_string(f, u); });
    return label.empty() ? u.best_name() : label; // never read a blank row
}

std::string accessibility_label(const Notification& n, std::int64_t now) {
    std::string label = compose_fields(
        SpeechConfig::current().notification,
        [&](NotificationSpeechField f) { return notification_field_string(f, n, now); });
    return label.empty() ? n.account.best_name() : label;
}

std::string compact_line(const TimelineItem& item, std::int64_t now) {
    if (const Status* s = std::get_if<Status>(&item.value))
        return compact_line(*s, now);
    if (const Notification* n = std::get_if<Notification>(&item.value)) {
        std::string line = present_name(n->account.best_name());
        if (n->status)
            line += ": " + present_text(n->status->text);
        return line;
    }
    if (const User* u = std::get_if<User>(&item.value))
        return present_name(u->best_name()) + " (@" + u->acct + ")";
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

namespace {
// Pull http(s) URLs out of plain text (Bluesky text carries them verbatim; a
// Mastodon post's `text` reconstructs them from its link spans too).
void find_urls_in_text(const std::string& text, std::vector<std::string>& out) {
    const char* trailing = ".,!?;:)]}'\"";
    size_t i = 0;
    while (i < text.size()) {
        size_t p = text.find("http", i);
        if (p == std::string::npos)
            return;
        const bool http = text.compare(p, 7, "http://") == 0;
        const bool https = text.compare(p, 8, "https://") == 0;
        if (!http && !https) {
            i = p + 4;
            continue;
        }
        size_t end = p;
        while (end < text.size() && static_cast<unsigned char>(text[end]) > ' ')
            ++end;
        std::string url = text.substr(p, end - p);
        while (!url.empty() && std::strchr(trailing, url.back()))
            url.pop_back();
        if (url.size() > (https ? 8u : 7u))
            out.push_back(std::move(url));
        i = end;
    }
}

// Extract (visible text, href) pairs from HTML <a> tags, skipping @mention and
// #hashtag anchors (those aren't links a user wants to open). Mirrors the Mac's
// PostLinks.anchors.
void anchors(const std::string& html, std::vector<std::pair<std::string, std::string>>& out) {
    size_t pos = 0;
    while (true) {
        const size_t lt = html.find("<a", pos);
        if (lt == std::string::npos)
            return;
        const char after = lt + 2 < html.size() ? html[lt + 2] : '\0';
        if (after != ' ' && after != '\t' && after != '\n' && after != '>') {
            pos = lt + 2;
            continue;
        }
        const size_t gt = html.find('>', lt);
        if (gt == std::string::npos)
            return;
        const std::string tag = html.substr(lt, gt - lt + 1); // the opening <a ...> tag
        const size_t close = html.find("</a", gt + 1);
        const size_t inner_end = close == std::string::npos ? html.size() : close;
        const std::string inner = html.substr(gt + 1, inner_end - (gt + 1));
        pos = close == std::string::npos ? html.size() : close + 3;
        if (tag.find("mention") != std::string::npos || tag.find("hashtag") != std::string::npos)
            continue; // @mention / #hashtag links aren't openable "links"
        const size_t h = tag.find("href=");
        if (h == std::string::npos || h + 5 >= tag.size())
            continue;
        const char quote = tag[h + 5];
        if (quote != '"' && quote != '\'')
            continue;
        const size_t he = tag.find(quote, h + 6);
        if (he == std::string::npos)
            continue;
        const std::string href = util::decode_entities(tag.substr(h + 6, he - (h + 6)));
        if (href.compare(0, 4, "http") != 0)
            continue;
        out.push_back({util::strip_html(inner), href});
    }
}

std::string media_type_noun(MediaAttachment::Kind k) {
    switch (k) {
    case MediaAttachment::Kind::Image: return "image";
    case MediaAttachment::Kind::Video: return "video";
    case MediaAttachment::Kind::Audio: return "audio";
    case MediaAttachment::Kind::Gifv: return "gifv";
    case MediaAttachment::Kind::Unknown: return "media";
    }
    return "media";
}

std::string capitalize(std::string s) {
    if (!s.empty())
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}
} // namespace

std::vector<PostLink> post_links(const Status& status) {
    const Status& s = status.display_status(); // unwrap a boost
    std::vector<PostLink> out;
    std::vector<std::string> seen;
    auto add = [&](const std::string& title, const std::string& url) {
        if (url.empty())
            return;
        for (const auto& u : seen)
            if (u == url)
                return;
        seen.push_back(url);
        out.push_back({title.empty() ? url : title, url});
    };

    const bool has_card = s.card && !s.card->url.empty();

    // Links embedded in the text. A Mastodon post carries HTML in `content`; a
    // Bluesky post has none, so fall back to scanning its plain `text`.
    std::vector<std::pair<std::string, std::string>> text_links;
    anchors(s.content, text_links);
    if (text_links.empty()) {
        std::vector<std::string> urls;
        find_urls_in_text(s.text, urls);
        for (const auto& u : urls)
            text_links.push_back({std::string{}, u});
    }
    for (const auto& [text, url] : text_links) {
        // The link-preview card's title decorates its matching text link.
        if (has_card && url == s.card->url && !s.card->title.empty())
            add(s.card->title, url);
        else
            add(text, url);
    }

    // The card on its own, if its link wasn't in the text.
    if (has_card)
        add(s.card->title, s.card->url);

    // Media attachments, labeled by description (or the type) plus the type.
    for (const auto& m : s.media_attachments) {
        const std::string noun = media_type_noun(m.type);
        const std::string label = m.description.empty() ? capitalize(noun) : m.description;
        add(label + " (" + noun + ")", m.url);
    }

    // Finally the post itself, so the old "open the post" behavior stays reachable.
    if (!s.url.empty())
        add("Open this post in browser", s.url);
    return out;
}

// Poll block for Post Info. Before voting (and while open) the option titles are
// listed without counts (Mastodon hides them until you vote); once you've voted or
// the poll has closed, each option shows its votes and percentage, your own picks
// are marked, and a summary line follows.
static std::string poll_text(const Poll& p, std::int64_t now) {
    std::string out = "\nPoll";
    if (p.multiple)
        out += " (choose one or more)";
    out += ":\n";
    const bool results = p.voted || p.expired;
    auto is_mine = [&](int i) {
        for (int v : p.own_votes)
            if (v == i)
                return true;
        return false;
    };
    for (size_t i = 0; i < p.options.size(); ++i) {
        const auto& o = p.options[i];
        out += "- " + o.title;
        if (results) {
            const int denom = p.multiple ? p.voters_count : p.votes_count;
            const int pct = denom > 0 ? (o.votes_count * 100 + denom / 2) / denom : 0;
            out += " — " + std::to_string(o.votes_count) +
                   (o.votes_count == 1 ? " vote (" : " votes (") + std::to_string(pct) + "%)";
            if (is_mine(static_cast<int>(i)))
                out += " (your vote)";
        }
        out += "\n";
    }
    if (results) {
        out += std::to_string(p.votes_count) + (p.votes_count == 1 ? " vote" : " votes");
        if (p.multiple && p.voters_count > 0)
            out += " from " + std::to_string(p.voters_count) +
                   (p.voters_count == 1 ? " voter" : " voters");
        out += p.expired ? ". Poll closed.\n" : ". You have voted.\n";
    } else if (p.expired) {
        out += "Poll closed.\n";
    }
    return out;
}

std::string post_info(const Status& s, std::int64_t now) {
    std::string out;
    out += s.account.best_name() + " (@" + s.account.acct + ")\n";
    out += present_time(s.created_at, now, true) + "\n";
    if (s.has_content_warning())
        out += "Content warning: " + *s.spoiler_text + "\n";
    out += "\n" + s.text + "\n";
    if (s.poll)
        out += poll_text(*s.poll, now);
    if (!s.media_attachments.empty()) {
        out += "\n";
        for (const auto& m : s.media_attachments) {
            out += capitalize(media_type_noun(m.type));
            if (!m.description.empty())
                out += ": " + m.description;
            out += "\n";
        }
    }
    if (std::string counts = stats(s); !counts.empty()) // only non-zero counts (Mac parity)
        out += "\n" + counts;
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
