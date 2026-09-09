#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtk/gtk.h>
#include <nlohmann/json.hpp>

#include "fastsm/capi/fastsm_core.h"
#include "fastsm/speech/speaker.hpp"

#include "invisible_hotkeys.hpp"
#include "keymap_manager_dialog.hpp"
#include "media_player.hpp"

namespace fastsmgtk {

// One rendered row. `text` is the fully-composed spoken label from the core —
// the UI never assembles post text.
struct Row {
    std::string id;
    std::string text;
    std::string account_id;
    std::string acct;
    std::string group_actors;
    bool favorited = false;
    bool boosted = false;
    bool bookmarked = false;
    bool muted = false;
    bool is_mine = false;
    bool gap_after = false;
    bool follow_request = false;
};

struct Timeline {
    std::string kind;
    std::string title;
    std::string selected_id; // UI-authoritative reading position
    bool dismissable = false;
    bool pinned = false;
    bool muted = false;
    bool auto_read = false;
    bool user_list = false;
    bool enter_opens_thread = false;
    bool reversed = false;
    std::vector<Row> rows;
};

// The main window: timelines pane + posts list, menu bar, keyboard handling,
// command dispatch and event rendering. The GTK mirror of
// windows/src/main_window.cpp (trimmed to the current Linux feature set).
class MainWindow {
public:
    MainWindow();

    void set_core(fastsm_core* core) { core_ = core; }
    void set_speaker(fastsm::speech::Speaker* speaker) { speaker_ = speaker; }
    GtkWidget* window() const { return window_; }

    // C-ABI event sink; marshals onto the GTK main loop via g_idle_add.
    static void event_sink(void* user, const char* json, size_t len);
    void on_event(const std::string& json);

private:
    // --- construction
    void build_menu();
    void build_views();

    // --- helpers
    Timeline* current();
    int selected_row();
    std::string selected_id();
    void dispatch_cmd(const nlohmann::json& cmd);
    void announce(const std::string& message);
    bool confirm(const std::string& text, const std::string& title);
    void open_url(const std::string& url);

    // --- list plumbing
    void populate_timelines_list();
    void bind_current_to_view();
    // A content-only update to the visible timeline (relative timestamps ticking
    // over, counts changing) whose row ids are unchanged: rewrites just the
    // changed cells' text in place and leaves the cursor alone, so Orca doesn't
    // re-announce the focused row. Returns false (and changes nothing) when the
    // ids differ, so the caller falls back to a full rebind. old_rows is the row
    // set the store currently shows; new rows are already in the timeline.
    bool refresh_current_rows_text(const std::vector<Row>& old_rows);
    void set_posts_cursor(int row);
    int index_of_id(const Timeline& tl, const std::string& id) const;
    void maybe_load_older(int row);
    void first_letter_nav(gunichar ch);

    // --- actions
    void compose(const std::string& mode);
    void do_enter_post_action();
    void do_secondary_post_action();
    void run_post_action(const std::string& action);
    void do_follow_request_action(const Row& r); // accept/reject a follow request (Enter)
    void do_enter_user_action();                 // configurable Enter on user-list rows
    void show_user_actions();                    // user-row actions menu (Accept/Reject on Follow Requests)
    std::vector<std::string> selected_user_row_ids(); // the multi-selection, else the focused row
    void run_user_action(const std::string& action, const std::vector<std::string>& row_ids);
    void do_find();
    void do_find_next();
    void do_find_prev();
    void find_from(int start_row, int dir);
    std::optional<std::string> prompt_text(const std::string& title, const std::string& label,
                                           const std::string& prefill = {});
    std::optional<std::string> pick_mention(const std::string& partial);
    void ev_user_suggestions(const nlohmann::json& e);
    void add_tray_icon();
    void do_hide_window();
    void surface_window();
    void ev_media_open(const nlohmann::json& e);
    void ev_media_picker(const nlohmann::json& e);
    void play_media_background(const std::string& url, const std::string& title);
    void stop_media();
    void do_boost();
    void do_favorite();
    void do_bookmark();
    void do_delete_post();
    void do_load_older();
    void do_add_account();
    void restore_selection(const std::string& id);

