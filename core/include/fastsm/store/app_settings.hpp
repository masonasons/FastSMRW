#pragma once

#include <map>
#include <string>

#include "fastsm/presentation/speech_settings.hpp"

namespace fastsm::store {

// User preferences. Grows over time (Mac has many more); for now the pieces M1
// needs plus the configurable speech field order/visibility.
struct AppSettings {
    bool sounds_enabled = true;
    int sound_volume = 100;     // master earcon/soundpack volume, 0-100 percent
    bool boundary_sound = true; // chime at the top/bottom of a timeline when navigating
    // Per-post navigation earcons: a short sound as you move onto a post carrying
    // each attribute. Each is independently toggleable (the Earcons settings).
    bool earcon_image = true;   // post has an image attachment
    bool earcon_media = true;   // post has other media (video / audio / GIF)
    bool earcon_mention = true; // post mentions you (skipped in the mentions/notifications buffers)
    bool earcon_pinned = true;  // post is pinned to a profile (yours or anyone's)
    bool earcon_poll = true;    // post has a poll
    bool enter_to_send = false; // Return sends (else Ctrl+Return sends)
    std::string soundpack = "Default"; // global default, and the fallback per account
    // Per-account soundpack overrides, keyed by account_key ("mastodon:<id>" /
    // "bluesky:<did>"). An account not present here uses `soundpack`.
    std::map<std::string, std::string> account_soundpacks;
    int fetch_pages = 3;        // API calls per load (1-10), ~40 posts each
    int cache_limit = 200;      // posts cached per timeline (0-20000; 0 = caching off)
    bool confirm_boost = false;
    bool confirm_unboost = false;
    bool confirm_favorite = false;
    bool confirm_unfavorite = false;
    bool confirm_clear_timeline = true;
    bool confirm_block = true;
    bool confirm_unblock = false;
    bool confirm_delete_post = true;
    bool show_mentions_in_notifications = true;
    bool reverse_timelines = false; // newest at the bottom (oldest-first) for time-ordered feeds
    bool auto_load_older = true;    // auto-fetch older posts when you reach the timeline's end
    // What pressing Enter (or the "Enter" invisible action) does by default.
    std::string enter_post_action = "post_info"; // post_info | thread | reply | links
    std::string enter_user_action = "actions";   // actions | profile | timeline
    // The secondary interact (Shift+Enter / "SecondaryAction") on a post.
    std::string secondary_post_action = "play_media"; // play_media | post_info | thread | reply | links
    bool media_background = false; // play audio without opening the player window
    // When replying, keep the person you're replying to mentioned up front and
    // append every other participant's @ at the end of the post instead. Off by
    // default (all mentions are prepended).
    bool reply_mentions_at_end = false;
    int auto_refresh_seconds = 60; // 0 = off; otherwise poll interval
    bool streaming_enabled = true; // real-time streaming (Mastodon)
    // Where the mobile apps (iOS/Android) place the timeline tab bar; the
    // desktop apps use a sidebar and ignore this. Bottom by default on Apple
    // (easier one-handed/VoiceOver reach on iPhone; macOS ignores it), top on
    // Android (matches its tab conventions).
#ifdef __APPLE__
    std::string tab_bar_position = "bottom"; // "top" | "bottom"
#else
    std::string tab_bar_position = "top"; // "top" | "bottom"
#endif
    // The active movement units, in cycle/rotor order. Normalized on load:
    // saved order first, any catalog unit not saved is appended enabled.
    struct MovementUnitPref {
        std::string unit; // MovementUnit::key()
        bool enabled = true;
    };
    std::vector<MovementUnitPref> movement_units;
    // The per-post actions offered on mobile (VoiceOver/TalkBack action rotor +
    // long-press menu), in display order. Normalized on load like movement
    // units: saved order first, any catalog action not saved appended enabled.
    // Desktop uses full menus and ignores this.
    struct PostActionPref {
        std::string action; // a post_action_catalog() key
        bool enabled = true;
    };
    std::vector<PostActionPref> post_actions;
    // Invisible interface (Windows): control the client from any window.
    std::string invisible_mode = "off";       // "off" | "hotkey" | "keyhook" | "layer"
    std::string invisible_keymap = "default";  // active keymap name
    std::string invisible_layer_key = "control+win+space"; // combo that toggles the layer
    bool invisible_repeat_at_edge = true; // re-speak the item when you bump a timeline edge
    bool window_shown = true;                  // remembered across restarts (ToggleWindow)
    std::string update_branch = "stable";      // "stable" (versioned) | "latest" (rolling)
    bool check_updates_on_startup = true;      // quietly check on launch
    present::SpeechSettings speech = present::SpeechSettings::defaults();
    present::TextPresentation text; // content-warning / demojify / mention-collapse

    static constexpr int kFetchPagesMin = 1;
    static constexpr int kFetchPagesMax = 10;
    static constexpr int kCacheLimitMin = 0; // 0 disables caching (clears cache files)
    static constexpr int kCacheLimitMax = 20000;
    static constexpr int kMaxMentionsMin = 0;
    static constexpr int kMaxMentionsMax = 20;
    // Auto-refresh choices offered in Settings (seconds); 0 = Off. Mac parity.
    static constexpr int kAutoRefreshOptions[] = {0, 30, 60, 120, 300};
};

// The canonical set of per-post actions, in default display order (View Media
// and Open Links first). One shared vocabulary: the interact / secondary-
// interact settings each pick one key, and the mobile action list toggles /
// orders many. `key` is the stable settings token; `label` is the base UI name
// (toggle actions like Boost show a state-dependent name in the app).
struct PostActionDef {
    std::string key;
    std::string label;
};
const std::vector<PostActionDef>& post_action_catalog();

} // namespace fastsm::store
