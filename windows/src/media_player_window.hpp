#pragma once

#include <functional>
#include <string>

#include <windows.h>

namespace fastsmui {

// A streaming audio player over DirectShow: it renders an HTTP URL progressively
// (nothing is downloaded to disk). Reusable both by the pop-up player window and
// for windowless "background" playback owned by the main window. Manages its own
// COM apartment for the graph's lifetime. Not copyable.
class MediaPlayback {
public:
    MediaPlayback();
    ~MediaPlayback();
    MediaPlayback(const MediaPlayback&) = delete;
    MediaPlayback& operator=(const MediaPlayback&) = delete;

    bool play(const std::wstring& url); // start streaming; false if it can't render
    void stop();
    bool active() const;
    void seek(double delta_seconds);
    void adjust_volume(int delta_pct);
    bool toggle_pause(); // returns true if now playing
    bool completed();    // playback reached the end (or aborted)
    double position() const;
    double duration() const;
    int volume_pct() const;

private:
    struct Impl;
    Impl* impl_;
};

// A keys-only pop-up player (owns its own MediaPlayback): Space play/pause,
// Left/Right seek, Up/Down volume, Escape stop+close. `speak` announces feedback.
// Returns false if the stream couldn't be rendered (caller opens the system
// player instead).
bool show_media_player(HWND parent, HINSTANCE inst, const std::wstring& title,
                       const std::wstring& url, std::function<void(const std::wstring&)> speak);

} // namespace fastsmui
