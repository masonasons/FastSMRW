#include "check.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "fastsm/net/http_client.hpp"
#include "fastsm/session/core_session.hpp"

using namespace fastsm;
using nlohmann::json;

namespace {
// A transport that never reaches the network (no accounts in the test config).
struct FakeHttp : net::IHttpClient {
    net::HttpResponse send(const net::HttpRequest&) override { return {}; }
};
} // namespace

void test_capi_session_events() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto dir = fs::temp_directory_path() / "fastsmrw_capi_test";
    fs::create_directories(dir, ec);
    fs::remove(dir / "config.json", ec); // ensure no accounts

    std::mutex m;
    std::condition_variable cv;
    std::vector<std::string> events;
    auto emit = [&](const std::string& js) {
        {
            std::lock_guard<std::mutex> lk(m);
            events.push_back(js);
        }
        cv.notify_all();
    };

    CoreSession::Paths paths;
    paths.config_dir = dir;
    auto session = std::make_unique<CoreSession>(paths, std::make_unique<FakeHttp>(), emit);

    auto wait_for_event = [&](const std::string& name) -> std::optional<json> {
        std::unique_lock<std::mutex> lk(m);
        for (int spins = 0; spins < 100; ++spins) { // up to ~5s
            for (const auto& e : events) {
                json j = json::parse(e);
                if (j.value("event", std::string{}) == name)
                    return j;
            }
            cv.wait_for(lk, std::chrono::milliseconds(50));
        }
        return std::nullopt;
    };

    // get_settings -> a "settings" event carrying the settings object.
    session->dispatch(R"({"cmd":"get_settings"})");
    const auto settings = wait_for_event("settings");
    CHECK(settings.has_value());
    if (settings)
        CHECK(settings->contains("settings"));

    // start -> a "timelines_changed" event (empty, since there are no accounts).
    session->dispatch(R"({"cmd":"start"})");
    const auto timelines = wait_for_event("timelines_changed");
    CHECK(timelines.has_value());
    if (timelines) {
        CHECK(timelines->contains("timelines"));
        CHECK(timelines->at("timelines").is_array());
    }

    session.reset(); // joins the core threads
    fs::remove_all(dir, ec);
}
