#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include <nlohmann/json.hpp>

#include "fastsm/capi/fastsm_core.h"
#include "fastsm/speech/speaker.hpp"

#include "compose_dialog.hpp"
#include "invisible_hotkeys.hpp"
#include "invisible_keyhook.hpp"
#include "keymap_manager_dialog.hpp"

namespace fastsmui {

class ServerFiltersDialog; // manager modal; non-null while open (like keymap_mgr_)
class ListsManagerDialog;  // Lists manager modal; non-null while open
class FollowedHashtagsDialog; // Followed Hashtags manager modal; non-null while open
class MediaPlayback;       // windowless background audio playback (media_player_window.hpp)

// The main application window: a left "Timelines" list and a right virtual
// "Timeline" posts list. It is a pure client of the core's C ABI: it dispatches
// JSON commands and renders JSON events; it holds no engine logic, only a cache
// of what to draw (so a Swift/Kotlin UI would mirror this exactly).
class MainWindow {
public:
    explicit MainWindow(HINSTANCE inst);
    ~MainWindow(); // defined in the .cpp (media_bg_ needs the complete MediaPlayback)

    bool create();
    HWND hwnd() const { return hwnd_; }
    HACCEL accel() const { return accel_; }
    void set_core(fastsm_core* core) { core_ = core; }
    void set_speaker(fastsm::speech::Speaker* speaker) { speaker_ = speaker; }
    void cycle_focus(); // Tab between the two panes

