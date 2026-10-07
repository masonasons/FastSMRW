#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fastsm::media {

// Plays audio attachments and YouTube links through FastPlay's engine (the
// fastplay_engine library: FFmpeg for nearly every format and stream, yt-dlp for
// YouTube, fetched and kept up to date by the engine itself).
//
// Built in where the core is compiled with FASTSM_FASTPLAY_ENGINE (Windows,
// macOS, iOS); elsewhere available() is false and nothing here does anything.
// One thing plays at a time. Not thread-safe on its own: the core calls it from
// its loop. Events arrive on the engine's own thread.
class MediaPlayer {
public:
    enum class Event { Status, Opened, Failed, Ended, Title };
    // (event, request, text): the request is open()'s, so a late event for
    // something replaced since can be told apart.
    using EventSink = std::function<void(Event, int, const std::string&)>;

    // `data_dir`: where yt-dlp and deno are kept.
    MediaPlayer(const std::filesystem::path& data_dir, EventSink sink);
    ~MediaPlayer(); // stops; no events after it returns

    MediaPlayer(const MediaPlayer&) = delete;
    MediaPlayer& operator=(const MediaPlayer&) = delete;

    static bool available();
    // Whether YouTube links play here, and whether a URL is a YouTube video's.
    static bool youtube_supported();
    static bool is_youtube_url(const std::string& url);
    // The output devices by name, as media_device names them.
    static std::vector<std::string> output_devices();

    // "" for the system's device; takes effect from the next open().
    void set_device(const std::string& name);
    // 0-100: a fraction of full loudness. Heard at once.
    void set_volume(int percent);

    int open(const std::string& url); // plays once opened; returns the request
    void close();
    bool active() const;              // opening, playing, paused or ended
    bool playing() const;
    bool toggle_pause();              // true if now playing
    bool seek_by(double seconds);
    double position() const;
    double length() const;            // 0 if not known (a live stream)
    bool live() const;
    std::string title() const;

    // Its settings as FastPlay.ini text (see fpe_settings_export): taken from
    // such text (how many settings were found), given as it, and reset.
    int import_settings(const std::string& ini);
    std::string export_settings();
    void reset_settings();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fastsm::media
