#include "fastsm/capi/fastsm_core.h"

#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "fastsm/fastsm.hpp"
#include "fastsm/session/core_session.hpp"

#ifdef _WIN32
#include "fastsm/net/winhttp_client.hpp"
#endif

// The opaque handle: the event sink (+ a lock) and the C++ session. `session` is
// declared last so it is destroyed (its threads joined) first, while the sink and
// its mutex are still alive — any event emitted during teardown safely sees a
// cleared sink.
struct fastsm_core {
    std::mutex sink_mutex;
    fastsm_event_fn sink = nullptr;
    void* user = nullptr;
    std::unique_ptr<fastsm::CoreSession> session;
};

fastsm_core* fastsm_core_create(const char* config_json) {
    nlohmann::json cfg;
    try {
        cfg = nlohmann::json::parse(config_json ? config_json : "{}");
    } catch (...) {
        cfg = nlohmann::json::object();
    }

    // Paths arrive as UTF-8 JSON strings; build std::filesystem::path from UTF-8.
    auto to_path = [](const std::string& s) {
        return std::filesystem::path(std::u8string(s.begin(), s.end()));
    };
    fastsm::CoreSession::Paths paths;
    paths.config_dir = to_path(cfg.value("config_dir", std::string{}));
    paths.bundled_soundpacks = to_path(cfg.value("soundpacks_dir", std::string{}));
    const std::string user_agent = cfg.value("user_agent", std::string("FastSMRW/0.0.1"));

    std::unique_ptr<fastsm::net::IHttpClient> http;
#ifdef _WIN32
    http = std::make_unique<fastsm::net::WinHttpClient>(user_agent);
#endif
    if (!http)
        return nullptr; // no transport available on this platform yet

    auto* core = new fastsm_core();
    auto emit = [core](const std::string& json) {
        std::lock_guard<std::mutex> lk(core->sink_mutex);
        if (core->sink)
            core->sink(core->user, json.c_str(), json.size());
    };
    core->session =
        std::make_unique<fastsm::CoreSession>(std::move(paths), std::move(http), std::move(emit));
    return core;
}

void fastsm_core_set_event_sink(fastsm_core* core, fastsm_event_fn cb, void* user) {
    if (!core)
        return;
    std::lock_guard<std::mutex> lk(core->sink_mutex);
    core->sink = cb;
    core->user = user;
}

void fastsm_core_dispatch(fastsm_core* core, const char* command_json, size_t len) {
    if (!core || !core->session || !command_json)
        return;
    core->session->dispatch(std::string(command_json, len));
}

void fastsm_core_destroy(fastsm_core* core) {
    if (!core)
        return;
    {
        std::lock_guard<std::mutex> lk(core->sink_mutex);
        core->sink = nullptr; // no events during/after teardown
    }
    delete core; // ~CoreSession joins its threads
}

const char* fastsm_core_version(void) { return fastsm::version(); }
