#define _CRT_SECURE_NO_WARNINGS
#include "check.hpp"
#include "fastsm/media/media_player.hpp"
#include "fastsm/net/http_client.hpp"
#include "fastsm/session/core_session.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

using fastsm::media::MediaPlayer;
using namespace std::chrono_literals;

namespace {

// Three seconds of a quiet tone as a 16-bit stereo WAV
void write_tone(const std::filesystem::path& path) {
    const int rate = 44100, frames = rate * 3;
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f)
        return;
    const int data = frames * 4, riff = 36 + data, fmt = 16, byte_rate = rate * 4;
    const short pcm = 1, channels = 2, block = 4, bits = 16;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmt, 4, 1, f);
    std::fwrite(&pcm, 2, 1, f);
    std::fwrite(&channels, 2, 1, f);
    std::fwrite(&rate, 4, 1, f);
    std::fwrite(&byte_rate, 4, 1, f);
    std::fwrite(&block, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data, 4, 1, f);
    for (int i = 0; i < frames; ++i) {
        const short s = static_cast<short>(3000 * std::sin(2 * 3.14159265 * 440 * i / rate));
        std::fwrite(&s, 2, 1, f);
        std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
}

struct Events {
    std::mutex mutex;
    std::vector<MediaPlayer::Event> seen;
    std::string last_text;
    bool has(MediaPlayer::Event e) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto s : seen)
            if (s == e)
                return true;
        return false;
    }
    bool wait_for(MediaPlayer::Event e, std::chrono::milliseconds limit) {
        for (auto waited = 0ms; waited < limit; waited += 20ms) {
            if (has(e))
                return true;
            std::this_thread::sleep_for(20ms);
        }
        return has(e);
    }
};

} // namespace

