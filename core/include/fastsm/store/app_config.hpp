#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "fastsm/models/user.hpp"
#include "fastsm/platform/bluesky/bluesky_credentials.hpp"
#include "fastsm/platform/mastodon/mastodon_credentials.hpp"
#include "fastsm/store/app_settings.hpp"

namespace fastsm::store {

// Per-account secret bundle (exactly one platform set).
struct StoredCredential {
    std::optional<MastodonCredentials> mastodon;
    std::optional<BlueskyCredentials> bluesky;
};

struct AccountRecord {
    std::string account_key;
    Platform platform = Platform::Mastodon;
    User me;
    StoredCredential credential;
};

struct AppConfig {
    std::vector<AccountRecord> accounts;
    std::string selected_account_key;
    AppSettings settings; // app preferences live in the same file as accounts
};

// Reads/writes config.json — accounts, the selection, and app settings in one
// file. Non-secret fields are plaintext; the credential bundle is DPAPI-encrypted
// (per-user) and base64-encoded.
class AppConfigStore {
public:
    explicit AppConfigStore(std::filesystem::path path);

    AppConfig load() const;
    bool save(const AppConfig& config) const;

    // Uses config_dir()/config.json.
    static AppConfigStore default_store();

private:
    std::filesystem::path path_;
};

} // namespace fastsm::store
