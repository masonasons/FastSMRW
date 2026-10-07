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

    // One of the engine's audio effects: reverb, echo, EQ and the rest. `name` is
    // for display; the effect is off until turned on, and its parameters only do
    // anything while it is.
    struct Effect {
        std::string key;
        std::string name;
        bool enabled = false;
    };
    // One adjustable value. `effect` is the effect it belongs to, empty for the
    // stream-wide ones (pitch, tempo, rate). `choices` names the values of a
    // choice parameter (a reverb room, a 3D mode) and is empty for a plain number,
    // in which case min/max/step/unit describe the range instead.
    struct Param {
        std::string key;
        std::string name;
        std::string unit;
        std::string effect;
        float min_value = 0.0f;
        float max_value = 1.0f;
        float step = 0.01f;
        float default_value = 0.0f;
        float value = 0.0f;
        std::vector<std::string> choices;
    };
    std::vector<Effect> effects() const;
    std::vector<Param> params() const;
    bool set_effect(const std::string& key, bool on);
    bool set_param(const std::string& key, float value);
    // The reverb is three-way rather than a plain toggle: 0 off, 1 simple (a room
    // you size), 2 advanced (the EFX environments).
    void set_reverb_type(int type);

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
