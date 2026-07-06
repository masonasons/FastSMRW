// Headless proof that fastsm_core builds and runs on macOS through its C ABI:
// the Darwin NSURLSession transport is wired in, create() succeeds (it returned
// nullptr before the __APPLE__ branch), and the core emits its startup events.
// Mirrors examples/python/smoke.py. Built by macos/build-core.sh.

#include "fastsm/capi/fastsm_core.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

int main() {
    std::printf("core version: %s\n", fastsm_core_version());

    // A throwaway config dir under the system temp dir.
    std::string base = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp/";
    if (base.empty() || base.back() != '/')
        base.push_back('/');
    std::string tmpl = base + "fastsm_smoke_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    const char* dir = mkdtemp(buf.data());
    if (!dir) {
        std::printf("mkdtemp failed\n");
        return 1;
    }

    std::string config = std::string("{\"config_dir\":\"") + dir + "\"}";
    fastsm_core* core = fastsm_core_create(config.c_str());
    if (!core) {
        std::printf("FAIL: fastsm_core_create returned null\n");
        return 1;
    }

    std::mutex m;
    std::vector<std::string> events;
    fastsm_core_set_event_sink(
        core,
        [](void* user, const char* json, size_t len) {
            auto* ev = static_cast<std::vector<std::string>*>(user);
            std::string s(json, len);
            // Pull the "event" name out for the summary.
            std::printf("  EVENT: %.140s\n", s.c_str());
            ev->push_back(s);
        },
        &events);

    auto send = [&](const std::string& cmd) {
        fastsm_core_dispatch(core, cmd.c_str(), cmd.size());
    };
    send("{\"cmd\":\"get_settings\"}");
    send("{\"cmd\":\"get_speech_catalog\"}");
    send("{\"cmd\":\"start\"}");

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    bool have_settings = false, have_timelines = false;
    for (const auto& e : events) {
        if (e.find("\"settings\"") != std::string::npos)
            have_settings = true;
        if (e.find("timelines_changed") != std::string::npos)
            have_timelines = true;
    }

    fastsm_core_destroy(core);

    std::printf("got %zu events\n", events.size());
    if (have_settings && have_timelines) {
        std::printf("OK: fastsm_core runs on macOS through the C ABI.\n");
        return 0;
    }
    std::printf("FAIL: missing expected startup events (settings=%d timelines=%d)\n",
                have_settings, have_timelines);
    return 1;
}
