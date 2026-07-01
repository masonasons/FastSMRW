#pragma once

#include <map>
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

// The main application window: a left "Timelines" list and a right virtual
// "Timeline" posts list. It is a pure client of the core's C ABI: it dispatches
// JSON commands and renders JSON events; it holds no engine logic, only a cache
// of what to draw (so a Swift/Kotlin UI would mirror this exactly).
class MainWindow {
public:
    explicit MainWindow(HINSTANCE inst);

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
        bool gap_after = false; // unloaded posts follow this row (auto-fill)
    };
    struct Timeline {
        std::wstring title;
        std::string kind;
        bool dismissable = false;
        bool user_list = false; // rows are users: multi-select + batch actions
        std::vector<Row> rows;
        std::string selected_id; // UI-authoritative remembered position
    };

    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT, WPARAM, LPARAM);
    // Subclass of the posts ListView: restores the remembered row when focus
    // returns (e.g. after a modal dialog) so it never snaps to the top.
    static LRESULT CALLBACK ViewProcStatic(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    void create_children();
    void layout();
    void populate_timelines_list();
    void bind_current_to_view(bool force = false);
    void maybe_load_older(int row); // pull the next page when near the bottom
    Timeline* current();
    int index_of_id(const Timeline& tl, const std::string& id) const;
    int selected_row() const;
    std::string selected_id();
    void restore_selection(const std::string& id);
    void on_view_keydown(int vk);
    void handle_command(int id);
    void announce(const std::string& message);

    // Commands.
    void dispatch_cmd(const nlohmann::json& cmd);
    void do_boost();
    void do_favorite();
    void compose(const char* mode); // dispatch compose_context for the selection
    void do_post_info();
    void show_user_actions(); // batch follow/mute/block on a user list
    void do_new_timeline();
    void do_add_account();
    void do_settings();
    void do_find();      // Ctrl+F: prompt for text, then find in the current timeline
    void do_find_next(); // F3 / invisible action: next match of the last query
    void do_find_prev(); // Shift+F3 / invisible action: previous match of the last query
    void find_from(int start_row, int dir); // search rows from start_row (dir +1/-1), wrapping
    void about();

    // Events.
    void on_event(const std::string& json);
    void ev_timelines_changed(const nlohmann::json& e);
    void ev_timeline_updated(const nlohmann::json& e);
    void ev_settings(const nlohmann::json& e);
    void ev_compose_context(const nlohmann::json& e);
    void ev_spawnable(const nlohmann::json& e);
    void ev_post_info(const nlohmann::json& e);
    void ev_user_profile(const nlohmann::json& e);
    void ev_user_picker(const nlohmann::json& e);
    void ev_keymap(const nlohmann::json& e);
    void ev_layer_keymap(const nlohmann::json& e);
    void ev_action_catalog(const nlohmann::json& e);
    void ev_invisible_ui_action(const nlohmann::json& e);
    void ev_client_filter(const nlohmann::json& e);  // open the per-timeline client filter dialog
    void ev_server_filters(const nlohmann::json& e); // open / refresh the server filters manager
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
    std::string invisible_mode_ = "off";
    std::string pending_update_url_;        // FastSMRW.zip URL from the last check
    bool startup_update_checked_ = false;   // guard the one-shot startup check
    std::map<std::string, std::string> invisible_bindings_; // key -> action
    std::vector<KmAction> action_catalog_;                  // for the Keyboard Manager
    KeymapManagerDialog* keymap_mgr_ = nullptr;             // non-null while its modal is open
    ServerFiltersDialog* server_filters_mgr_ = nullptr;     // non-null while its modal is open
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
    std::vector<std::string> spawnable_kinds_; // parallels the New Timeline dialog
};

} // namespace fastsmui
