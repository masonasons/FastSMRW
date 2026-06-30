#include "fastsm/store/app_config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

#include "fastsm/models/serialization.hpp"
#include "fastsm/store/dpapi.hpp"
#include "fastsm/store/paths.hpp"
#include "fastsm/util/base64.hpp"
#include "fastsm/store/settings_json.hpp"

using nlohmann::json;

namespace fastsm::store {
namespace {

json credential_to_json(const StoredCredential& c) {
    json j;
    if (c.mastodon) {
        j["type"] = "mastodon";
        j["instance_url"] = c.mastodon->instance_url;
        j["client_id"] = c.mastodon->client_id;
        j["client_secret"] = c.mastodon->client_secret;
        j["access_token"] = c.mastodon->access_token;
    } else if (c.bluesky) {
        j["type"] = "bluesky";
        j["service_url"] = c.bluesky->service_url;
        j["identifier"] = c.bluesky->identifier;
        j["app_password"] = c.bluesky->app_password;
        j["did"] = c.bluesky->did;
        j["handle"] = c.bluesky->handle;
    }
    return j;
}

StoredCredential credential_from_json(const json& j) {
    StoredCredential c;
    const std::string type = j.value("type", "");
    if (type == "mastodon") {
        MastodonCredentials m;
        m.instance_url = j.value("instance_url", "");
        m.client_id = j.value("client_id", "");
        m.client_secret = j.value("client_secret", "");
        m.access_token = j.value("access_token", "");
        c.mastodon = m;
    } else if (type == "bluesky") {
        BlueskyCredentials b;
        b.service_url = j.value("service_url", "");
        b.identifier = j.value("identifier", "");
        b.app_password = j.value("app_password", "");
        b.did = j.value("did", "");
        b.handle = j.value("handle", "");
        c.bluesky = b;
    }
    return c;
}

} // namespace

AppConfigStore::AppConfigStore(std::filesystem::path path) : path_(std::move(path)) {}

AppConfigStore AppConfigStore::default_store() {
    return AppConfigStore(config_dir() / "config.json");
}

AppConfig AppConfigStore::load() const {
    AppConfig config;
    std::ifstream in(path_, std::ios::binary);
    if (!in)
        return config;
    json root;
    try {
        in >> root;
    } catch (...) {
        return config;
    }
    config.selected_account_key = root.value("selected", "");
    if (auto it = root.find("accounts"); it != root.end() && it->is_array()) {
        for (const auto& a : *it) {
            AccountRecord rec;
            rec.account_key = a.value("account_key", "");
            rec.platform = static_cast<Platform>(a.value("platform", 0));
            if (auto me = a.find("me"); me != a.end())
                rec.me = me->get<User>();
            const std::string enc = util::base64_decode(a.value("credential_enc", ""));
            if (auto plain = dpapi_unprotect(enc)) {
                try {
                    rec.credential = credential_from_json(json::parse(*plain));
                } catch (...) {
                }
            }
            config.accounts.push_back(std::move(rec));
        }
    }
    // App settings live in this file alongside accounts.
    if (auto it = root.find("settings"); it != root.end() && it->is_object())
        config.settings = settings_from_json(*it);
    return config;
}

bool AppConfigStore::save(const AppConfig& config) const {
    json root;
    root["selected"] = config.selected_account_key;
    root["accounts"] = json::array();
    for (const auto& rec : config.accounts) {
        json a;
        a["account_key"] = rec.account_key;
        a["platform"] = static_cast<int>(rec.platform);
        a["me"] = rec.me;
        const std::string plain = credential_to_json(rec.credential).dump();
        a["credential_enc"] = util::base64_encode(dpapi_protect(plain));
        root["accounts"].push_back(std::move(a));
    }
    root["settings"] = settings_to_json(config.settings);

    try {
        const std::filesystem::path tmp = path_.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out << root.dump(2);
        }
        std::error_code ec;
        std::filesystem::rename(tmp, path_, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace fastsm::store