    // --- event renderers
    void ev_compose_context(const nlohmann::json& e);
    void ev_timelines_changed(const nlohmann::json& e);
    void ev_timeline_updated(const nlohmann::json& e);
    void ev_settings(const nlohmann::json& e);
    void ev_spawnable(const nlohmann::json& e);
    void ev_copy(const nlohmann::json& e);
    void ev_url_picker(const nlohmann::json& e);
    void ev_confirm(const nlohmann::json& e); // core-composed yes/no, re-dispatches on yes
    void ev_user_picker(const nlohmann::json& e);
    void ev_post_info(const nlohmann::json& e);
    void ev_user_profile(const nlohmann::json& e);
    void ev_account_settings(const nlohmann::json& e);
    void ev_lists(const nlohmann::json& e);
    void ev_user_lists(const nlohmann::json& e);
    void ev_followed_hashtags(const nlohmann::json& e);
    void ev_trending_hashtags(const nlohmann::json& e);
    void ev_alias_prompt(const nlohmann::json& e);
    void ev_aliases_list(const nlohmann::json& e);
    void ev_client_filter(const nlohmann::json& e);
    void ev_server_filters(const nlohmann::json& e);
    void ev_profile_editor(const nlohmann::json& e);
    bool run_edit_filter_dialog(nlohmann::json& filter);
    void apply_invisible();
    void ev_keymap(const nlohmann::json& e);
    void ev_action_catalog(const nlohmann::json& e);
    void open_keymap_manager();
    void ev_invisible_ui_action(const nlohmann::json& e);
    void ev_follow_request_prompt(const nlohmann::json& e); // core-driven accept/reject choice
    void do_settings();
    void ev_hashtag_prompt(const nlohmann::json& e);
    void ev_hashtag_timeline_picker(const nlohmann::json& e); // pick a post's hashtag to open

    // --- signal handlers
    static gboolean on_posts_key(GtkWidget* widget, GdkEventKey* event, gpointer user);
    static void on_posts_cursor_changed(GtkTreeView* view, gpointer user);
    static void on_timelines_cursor_changed(GtkTreeView* view, gpointer user);

    fastsm_core* core_ = nullptr;
    fastsm::speech::Speaker* speaker_ = nullptr;

    GtkWidget* window_ = nullptr;
    GtkWidget* menu_bar_ = nullptr;
    GtkWidget* timelines_view_ = nullptr;
    GtkWidget* posts_view_ = nullptr;
    GtkListStore* timelines_store_ = nullptr;
    GtkListStore* posts_store_ = nullptr;

    std::vector<Timeline> timelines_;
    int current_ = 0;
    std::string current_account_;
    nlohmann::json settings_ = nlohmann::json::object();
    std::vector<std::string> soundpacks_;
    std::vector<std::string> sound_devices_; // mixer output devices, from the settings event
    bool load_pending_ = false;
    bool updating_ = false; // suppress cursor-changed while (re)binding models
    std::string find_query_;
    InvisibleHotkeys invisible_;
    std::string invisible_mode_ = "off";
    // The @-mention picker while it is open (fed by user_suggestions events).
    GtkWidget* mention_dialog_ = nullptr;
    GtkWidget* mention_query_ = nullptr;
    GtkListStore* mention_store_ = nullptr;
    GtkWidget* mention_view_ = nullptr;
    std::vector<std::string> mention_handles_;
    GtkStatusIcon* tray_ = nullptr; // deprecated API, but the only X11 tray
    std::unique_ptr<MediaPlayback> media_bg_; // background audio (no window)
    guint media_bg_timer_ = 0;                // auto-clear when it ends
    std::vector<KmAction> action_catalog_;    // loaded once for the Keyboard Manager
    KeymapManagerDialog* keymap_mgr_ = nullptr; // tracked while open (event forwarding)
};

} // namespace fastsmgtk
