#include "fastsm/store/account_store.hpp"

#include "fastsm/auth/bluesky_auth.hpp"
#include "fastsm/platform/bluesky/bluesky_account.hpp"
#include "fastsm/platform/mastodon/mastodon_account.hpp"

namespace fastsm {

AccountStore::AccountStore(net::IHttpClient* http) : http_(http) {}

void AccountStore::load(const store::AppConfig& config) {
    entries_.clear();
    for (const auto& rec : config.accounts) {
        if (rec.platform == Platform::Mastodon && rec.credential.mastodon) {
            auto account =
                std::make_unique<MastodonAccount>(*rec.credential.mastodon, rec.me, http_);
            entries_.push_back({std::move(account), rec.credential});
        } else if (rec.platform == Platform::Bluesky && rec.credential.bluesky) {
            // Bluesky tokens aren't persisted; re-establish a session.
            const auto& bc = *rec.credential.bluesky;
            BlueskyAuth auth(http_);
            BlueskyLoginResult login = auth.login(bc.service_url, bc.identifier, bc.app_password);
            if (!login.ok)
                continue; // skip accounts we can't restore this launch
            auto account = std::make_unique<BlueskyAccount>(login.credentials, login.session,
                                                            login.me, http_);
            entries_.push_back({std::move(account), rec.credential});
        }
    }

    selected_key_ = config.selected_account_key;
    if (selected() == nullptr && !entries_.empty())
        selected_key_ = entries_.front().account->account_key();
}

void AccountStore::add(std::unique_ptr<SocialAccount> account, store::StoredCredential credential) {
    if (!account)
        return;
    if (selected_key_.empty())
        selected_key_ = account->account_key();
    entries_.push_back({std::move(account), std::move(credential)});
}

void AccountStore::remove(const std::string& account_key) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->account->account_key() == account_key) {
            entries_.erase(it);
            break;
        }
    }
    if (selected() == nullptr)
        selected_key_ = entries_.empty() ? std::string{} : entries_.front().account->account_key();
}

void AccountStore::select(const std::string& account_key) {
    for (const auto& e : entries_) {
        if (e.account->account_key() == account_key) {
            selected_key_ = account_key;
            return;
        }
    }
}

SocialAccount* AccountStore::selected() const {
    for (const auto& e : entries_) {
        if (e.account->account_key() == selected_key_)
            return e.account.get();
    }
    return nullptr;
}

std::vector<SocialAccount*> AccountStore::accounts() const {
    std::vector<SocialAccount*> out;
    out.reserve(entries_.size());
    for (const auto& e : entries_)
        out.push_back(e.account.get());
    return out;
}

store::AppConfig AccountStore::to_config() const {
    store::AppConfig config;
    config.selected_account_key = selected_key_;
    for (const auto& e : entries_) {
        store::AccountRecord rec;
        rec.account_key = e.account->account_key();
        rec.platform = e.account->platform();
        rec.me = e.account->me();
        rec.credential = e.credential;
        config.accounts.push_back(std::move(rec));
    }
    return config;
}

} // namespace fastsm
