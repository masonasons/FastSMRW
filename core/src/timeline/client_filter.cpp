#include "fastsm/timeline/client_filter.hpp"

#include <algorithm>
#include <cctype>

namespace fastsm {
namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

bool client_filter_should_show(const ClientFilter& f, const TimelineItem& item,
                               const std::string& me_id) {
    if (!f.is_active())
        return true;
    const Status* row = item.status();
    if (!row) // users and bare notifications aren't post-type filtered
        return true;

    // The post to inspect: unwrap a boost to its original (matches FastSM).
    const Status& post = row->display_status();
    const bool is_boost = row->is_boost();
    const bool is_quote = post.quote != nullptr;
    const bool has_media = !post.media_attachments.empty();
    const bool is_reply_to_id = post.in_reply_to_id.has_value();
    const std::string author_id = post.account.id;
    const std::string reply_acct = post.in_reply_to_account_id.value_or(std::string{});

    // A self-reply (thread) needs in_reply_to_account_id == the author; without it
    // we can't tell, so treat it as a reply to others (FastSM's safer default).
    const bool is_thread = is_reply_to_id && !reply_acct.empty() && reply_acct == author_id;
    const bool is_reply = is_reply_to_id && !is_thread;
    const bool is_reply_to_me = is_reply_to_id && !me_id.empty() && reply_acct == me_id;
    const bool is_original = !is_boost && !is_reply_to_id;
    const bool is_my_post = !me_id.empty() && author_id == me_id;
    const bool is_my_reply = is_reply_to_id && is_my_post;

    if (is_boost && !f.boosts)
        return false;
    if (is_quote && !f.quotes)
        return false;
    if (is_thread && !f.threads)
        return false;
    if (is_reply && !f.replies)
        return false;
    if (is_reply_to_me && !f.replies_to_me)
        return false;
    if (is_original && !f.original)
        return false;
    if (has_media && !f.media)
        return false;
    if (!has_media && !f.no_media)
        return false;
    if (is_my_post && !f.my_posts)
        return false;
    if (is_my_reply && !f.my_replies)
        return false;

    if (!f.text.empty()) {
        const std::string needle = to_lower(f.text);
        std::string hay = to_lower(post.text + " " + post.account.display_name + " " + post.account.acct);
        if (hay.find(needle) == std::string::npos)
            return false;
    }
    return true;
}

} // namespace fastsm
