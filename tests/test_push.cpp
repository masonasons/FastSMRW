#include "check.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <vector>

#include "fastsm/platform/mastodon/mastodon_account.hpp"
#include "fastsm/session/core_session.hpp"
#include "fastsm/store/app_config.hpp"
#include "fastsm/store/settings_json.hpp"

using namespace fastsm;
using nlohmann::json;

namespace {
struct PushHttp : net::IHttpClient {
	std::mutex mutex;
	std::vector<net::HttpRequest> requests;
	std::atomic<long> second_status{200};

	net::HttpResponse send(const net::HttpRequest& request) override {
		net::HttpResponse response;
		response.status = 200;
		response.body = "[]";
		if (request.url.find("/api/v1/push/subscription") != std::string::npos) {
			std::lock_guard<std::mutex> lock(mutex);
			requests.push_back(request);
			response.body = "{}";
			if (request.url.find("https://two.example/") == 0) {
				response.status = second_status.load();
			}
		}
		return response;
	}

	std::vector<net::HttpRequest> take_requests() {
		std::lock_guard<std::mutex> lock(mutex);
		auto result = std::move(requests);
		requests.clear();
		return result;
	}
};

bool has_alert(const net::HttpRequest& request, const std::string& key, bool enabled) {
	const std::string field = "data%5Balerts%5D%5B" + key + "%5D=" + (enabled ? "true" : "false");
	return ("&" + request.body + "&").find("&" + field + "&") != std::string::npos;
}

struct PushSession {
	std::filesystem::path dir = std::filesystem::temp_directory_path() /
		("fastsmrw_push_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::mutex mutex;
	std::condition_variable cv;
	std::vector<json> events;
	PushHttp* http = nullptr;
	std::unique_ptr<CoreSession> session;

	explicit PushSession(int account_count = 2) {
		std::filesystem::create_directories(dir);
		store::AppConfig config;
		config.settings.sounds_enabled = false;
		config.settings.auto_refresh_seconds = 0;
		config.settings.streaming_enabled = false;
		for (int i = 0; i < account_count; ++i) {
			store::AccountRecord account;
			account.me.id = std::to_string(i + 1);
			account.me.acct = i == 0 ? "alice@one.example" : "bob@two.example";
			account.account_key = "mastodon:" + account.me.id;
			account.credential.mastodon = MastodonCredentials{
				i == 0 ? "https://one.example" : "https://two.example", "", "", "test-token"};
			config.accounts.push_back(account);
		}
		CHECK(store::AppConfigStore(dir / "config.json").save(config));
		start();
	}

	void start() {
		auto transport = std::make_unique<PushHttp>();
		http = transport.get();
		CoreSession::Paths paths;
		paths.config_dir = dir;
		session = std::make_unique<CoreSession>(paths, std::move(transport), [this](const std::string& event) {
			{
				std::lock_guard<std::mutex> lock(mutex);
				events.push_back(json::parse(event));
			}
			cv.notify_all();
		});
		session->dispatch(R"({"cmd":"start"})");
		const auto settings = wait("settings");
		CHECK_EQ(settings.value("push_alert_types", json::array()).size(), size_t(8));
	}

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

	void subscribe() {
		session->dispatch(R"({"cmd":"push_subscribe","endpoint":"https://relay.example/device","p256dh":"key","auth":"secret"})");
		CHECK(wait("push_subscribe_result").value("ok", false));
	}

	~PushSession() {
		session.reset();
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
	}
};
} // namespace

void test_push_settings() {
	const auto defaults = store::settings_to_json(store::settings_from_json(json::object()));
	CHECK_EQ(defaults["push_alerts"].size(), size_t(8));
	for (const auto& value : defaults["push_alerts"]) {
		CHECK(value == true);
	}
	// Older/incomplete configurations keep missing types on; malformed values
	// cannot crash startup, and unknown server types are not silently enabled.
	const auto partial = store::settings_from_json({{"push_alerts",
		{{"mention", false}, {"poll", nullptr}, {"unknown", true}}}});
	const auto encoded = store::settings_to_json(partial);
	CHECK(!encoded["push_alerts"]["mention"].get<bool>());
	CHECK(encoded["push_alerts"]["poll"].get<bool>());
	CHECK(encoded["push_alerts"]["follow"].get<bool>());
	CHECK(!encoded["push_alerts"].contains("unknown"));
	CHECK_EQ(store::settings_to_json(store::settings_from_json(encoded))["push_alerts"], encoded["push_alerts"]);
	CHECK(store::settings_from_json({{"push_alerts", nullptr}}).push_alerts.mention);
	PushAlerts off;
	for (const auto& def : push_alert_catalog) {
		off.*(def.enabled) = false;
	}
	store::AppSettings settings;
	settings.push_alerts = off;
	const auto restored = store::settings_from_json(store::settings_to_json(settings));
	for (const auto& def : push_alert_catalog) {
		CHECK(!(restored.push_alerts.*(def.enabled)));
	}
}

void test_push_requests() {
	PushHttp http;
	MastodonAccount account({"https://two.example", "", "", "test-token"}, {}, &http);
	PushAlerts alerts;
	alerts.favourite = false;
	alerts.status = false;
	CHECK(account.subscribe_push("https://relay.example/device", "key", "secret", alerts) == PushSubscribe::Ok);
	CHECK(account.update_push_alerts(alerts) == PushSubscribe::Ok);
	auto requests = http.take_requests();
	CHECK_EQ(requests.size(), size_t(2));
	if (requests.size() == 2) {
		CHECK_EQ(requests[0].method, std::string("POST"));
		CHECK_EQ(requests[1].method, std::string("PUT"));
		CHECK(requests[0].body.find("subscription%5Bendpoint%5D=") != std::string::npos);
		CHECK(requests[1].body.find("subscription") == std::string::npos);
		for (const auto& request : requests) {
			for (const auto& key : {"mention", "reblog", "follow", "follow_request", "poll", "update"}) {
				CHECK(has_alert(request, key, true));
			}
			CHECK(has_alert(request, "favourite", false));
			CHECK(has_alert(request, "status", false));
		}
	}
	for (const auto& def : push_alert_catalog) {
		alerts.*(def.enabled) = false;
	}
	CHECK(account.update_push_alerts(alerts) == PushSubscribe::Ok);
	requests = http.take_requests();
	CHECK_EQ(requests.size(), size_t(1));
	if (!requests.empty()) {
		CHECK(requests[0].body.find("true") == std::string::npos);
		for (const auto& def : push_alert_catalog) {
			CHECK(has_alert(requests[0], def.key, false));
		}
	}
	http.second_status = 403;
	CHECK(account.update_push_alerts(alerts) == PushSubscribe::NeedsReauth);
	http.second_status = 503;
	CHECK(account.update_push_alerts(alerts) == PushSubscribe::Failed);
	http.second_status = 404;
	CHECK(account.update_push_alerts(alerts) == PushSubscribe::Failed);
}

void test_push_session() {
	PushSession fixture;
	// Saving while the master switch is off must not create or update pushes.
	fixture.session->dispatch(R"({"cmd":"push_update_alerts","alerts":{"mention":false}})");
	CHECK(fixture.wait("push_update_alerts_result").value("ok", false));
	CHECK(fixture.http->take_requests().empty());
	fixture.subscribe();
	auto requests = fixture.http->take_requests();
	CHECK_EQ(requests.size(), size_t(2));
	for (const auto& request : requests) {
		CHECK(has_alert(request, "mention", false));
		CHECK(has_alert(request, "follow", true));
	}
	// Rapid patches must accumulate and the queued disable must run last.
	// A failure for one account must not obscure the successful other account.
	fixture.http->second_status = 403;
	fixture.session->dispatch(R"({"cmd":"push_update_alerts","alerts":{"reblog":false},"apply_to_subscriptions":true})");
	fixture.session->dispatch(R"({"cmd":"push_update_alerts","alerts":{"follow":false},"apply_to_subscriptions":true})");
	fixture.session->dispatch(R"({"cmd":"push_unsubscribe"})");
	for (int i = 0; i < 2; ++i) {
		const auto result = fixture.wait("push_update_alerts_result");
		CHECK(!result.value("ok", true));
		const auto accounts = result.value("accounts", json::array());
		CHECK_EQ(accounts.size(), size_t(2));
		if (accounts.size() == 2) {
			CHECK(accounts[0].value("ok", false));
			CHECK(!accounts[1].value("ok", true));
			CHECK_EQ(accounts[1].value("reason", ""), std::string("reauth"));
			CHECK_EQ(accounts[1].value("account_key", ""), std::string("mastodon:2"));
		}
	}
	fixture.wait("push_unsubscribe_result");
	requests = fixture.http->take_requests();
	CHECK_EQ(requests.size(), size_t(6));
	if (requests.size() == 6) {
		for (size_t i = 0; i < 4; ++i) {
			CHECK_EQ(requests[i].method, std::string("PUT"));
			CHECK(has_alert(requests[i], "mention", false));
			CHECK(has_alert(requests[i], "reblog", false));
			CHECK(has_alert(requests[i], "follow", i < 2));
		}
		CHECK_EQ(requests[4].method, std::string("DELETE"));
		CHECK_EQ(requests[5].method, std::string("DELETE"));
	}
	// Failed changes survive a full session restart and are used on renewal.
	fixture.session.reset();
	{
		std::lock_guard<std::mutex> lock(fixture.mutex);
		fixture.events.clear();
	}
	fixture.start();
	fixture.subscribe();
	requests = fixture.http->take_requests();
	CHECK_EQ(requests.size(), size_t(2));
	for (const auto& request : requests) {
		CHECK(has_alert(request, "mention", false));
		CHECK(has_alert(request, "reblog", false));
		CHECK(has_alert(request, "follow", false));
	}
}

void test_push_without_accounts() {
	PushSession fixture(0);
	fixture.session->dispatch(R"({"cmd":"push_update_alerts","alerts":{"poll":false},"apply_to_subscriptions":true})");
	const auto result = fixture.wait("push_update_alerts_result");
	CHECK(!result.value("ok", true));
	CHECK_EQ(result.value("reason", ""), std::string("unsupported"));
	CHECK(fixture.http->take_requests().empty());
	fixture.session.reset(); // drain the queued config save before reading it
	CHECK(!store::AppConfigStore(fixture.dir / "config.json").load().settings.push_alerts.poll);
}
