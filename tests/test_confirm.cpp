#include "check.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fastsm/net/http_client.hpp"
#include "fastsm/platform/mastodon/mastodon_account.hpp"
#include "fastsm/session/core_session.hpp"
#include "fastsm/store/app_config.hpp"
#include "fastsm/store/settings_json.hpp"

using namespace fastsm;
using nlohmann::json;

namespace {

// Answers nothing, so the relationship lookup comes back unknown and the toggle
// settles on "follow" -- which is all these tests need to be deterministic.
struct SilentHttp : net::IHttpClient {
    net::HttpResponse send(const net::HttpRequest&) override { return {}; }
};

// A session with one Mastodon account, so relationship commands have somewhere
// to go.
struct ConfirmSession {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("fastsmrw_confirm_test_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<json> events;
    std::unique_ptr<CoreSession> session;

    ConfirmSession() {
        std::filesystem::create_directories(dir);
        store::AppConfig config;
        config.settings.sounds_enabled = false;
        config.settings.auto_refresh_seconds = 0;
        config.settings.streaming_enabled = false;
        store::AccountRecord account;
        account.me.id = "1";
        account.me.acct = "alice@one.example";
        account.account_key = "mastodon:1";
        account.credential.mastodon =
            MastodonCredentials{"https://one.example", "", "", "test-token"};
        config.accounts.push_back(account);
        CHECK(store::AppConfigStore(dir / "config.json").save(config));

        CoreSession::Paths paths;
        paths.config_dir = dir;
        session = std::make_unique<CoreSession>(
            paths, std::make_unique<SilentHttp>(), [this](const std::string& event) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    events.push_back(json::parse(event));
                }
                cv.notify_all();
            });
        session->dispatch(R"({"cmd":"start"})");
        wait("settings");
    }

    // Wait for one event by name, removing it so a later wait sees the next one.
    json wait(const std::string& name) {
        std::unique_lock<std::mutex> lock(mutex);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;) {
            for (auto it = events.begin(); it != events.end(); ++it) {
                if (it->value("event", "") == name) {
                    json result = *it;
                    events.erase(it);
                    return result;
                }
            }
            if (cv.wait_until(lock, deadline) == std::cv_status::timeout) {
                CHECK(false);
                return json::object();
            }
        }
    }

    bool saw(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& e : events)
            if (e.value("event", "") == name)
                return true;
        return false;
    }

    void set_confirm_follow(bool on) {
        store::AppSettings s;
        s.sounds_enabled = false;
        s.auto_refresh_seconds = 0;
        s.streaming_enabled = false;
        s.confirm_follow = on;
        session->dispatch(
            json{{"cmd", "update_settings"}, {"settings", store::settings_to_json(s)}}.dump());
        wait("settings");
    }

    ~ConfirmSession() {
        session.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

} // namespace

void test_confirm_settings_roundtrip() {
    // Both default to off: turning them on is opt-in, so nobody who upgrades
    // suddenly gets a prompt on a key they've been pressing for months.
    const auto defaults = store::settings_from_json(json::object());
    CHECK(!defaults.confirm_follow);
    CHECK(!defaults.confirm_unfollow);

    store::AppSettings s;
    s.confirm_follow = true;
    s.confirm_unfollow = true;
    const auto restored = store::settings_from_json(store::settings_to_json(s));
    CHECK(restored.confirm_follow);
    CHECK(restored.confirm_unfollow);
}

void test_confirm_follow_toggle() {
    ConfirmSession fixture;

    // Off (the default): the toggle acts straight away and never asks.
    fixture.session->dispatch(R"({"cmd":"follow_toggle","account_id":"7","acct":"bob@two.example"})");
    fixture.wait("announce"); // the action ran (and failed, with no network)
    CHECK(!fixture.saw("confirm"));

    // On: the core asks instead, and hands back the command to run on "yes" --
    // with the direction it resolved, which the UI couldn't have known.
    fixture.set_confirm_follow(true);
    fixture.session->dispatch(R"({"cmd":"follow_toggle","account_id":"7","acct":"bob@two.example"})");
    const json confirm = fixture.wait("confirm");
    CHECK_EQ(confirm.value("title", ""), std::string("Follow"));
    CHECK_EQ(confirm.value("text", ""), std::string("Follow @bob@two.example?"));
    const json command = confirm.value("command", json::object());
    CHECK_EQ(command.value("cmd", ""), std::string("set_relationship"));
    CHECK_EQ(command.value("action", ""), std::string("follow"));
    CHECK_EQ(command.value("account_id", ""), std::string("7"));
}
