#include "fastsm/presentation/reply_helper.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <vector>

namespace fastsm::present {
namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

std::vector<ReplyParticipant> reply_participants(const Status& status, const User& me) {
    std::vector<ReplyParticipant> all;
    // The author of the post being replied to, then everyone they mentioned.
    all.push_back({status.account.acct, status.account.display_name.empty()
                                            ? (status.account.username.empty()
                                                   ? status.account.acct
                                                   : status.account.username)
                                            : status.account.display_name});
    for (const auto& m : status.mentions)
        all.push_back({m.acct, m.username.empty() ? m.acct : m.username});

    const std::string mine = lower(me.acct);
    std::unordered_set<std::string> seen;
    std::vector<ReplyParticipant> ordered;
    for (const auto& p : all) {
        const std::string key = lower(p.acct);
        if (p.acct.empty() || key == mine || seen.count(key))
            continue;
        seen.insert(key);
        ordered.push_back(p);
    }
    return ordered;
}

std::string mention_prefix(const std::vector<std::string>& accts) {
    std::string out;
    for (const auto& acct : accts) {
        if (acct.empty())
            continue;
        if (!out.empty())
            out += " ";
        out += "@" + acct;
    }
    return out.empty() ? "" : out + " ";
}

std::string mention_prefix(const Status& status, const User& me) {
    std::vector<std::string> accts;
    for (const auto& p : reply_participants(status, me))
        accts.push_back(p.acct);
    return mention_prefix(accts);
}

} // namespace fastsm::present
