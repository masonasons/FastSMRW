#include "fastsm/util/log.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace fastsm::log {
namespace {

std::mutex g_mutex;
std::ofstream g_stream;

std::string stamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char hms[16];
    std::strftime(hms, sizeof(hms), "%H:%M:%S", &tm);
    char out[32];
    std::snprintf(out, sizeof(out), "%s.%03d", hms, static_cast<int>(ms.count()));
    return out;
}

} // namespace

void init(const std::filesystem::path& file) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_stream.open(file, std::ios::out | std::ios::trunc);
    if (g_stream.is_open()) {
        g_stream << stamp() << "  === FastSMRW log opened ===\n";
        g_stream.flush();
    }
}

void write(const std::string& message) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_stream.is_open())
        return;
    g_stream << stamp() << "  " << message << '\n';
    g_stream.flush();
}

} // namespace fastsm::log