// The media player over FastPlay's engine, where the core has it: a file opens,
// plays, pauses, seeks and ends, all through the real engine (on its silent
// output, so it runs on machines with no sound card).
void test_media_player() {
    CHECK(!MediaPlayer::is_youtube_url("https://example.com/video.mp4"));
    if (!MediaPlayer::available())
        return; // this build plays media in the apps
#ifdef _WIN32
    _putenv("FASTPLAY_NULL_AUDIO=1");
#else
    setenv("FASTPLAY_NULL_AUDIO", "1", 1);
#endif
    const auto dir = std::filesystem::temp_directory_path() / "fastsm_media_test";
    std::filesystem::create_directories(dir);
    const auto wav = dir / "tone.wav";
    write_tone(wav);

    Events events;
    {
        MediaPlayer player(dir, [&](MediaPlayer::Event e, int, const std::string& text) {
            std::lock_guard<std::mutex> lock(events.mutex);
            events.seen.push_back(e);
            events.last_text = text;
        });
        CHECK(MediaPlayer::youtube_supported() ==
              MediaPlayer::is_youtube_url("https://youtu.be/jNQXAC9IVRw"));
        player.set_volume(50);
        CHECK(player.open(wav.string()) > 0);
        CHECK(events.wait_for(MediaPlayer::Event::Opened, 5000ms));
        CHECK(events.last_text == "tone.wav");
        CHECK(player.playing());
        CHECK(std::fabs(player.length() - 3.0) < 0.05);
        std::this_thread::sleep_for(500ms);
        CHECK(player.position() > 0.2);
        CHECK(!player.toggle_pause()); // paused
        CHECK(player.active() && !player.playing());
        CHECK(player.seek_by(1.5));
        CHECK(player.toggle_pause()); // playing again
        CHECK(events.wait_for(MediaPlayer::Event::Ended, 5000ms));

        // Something that isn't there fails, and says why
        CHECK(player.open((dir / "missing.mp3").string()) > 0);
        CHECK(events.wait_for(MediaPlayer::Event::Failed, 5000ms));
        CHECK(!events.last_text.empty());
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// The core's side: play_media plays audio in the core (media_player events), the
// player's keys come back as commands, and what happens is announced. Images
// still go to the app (media_open).
void test_core_media_flow() {
    if (!MediaPlayer::available())
        return;
#ifdef _WIN32
    _putenv("FASTPLAY_NULL_AUDIO=1");
#else
    setenv("FASTPLAY_NULL_AUDIO", "1", 1);
#endif
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto dir = fs::temp_directory_path() / "fastsm_core_media_test";
    fs::create_directories(dir, ec);
    fs::remove(dir / "config.json", ec);
    const auto wav = dir / "tone.wav";
    write_tone(wav);

    std::mutex m;
    std::vector<nlohmann::json> events;
    auto emit = [&](const std::string& js) {
        std::lock_guard<std::mutex> lk(m);
        events.push_back(nlohmann::json::parse(js));
    };
    // The first event matching, from `from` on
    auto find = [&](const std::string& name, const std::string& key, const std::string& value,
                    size_t& from) -> bool {
        for (int spins = 0; spins < 100; ++spins) {
            {
                std::lock_guard<std::mutex> lk(m);
                for (size_t i = from; i < events.size(); ++i) {
                    if (events[i].value("event", std::string{}) == name &&
                        (key.empty() || events[i].value(key, std::string{}) == value)) {
                        from = i + 1;
                        return true;
                    }
                }
            }
            std::this_thread::sleep_for(50ms);
        }
        return false;
    };

    fastsm::CoreSession::Paths paths;
    paths.config_dir = dir;
    struct NoHttp : fastsm::net::IHttpClient {
        fastsm::net::HttpResponse send(const fastsm::net::HttpRequest&) override { return {}; }
    };
    auto session = std::make_unique<fastsm::CoreSession>(paths, std::make_unique<NoHttp>(), emit);
    size_t at = 0;

    session->dispatch(R"({"cmd":"get_settings"})");
    CHECK(find("settings", "", "", at));
    {
        std::lock_guard<std::mutex> lk(m);
        CHECK(events[at - 1].value("media_in_core", false));
        CHECK(events[at - 1].contains("media_devices"));
    }

    nlohmann::json play = {{"cmd", "play_media"}, {"url", wav.string()}, {"kind", "audio"}, {"title", "Audio: a tone"}};
    session->dispatch(play.dump());
    CHECK(find("media_player", "state", "opening", at));
    CHECK(find("media_player", "state", "playing", at));
    session->dispatch(R"({"cmd":"media_toggle"})");
    CHECK(find("announce", "message", "Paused", at));
    session->dispatch(R"({"cmd":"media_seek","by":1})");
    CHECK(find("announce", "", "", at));
    {
        std::lock_guard<std::mutex> lk(m);
        CHECK(events[at - 1].value("message", std::string{}).find(" of 0:03") != std::string::npos);
    }
    session->dispatch(R"({"cmd":"media_volume","by":-10})");
    CHECK(find("announce", "message", "Volume 90 percent", at));
    session->dispatch(R"({"cmd":"media_stop"})");
    CHECK(find("media_player", "state", "closed", at));
    CHECK(find("announce", "message", "Stopped", at));

    // Played to the end: "ended", and it says so
    session->dispatch(play.dump());
    CHECK(find("media_player", "state", "playing", at));
    CHECK(find("media_player", "state", "ended", at));
    CHECK(find("announce", "message", "Finished", at));

    // A video plays in the core too (its sound)
    nlohmann::json video = {{"cmd", "play_media"}, {"url", wav.string()}, {"kind", "video"}, {"title", "Video"}};
    session->dispatch(video.dump());
    CHECK(find("media_player", "state", "opening", at));
    CHECK(find("media_player", "state", "playing", at));
    session->dispatch(R"({"cmd":"media_stop"})");
    CHECK(find("media_player", "state", "closed", at));

    // A GIF-style video is silent: still the app's to show
    session->dispatch(R"({"cmd":"play_media","url":"https://example.com/a.mp4","kind":"gifv","title":"GIF"})");
    CHECK(find("media_open", "kind", "gifv", at));

    // An image is still the app's to show
    session->dispatch(R"({"cmd":"play_media","url":"https://example.com/a.png","kind":"image","title":"Image"})");
    CHECK(find("media_open", "kind", "image", at));

    session.reset();
    fs::remove_all(dir, ec);
}
