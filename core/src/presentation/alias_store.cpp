#include "fastsm/presentation/alias_store.hpp"

#include <cctype>

namespace fastsm::present {

std::map<std::string, AliasEntry> Aliases::current_;

namespace {
std::string to_lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

std::string Aliases::key_for(const User& u) {
    // Bluesky ids are DIDs, which are globally stable regardless of handle changes.
    if (u.platform == Platform::Bluesky && !u.id.empty())
        return "did:" + to_lower(u.id);
    // A profile URL is the same no matter which account is viewing the person.
    if (!u.url.empty())
        return to_lower(u.url);
    // Fallbacks (rare): a platform-native id, then the bare handle.
    if (!u.id.empty())
        return "id:" + to_lower(u.id);
    return "acct:" + to_lower(u.acct);
}

std::optional<std::string> Aliases::lookup(const User& u) {
    auto it = current_.find(key_for(u));
    if (it != current_.end() && !it->second.alias.empty())
        return it->second.alias;
    return std::nullopt;
}

const std::map<std::string, AliasEntry>& Aliases::current() { return current_; }

void Aliases::set_current(std::map<std::string, AliasEntry> aliases) {
    current_ = std::move(aliases);
}

} // namespace fastsm::present
