#include "fastsm/presentation/status_presenter.hpp"

#include <string>
#include <vector>

#include "fastsm/util/relative_date.hpp"

namespace fastsm::present {
namespace {

// U+267B RECYCLING SYMBOL, in UTF-8 — the boost indicator in the visual line.
constexpr const char* kBoostMark = "\xE2\x99\xBB";

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

std::string stats(const Status& s) {
    auto count = [](int n, const char* one, const char* many) {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    };
    return count(s.replies_count, "reply", "replies") + ", " +
           count(s.boosts_count, "boost", "boosts") + ", " +
           count(s.favourites_count, "favorite", "favorites");
}

} // namespace

std::string compact_line(const Status& s, std::int64_t now) {
    const Status& d = s.display_status();
    const std::string rel = util::relative_compact(d.created_at, now);
    std::string prefix = d.account.best_name();
    if (s.is_boost())
        prefix = s.account.best_name() + " " + kBoostMark + " " + d.account.best_name();
    return prefix + " (" + rel + "): " + one_line(d.text);
}

std::string accessibility_label(const Status& s, std::int64_t now) {
    const Status& d = s.display_status();
    std::vector<std::string> parts;

    if (s.is_boost())
        add(parts, s.account.best_name() + " boosted");
    add(parts, d.account.best_name());
    if (!d.account.acct.empty())
        add(parts, "@" + d.account.acct);
    if (d.has_content_warning())
        add(parts, "Content warning: " + *d.spoiler_text);
    add(parts, one_line(d.text));

    if (!d.media_attachments.empty()) {
        const size_t n = d.media_attachments.size();
        add(parts, std::to_string(n) + (n == 1 ? " attachment" : " attachments"));
    }
    if (d.poll)
        add(parts, "Poll with " + std::to_string(d.poll->options.size()) + " options");

    add(parts, util::relative_spoken(d.created_at, now));
    if (d.in_reply_to_id)
        add(parts, "Reply");
    add(parts, stats(d));
    if (d.favourited)
        add(parts, "Favorited");
    if (d.boosted)
        add(parts, "Boosted");
    if (d.application_name)
        add(parts, "via " + *d.application_name);

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
