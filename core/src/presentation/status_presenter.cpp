#include "fastsm/presentation/status_presenter.hpp"

#include <string>
#include <vector>

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

std::string accessibility_label(const Status& s, std::int64_t now) {
    const Status& d = s.display_status();
    std::vector<std::string> parts;

    // Field order mirrors the Mac StatusPresenter (configurable in M3).
    if (s.is_boost())
        add(parts, s.account.best_name() + " boosted");          // boostedBy
    add(parts, d.account.best_name());                            // author
    if (!d.account.acct.empty())
        add(parts, "@" + d.account.acct);                        // handle
    if (d.has_content_warning())
        add(parts, "Content warning: " + *d.spoiler_text);       // contentWarning
    add(parts, one_line(d.text));                                // text
    if (d.quote)                                                 // quote
        add(parts, "Quoting " + d.quote->account.best_name() + ": " + one_line(d.quote->text));
    if (!d.media_attachments.empty())                            // media
        add(parts, media_summary(d.media_attachments));
    if (d.poll)                                                 // poll
        add(parts, "Poll with " + std::to_string(d.poll->options.size()) + " options");
    add(parts, util::relative_spoken(d.created_at, now));        // time
    add(parts, stats(d));                                        // stats
    if (d.favourited)
        add(parts, "Favorited");                                 // favorited
    if (d.boosted)
        add(parts, "Boosted");                                   // boosted
    if (d.visibility)
        add(parts, visibility_name(*d.visibility));              // visibility
    if (d.in_reply_to_id)
        add(parts, "Reply");                                     // replyIndicator
    if (d.application_name)
        add(parts, "via " + *d.application_name);                // source

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
    return compact_line(item, now);
}

} // namespace fastsm::present
