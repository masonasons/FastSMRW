#pragma once

#include <map>
#include <optional>
#include <string>

#include "fastsm/models/user.hpp"

// Global, cross-account user aliases: a custom display name for a person that
// replaces their real display name throughout the app's spoken and on-screen
// output. All composition lives in the core; the front end only edits the store
// and renders what the presenters produce.
namespace fastsm::present {

struct AliasEntry {
    std::string alias;  // the custom display name (replaces the real one)
    std::string handle; // "@acct" snapshot, shown by the aliases manager
};

// The aliases are keyed by a person's canonical, viewer-independent identity so
// the same person is recognized no matter which logged-in account is viewing
// (Mastodon returns local handles bare, so the handle alone isn't stable across
// accounts). Read by the presenters; loaded/saved by the session.
class Aliases {
public:
    // Canonical key for a user: the Bluesky DID (globally stable), else the
    // lowercased profile URL, else a platform-tagged id, else the handle.
    static std::string key_for(const User& u);
    // The alias set for this user, or nullopt if none.
    static std::optional<std::string> lookup(const User& u);

    static const std::map<std::string, AliasEntry>& current();
    static void set_current(std::map<std::string, AliasEntry> aliases);

private:
    static std::map<std::string, AliasEntry> current_;
};

} // namespace fastsm::present
