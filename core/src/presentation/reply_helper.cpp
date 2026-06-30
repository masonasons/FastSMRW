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

std::string mention_prefix(const Status& status, const User& me) {
    std::vector<std::string> handles;
    handles.push_back(status.account.acct);
    for (const auto& m : status.mentions)
        handles.push_back(m.acct);

    const std::string mine = lower(me.acct);
    std::unordered_set<std::string> seen;
    std::vector<std::string> ordered;
    for (const auto& handle : handles) {
        const std::string key = lower(handle);
        if (handle.empty() || key == mine || seen.count(key))
            continue;
        seen.insert(key);
        ordered.push_back(handle);
    }

    if (ordered.empty())
        return "";
    std::string out;
    for (size_t i = 0; i < ordered.size(); ++i) {
        if (i)
            out += " ";
        out += "@" + ordered[i];
    }
    return out + " ";
}

} // namespace fastsm::present