    // Event sink registered with the core (called on the core thread); marshals
    // the JSON onto the UI thread via WM_APP_EVENT.
    static void event_sink(void* user, const char* event_json, size_t len);

private:
    // A cached, render-ready row (the text is the core-composed speech label).
    struct Row {
        std::string id;
        std::wstring text;
        bool favorited = false;
        bool boosted = false;
        bool is_mine = false;        // your own post (can be deleted)
        bool gap_after = false;      // unloaded posts follow this row (auto-fill)
        bool follow_request = false; // a follow-request notification (Enter accepts/rejects)
        std::string account_id;      // requester's account id (follow-request rows)
        std::string acct;            // requester's handle (follow-request rows)
    };
    struct Timeline {
        std::wstring title;
        std::string kind;
        bool dismissable = false;
        bool pinned = false;    // user pinned this tab (locked from dismissal)
        bool user_list = false; // rows are users: multi-select + batch actions
        bool reversed = false;  // oldest at top, newest at bottom (load older from the top)
        std::vector<Row> rows;
        std::string selected_id; // UI-authoritative remembered position
    };

    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT, WPARAM, LPARAM);
    // Subclass of the posts ListView: restores the remembered row when focus
    // returns (e.g. after a modal dialog) so it never snaps to the top.
    static LRESULT CALLBACK ViewProcStatic(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK TimelinesListProcStatic(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    void create_children();
    void layout();
    void populate_timelines_list();
    void update_pin_menu(); // reflect the current tab's pin state on the Pin menu item
    void bind_current_to_view(bool force = false);
    void maybe_load_older(int row); // pull the next page when near the bottom (if auto is on)
    void do_load_older();           // manually pull the next page (Period / Timeline menu)
    Timeline* current();
    int index_of_id(const Timeline& tl, const std::string& id) const;
    int selected_row() const;
    std::string selected_id();
    void restore_selection(const std::string& id);
    void on_view_keydown(int vk);
    void handle_command(int id);
    void announce(const std::string& message);
    // Show a popup menu (TPM_RETURNCMD) at pt and return the chosen id. When the
    // window is hidden (invisible interface), briefly show it and take the
    // foreground first, else focus never reaches the menu (FastPlay pattern).
    int track_popup(HMENU menu, POINT pt);

    // Focus handling for modal dialogs opened from the invisible interface. The
    // main window may be hidden or merely not in the foreground; a plain modal
    // owned by it then never gets focus, and on close it reactivates the hidden
    // window (surfacing it / stealing screen-reader focus). enter_modal() records
    // the prior foreground window, surfaces + foregrounds the main window so the
    // dialog takes focus; leave_modal() re-hides if we unhid and hands focus back
    // to where it was. Same idea as track_popup, for DialogBox dialogs.
    struct ModalGuard {
        HWND prior = nullptr;
        bool was_hidden = false;
    };
    ModalGuard enter_modal();
    void leave_modal(const ModalGuard& g);

    // Commands.
    void dispatch_cmd(const nlohmann::json& cmd);
    void do_boost();
    void do_favorite();
    void do_delete_post(); // delete the focused post if it's yours (confirms first)
    void update_menu_checks(HMENU menu); // check Boost/Favorite for the focused post
    void compose(const char* mode); // dispatch compose_context for the selection
    void do_post_info();
    void show_user_actions(); // batch follow/mute/block on a user list
    void do_follow_request_action(const Row& r); // accept/reject a follow request (Enter)
    void do_enter_post_action();                 // Enter on a post (configurable)
    void do_enter_user_action();                 // Enter on a user (configurable)
    void do_secondary_post_action();             // Shift+Enter on a post (configurable)
    void play_media_background(const std::wstring& url, const std::wstring& title);
    void stop_media(); // stop windowless background audio
    void do_new_timeline();
    void do_add_account();
    void do_settings();
    void do_find();      // Ctrl+F: prompt for text, then find in the current timeline
    void do_find_next(); // F3 / invisible action: next match of the last query
    void do_find_prev(); // Shift+F3 / invisible action: previous match of the last query
    void find_from(int start_row, int dir); // search rows from start_row (dir +1/-1), wrapping
    void first_letter_nav(wchar_t letter);  // Shift+letter: next post whose spoken text starts with it
    void about();

    // Events.
    void on_event(const std::string& json);
    void ev_timelines_changed(const nlohmann::json& e);
    void ev_timeline_updated(const nlohmann::json& e);
    void ev_settings(const nlohmann::json& e);
    void ev_account_settings(const nlohmann::json& e); // per-account soundpack dialog
    void ev_compose_context(const nlohmann::json& e);
    void ev_spawnable(const nlohmann::json& e);
    void ev_post_info(const nlohmann::json& e);
    void ev_user_profile(const nlohmann::json& e);
    void ev_user_picker(const nlohmann::json& e);
    void ev_user_suggestions(const nlohmann::json& e); // fill the open @-mention picker
    // Modal @-mention autocomplete: type a partial handle, live-search accounts,
    // insert the chosen @handle. Owned by the composer dialog; nullopt if cancelled.
    std::optional<std::string> pick_mention(HWND owner, const std::string& partial);
    std::string prompt_handle(); // modal "Type a handle…" box; "" if cancelled/empty
    void ev_url_picker(const nlohmann::json& e); // choose among links found in a post
    void ev_keymap(const nlohmann::json& e);
    void ev_layer_keymap(const nlohmann::json& e);
    void ev_action_catalog(const nlohmann::json& e);
    void ev_invisible_ui_action(const nlohmann::json& e);
    void ev_media_open(const nlohmann::json& e);   // stream audio in the in-app player
    void ev_media_picker(const nlohmann::json& e); // choose which media to play
    void ev_client_filter(const nlohmann::json& e);  // open the per-timeline client filter dialog
    void ev_server_filters(const nlohmann::json& e); // open / refresh the server filters manager
    void ev_user_lists(const nlohmann::json& e);     // open the add/remove-from-lists checklist
    void ev_lists(const nlohmann::json& e);          // forward into the open Lists manager
    void ev_hashtag_prompt(const nlohmann::json& e);    // prompt to follow a hashtag
    void ev_followed_hashtags(const nlohmann::json& e); // open / refresh Followed Hashtags manager
    void ev_update_status(const nlohmann::json& e); // check result -> prompt / announce
    void ev_update_ready(const nlohmann::json& e);  // downloaded -> swap + restart
    // Apply the current invisible-interface mode (from settings_): (re)load the
    // keymap for hotkey mode, or tear the global hotkeys down when off.
    void apply_invisible();
    // Install the driver for the current invisible mode from invisible_bindings_.
    void install_active_driver();
    // Leave the layer (e.g. a dialog opened); restores the base driver if the
    // layer was called up on demand as an overlay from hotkey/keyhook mode.
    void leave_layer();
    void open_keymap_manager(HWND parent);

    HINSTANCE inst_;
    fastsm_core* core_ = nullptr;
    fastsm::speech::Speaker* speaker_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND timelines_list_ = nullptr;
    HWND timeline_view_ = nullptr;
    HACCEL accel_ = nullptr;
    std::wstring scratch_;                   // backing store for virtual-list item text
    std::vector<std::string> rendered_ids_;  // last rendered row ids (reload guard)
    bool updating_selection_ = false;
    bool load_pending_ = false; // a load_older is in flight (avoid spamming)

    // Invisible interface (global hotkeys). Bindings come from the core's keymap
    // event; the driver registers them and maps WM_HOTKEY ids back to actions.
    HotkeyDriver hotkey_driver_;   // mode "hotkey" (RegisterHotKey)
    KeyhookDriver keyhook_driver_; // mode "keyhook" (WH_KEYBOARD_LL)
    std::unique_ptr<MediaPlayback> media_bg_; // windowless background audio playback
    std::string invisible_mode_ = "off";
    bool installed_mode() const;            // installed.txt marker present (vs portable)
    std::string pending_update_url_;        // FastSMRW.zip URL from the last check
    std::string pending_installer_url_;     // FastSMRWInstaller.exe URL from the last check
    bool startup_update_checked_ = false;   // guard the one-shot startup check
    std::map<std::string, std::string> invisible_bindings_; // key -> action
    std::vector<KmAction> action_catalog_;                  // for the Keyboard Manager
    KeymapManagerDialog* keymap_mgr_ = nullptr;             // non-null while its modal is open
    ServerFiltersDialog* server_filters_mgr_ = nullptr;     // non-null while its modal is open
    ListsManagerDialog* lists_mgr_ = nullptr;               // non-null while its modal is open
    FollowedHashtagsDialog* followed_tags_mgr_ = nullptr;   // non-null while its modal is open
    HWND mention_dlg_ = nullptr;                            // @-mention picker, while open (nested modal)
    std::string layer_enter_message_ = "FastSM layer";     // spoken when the layer opens
    std::string layer_help_message_;                        // spoken on "/" in the layer
    std::map<std::string, std::string> layer_bindings_;     // cached bare-key layer map
    std::string layer_activation_ = "control+win+space";    // cached layer toggle combo
    bool overlay_layer_ = false; // the layer was called up on demand (EnterLayer)

    std::vector<Timeline> timelines_;
    std::string current_account_; // which account timelines_ belongs to (per event)
    std::wstring find_query_;      // last Find text (for Find Next)
    int current_ = 0;
    nlohmann::json settings_ = nlohmann::json::object(); // cached settings object
    std::vector<std::string> soundpacks_;
    std::vector<std::string> spawnable_kinds_;  // parallels the New Timeline dialog
    std::vector<std::string> spawnable_params_; // per-entry param (e.g. a list id), echoed on spawn
};

} // namespace fastsmui
