#pragma once

#include <string>

#include "fastsm/presentation/speech_settings.hpp"

namespace fastsm::store {

// User preferences. Grows over time (Mac has many more); for now the pieces M1
// needs plus the configurable speech field order/visibility.
struct AppSettings {
    bool sounds_enabled = true;
    bool enter_to_send = false; // Return sends (else Ctrl+Return sends)
    std::string soundpack = "Default";
    int fetch_pages = 3;        // API calls per load (1-10), ~40 posts each
    int cache_limit = 200;      // posts cached per timeline (100-20000)
    bool confirm_boost = false;
    bool confirm_favorite = false;
    bool confirm_clear_timeline = true;
    bool confirm_block = true;
    bool show_mentions_in_notifications = true;
    int auto_refresh_seconds = 0; // 0 = off; otherwise poll interval
    bool streaming_enabled = false; // real-time streaming (Mastodon)
    // Invisible interface (Windows): control the client from any window.
    std::string invisible_mode = "off";       // "off" | "hotkey" | "keyhook" | "layer"
    std::string invisible_keymap = "default";  // active keymap name
    std::string invisible_layer_key = "control+win+space"; // combo that toggles the layer
    bool window_shown = true;                  // remembered across restarts (ToggleWindow)
    std::string update_branch = "stable";      // "stable" (versioned) | "latest" (rolling)
    bool check_updates_on_startup = true;      // quietly check on launch
    present::SpeechSettings speech = present::SpeechSettings::defaults();
    present::TextPresentation text; // content-warning / demojify / mention-collapse

    static constexpr int kFetchPagesMin = 1;
    static constexpr int kFetchPagesMax = 10;
    static constexpr int kCacheLimitMin = 100;
    static constexpr int kCacheLimitMax = 20000;
    static constexpr int kMaxMentionsMin = 0;
    static constexpr int kMaxMentionsMax = 20;
    // Auto-refresh choices offered in Settings (seconds); 0 = Off. Mac parity.
    static constexpr int kAutoRefreshOptions[] = {0, 30, 60, 120, 300};
};

} // namespace fastsm::store
