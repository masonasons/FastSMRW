#include "main_window.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "fastsm/fastsm.hpp"
#include "fastsm/store/settings_json.hpp"

#include "compose_dialog.hpp"
#include "post_info_dialog.hpp"
#include "report_dialog.hpp"
#include "settings_dialog.hpp"
#include "user_profile_dialog.hpp"

using nlohmann::json;

namespace fastsmgtk {

namespace {

// Defined further down (with the manager-dialog handlers).
std::optional<std::pair<int, int>>
run_list_manager(GtkWindow* parent, const std::string& title,
                 const std::vector<std::string>& rows, const std::vector<std::string>& buttons);

// Convenience for wiring a menu item with an optional accelerator.
GtkWidget* add_item(GtkWidget* menu, const char* label, GtkAccelGroup* accel, guint key,
                    GdkModifierType mods, GCallback cb, gpointer user) {
    GtkWidget* item = gtk_menu_item_new_with_mnemonic(label);
    if (key)
        gtk_widget_add_accelerator(item, "activate", accel, key, mods, GTK_ACCEL_VISIBLE);
    g_signal_connect(item, "activate", cb, user);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

GtkWidget* make_list_view(GtkListStore* store, const char* accessible_name) {
    GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* column =
        gtk_tree_view_column_new_with_attributes("", renderer, "text", 0, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
    // The row text is what Orca reads; the view's name tells it which pane.
    atk_object_set_name(gtk_widget_get_accessible(view), accessible_name);
    return view;
}

} // namespace

MainWindow::MainWindow() {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "FastSMRW");
    gtk_window_set_default_size(GTK_WINDOW(window_), 900, 600);
    g_signal_connect(window_, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) { gtk_main_quit(); }),
                     nullptr);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), vbox);

    build_menu();
    gtk_box_pack_start(GTK_BOX(vbox), menu_bar_, FALSE, FALSE, 0);
    add_tray_icon();

    build_views();
    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), 220);

    GtkWidget* left_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(left_scroll), timelines_view_);
    GtkWidget* right_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(right_scroll), posts_view_);
    gtk_paned_pack1(GTK_PANED(paned), left_scroll, FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), right_scroll, TRUE, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);
}

void MainWindow::build_menu() {
    GtkAccelGroup* accel = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window_), accel);
    menu_bar_ = gtk_menu_bar_new();

    auto ctrl = GDK_CONTROL_MASK;
    auto ctrl_shift = static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK);
    auto none = static_cast<GdkModifierType>(0);
    auto sep = [](GtkWidget* menu) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    };
    auto cb = [](void (*fn)(GtkMenuItem*, gpointer)) { return G_CALLBACK(fn); };
    (void)cb;
    // Small helper: menu item that dispatches a fixed command on the selected
    // post. Cuts the boilerplate for the Status menu.
    auto on_selected = [&](GtkWidget* menu, const char* label, guint key, GdkModifierType mods,
                           const char* command, bool pick) {
        GtkWidget* item = add_item(menu, label, accel, key, mods,
                                   G_CALLBACK(+[](GtkMenuItem* mi, gpointer u) {
                                       auto* self = static_cast<MainWindow*>(u);
                                       const std::string id = self->selected_id();
                                       if (id.empty())
                                           return;
                                       const auto* command = static_cast<const char*>(
                                           g_object_get_data(G_OBJECT(mi), "fastsm-cmd"));
                                       json cmd(json::object());
                                       cmd["cmd"] = command;
                                       cmd["id"] = id;
                                       if (g_object_get_data(G_OBJECT(mi), "fastsm-pick"))
                                           cmd["pick"] = true;
                                       self->dispatch_cmd(cmd);
                                   }),
                                   this);
        g_object_set_data(G_OBJECT(item), "fastsm-cmd", const_cast<char*>(command));
        if (pick)
            g_object_set_data(G_OBJECT(item), "fastsm-pick", GINT_TO_POINTER(1));
    };
    auto plain = [&](GtkWidget* menu, const char* label, guint key, GdkModifierType mods,
                     const char* command) {
        GtkWidget* item = add_item(menu, label, accel, key, mods,
                                   G_CALLBACK(+[](GtkMenuItem* mi, gpointer u) {
                                       const auto* command = static_cast<const char*>(
                                           g_object_get_data(G_OBJECT(mi), "fastsm-cmd"));
                                       static_cast<MainWindow*>(u)->dispatch_cmd(
                                           {{"cmd", command}});
                                   }),
                                   this);
        g_object_set_data(G_OBJECT(item), "fastsm-cmd", const_cast<char*>(command));
    };
    auto spawn = [&](GtkWidget* menu, const char* label, const char* kind) {
        GtkWidget* item = add_item(menu, label, accel, 0, none,
                                   G_CALLBACK(+[](GtkMenuItem* mi, gpointer u) {
                                       const auto* kind = static_cast<const char*>(
                                           g_object_get_data(G_OBJECT(mi), "fastsm-kind"));
                                       static_cast<MainWindow*>(u)->dispatch_cmd(
                                           {{"cmd", "spawn_timeline"}, {"kind", kind}});
                                   }),
                                   this);
        g_object_set_data(G_OBJECT(item), "fastsm-kind", const_cast<char*>(kind));
    };

    // --- Application (mirrors windows/src/main_window.cpp build_menu) ---
    GtkWidget* app_menu = gtk_menu_new();
    add_item(app_menu, "_About FastSMRW", accel, 0, none,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 auto* self = static_cast<MainWindow*>(u);
                 GtkWidget* d = gtk_message_dialog_new(
                     GTK_WINDOW(self->window_), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                     GTK_BUTTONS_OK, "FastSMRW\nVersion %s\n\nA fast, accessible "
                                     "Mastodon/Bluesky client.",
                     fastsm::version());
                 gtk_window_set_title(GTK_WINDOW(d), "About FastSMRW");
                 gtk_dialog_run(GTK_DIALOG(d));
                 gtk_widget_destroy(d);
             }),
             this);
    add_item(app_menu, "_Help (User Guide)", accel, GDK_KEY_F1, none,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->open_url(
                     "https://github.com/masonasons/FastSMRW/blob/main/README-Linux.md");
             }),
             this);
    sep(app_menu);
    add_item(app_menu, "_Settings…", accel, GDK_KEY_comma, ctrl,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->do_settings();
             }),
             this);
    add_item(app_menu, "_Keyboard Manager…", accel, 0, none,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->open_keymap_manager();
             }),
             this);
    plain(app_menu, "Ser_ver Filters…", 0, none, "list_server_filters");
    add_item(app_menu, "Check for _Updates…", accel, GDK_KEY_F1, GDK_SHIFT_MASK,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 auto* self = static_cast<MainWindow*>(u);
                 self->announce("Checking for updates…");
                 self->dispatch_cmd({{"cmd", "check_for_update"}, {"silent", false}});
             }),
             this);
    sep(app_menu);
    add_item(app_menu, "S_top Media Playback", accel, GDK_KEY_s, ctrl,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->stop_media();
             }),
             this);
    sep(app_menu);
    add_item(app_menu, "_Hide Window", accel, GDK_KEY_h, ctrl,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->do_hide_window();
             }),
             this);
    add_item(app_menu, "_Quit FastSMRW", accel, GDK_KEY_q, ctrl,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 gtk_widget_destroy(static_cast<MainWindow*>(u)->window_);
             }),
             this);
    GtkWidget* app_root = gtk_menu_item_new_with_mnemonic("_Application");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(app_root), app_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar_), app_root);

    // --- Me ---
    GtkWidget* me_menu = gtk_menu_new();
    plain(me_menu, "_Edit Profile…", 0, none, "open_profile_editor");
    spawn(me_menu, "View My _Followers", "my_followers");
    spawn(me_menu, "View My Follo_wing", "my_following");
    plain(me_menu, "_Lists…", 0, none, "list_lists");
    spawn(me_menu, "View _Muted Users", "mutes");
    spawn(me_menu, "View _Blocked Users", "blocks");
    spawn(me_menu, "View Follow _Requests", "follow_requests");
    plain(me_menu, "Followed Hasht_ags…", 0, none, "list_followed_hashtags");
    plain(me_menu, "_Trending Hashtags…", 0, none, "list_trending_hashtags");
    add_item(me_menu, "User A_nalysis…", accel, 0, none,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 auto* self = static_cast<MainWindow*>(u);
                 auto choice = run_list_manager(
                     GTK_WINDOW(self->window_), "User Analysis",
                     {"People you follow who don't follow you back",
                      "Followers you don't follow back", "Mutuals (you follow each other)"},
                     {"_Analyze"});
                 if (!choice || choice->second < 0 || choice->second > 2)
                     return;
                 const char* category = choice->second == 0   ? "not_following_back"
                                        : choice->second == 1 ? "no_followback"
                                                              : "mutuals";
                 self->dispatch_cmd({{"cmd", "analyze_users"}, {"category", category}});
             }),
             this);
    plain(me_menu, "User A_liases…", 0, none, "list_aliases");
    GtkWidget* me_root = gtk_menu_item_new_with_mnemonic("_Me");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(me_root), me_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar_), me_root);

    // --- Status ---
    GtkWidget* status_menu = gtk_menu_new();
    add_item(status_menu, "_New Post", accel, GDK_KEY_n, ctrl,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->compose("new");
             }),
             this);
    sep(status_menu);
    add_item(status_menu, "_Reply", accel, 0, none,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->compose("reply");
             }),
             this);
    add_item(status_menu, "_Boost", accel, 0, none,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->do_boost();
             }),
             this);
    add_item(status_menu, "_Favorite", accel, 0, none,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->do_favorite();
             }),
             this);
    add_item(status_menu, "Book_mark", accel, 0, none,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->do_bookmark();
             }),
             this);
    on_selected(status_menu, "_Copy", GDK_KEY_c, ctrl, "copy", false);
    add_item(status_menu, "_Quote", accel, 0, none,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->compose("quote");
             }),
             this);
    on_selected(status_menu, "Post _Info…", 0, none, "post_info", false);
    on_selected(status_menu, "View _Thread", 0, none, "open_thread", false);
    on_selected(status_menu, "M_ute Conversation", 0, none, "toggle_mute_conversation", false);
    sep(status_menu);
    on_selected(status_menu, "Open _User Timeline", 0, none, "open_user_timeline", true);
    on_selected(status_menu, "Open User _Profile", GDK_KEY_u, ctrl, "open_user_profile", true);
    on_selected(status_menu, "Sen_d a Message…", 0, none, "message_user", true);
    on_selected(status_menu, "_Speak User", GDK_KEY_semicolon, ctrl, "speak_user", false);
    // Shifted punctuation: GTK matches the translated keyval, so Shift+";" is
    // ":" (and Shift+"," is "<" below) — register those, not the base keys.
    on_selected(status_menu, "Speak Referenced Repl_y", GDK_KEY_colon, ctrl_shift, "speak_reply",
                false);
    on_selected(status_menu, "Fo_llow / Unfollow", GDK_KEY_l, ctrl, "follow_toggle", true);
    add_item(status_menu, "Add / Edit _Alias…", accel, GDK_KEY_n, ctrl_shift,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 auto* self = static_cast<MainWindow*>(u);
                 const std::string id = self->selected_id();
                 if (!id.empty())
                     self->dispatch_cmd({{"cmd", "begin_alias"}, {"id", id}, {"pick", true}});
             }),
             this);
    sep(status_menu);
    on_selected(status_menu, "Open Lin_ks", GDK_KEY_o, ctrl, "open_post_links", false);
    on_selected(status_menu, "Open in Br_owser", 0, none, "open_status", false);
    GtkWidget* status_root = gtk_menu_item_new_with_mnemonic("_Status");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(status_root), status_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar_), status_root);

    // --- Timeline ---
    GtkWidget* tl_menu = gtk_menu_new();
    plain(tl_menu, "_New Timeline…", GDK_KEY_t, ctrl, "get_spawnable");
    plain(tl_menu, "Re_fresh Timeline", GDK_KEY_r, ctrl, "refresh");
    plain(tl_menu, "_Pin Timeline", GDK_KEY_p, ctrl, "toggle_pin");
    plain(tl_menu, "_Mute Timeline Sounds", GDK_KEY_m, ctrl, "toggle_mute");
    plain(tl_menu, "Auto-_read New Posts", 0, none, "toggle_auto_read");
    plain(tl_menu, "_Close Timeline", GDK_KEY_w, ctrl, "close_timeline");
    add_item(tl_menu, "Load _Older Posts", accel, 0, none,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->do_load_older();
             }),
             this);
    sep(tl_menu);
    add_item(tl_menu, "_Find…", accel, GDK_KEY_f, ctrl,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) { static_cast<MainWindow*>(u)->do_find(); }),
             this);
    add_item(tl_menu, "Find _Next", accel, GDK_KEY_F3, none,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->do_find_next();
             }),
             this);
    add_item(tl_menu, "Find _Previous", accel, GDK_KEY_F3, GDK_SHIFT_MASK,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->do_find_prev();
             }),
             this);
    sep(tl_menu);
    plain(tl_menu, "C_lient Filters…", GDK_KEY_f, ctrl_shift, "get_client_filter");
    sep(tl_menu);
    add_item(tl_menu, "Clea_r Timeline", accel, GDK_KEY_Delete, ctrl,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 auto* self = static_cast<MainWindow*>(u);
                 if (!self->settings_.value("confirm_clear_timeline", true) ||
                     self->confirm("Clear this timeline? This removes the loaded posts and its "
                                   "cache.",
                                   "Clear Timeline"))
                     self->dispatch_cmd({{"cmd", "clear_timeline"}});
             }),
             this);
    add_item(tl_menu, "Clear _All Timelines", accel, GDK_KEY_Delete, ctrl_shift,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 auto* self = static_cast<MainWindow*>(u);
                 if (!self->settings_.value("confirm_clear_timeline", true) ||
                     self->confirm("Clear every timeline? This removes all loaded posts and "
                                   "caches.",
                                   "Clear All Timelines"))
                     self->dispatch_cmd({{"cmd", "clear_all_timelines"}});
             }),
             this);
    plain(tl_menu, "_Undo Navigation", GDK_KEY_z, ctrl, "go_back");
    sep(tl_menu);
    for (int i = 1; i <= 9; ++i) {
        const std::string label = "Go to Timeline _" + std::to_string(i);
        GtkWidget* item = add_item(tl_menu, label.c_str(), accel,
                                   static_cast<guint>(GDK_KEY_0 + i), ctrl,
                                   G_CALLBACK(+[](GtkMenuItem* mi, gpointer u) {
                                       const int number = GPOINTER_TO_INT(
                                           g_object_get_data(G_OBJECT(mi), "fastsm-number"));
                                       static_cast<MainWindow*>(u)->dispatch_cmd(
                                           {{"cmd", "select_timeline"}, {"number", number}});
                                   }),
                                   this);
        g_object_set_data(G_OBJECT(item), "fastsm-number", GINT_TO_POINTER(i));
    }
    GtkWidget* tl_root = gtk_menu_item_new_with_mnemonic("_Timeline");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(tl_root), tl_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar_), tl_root);

    // --- Account ---
    GtkWidget* acct_menu = gtk_menu_new();
    add_item(acct_menu, "_Add Account…", accel, GDK_KEY_a, ctrl_shift,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->do_add_account();
             }),
             this);
    plain(acct_menu, "Account _Settings…", GDK_KEY_less, ctrl_shift, "get_account_settings");
    sep(acct_menu);
    add_item(acct_menu, "_Previous Account", accel, GDK_KEY_bracketleft, ctrl,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->dispatch_cmd(
                     {{"cmd", "select_account"}, {"dir", "prev"}});
             }),
             this);
    add_item(acct_menu, "_Next Account", accel, GDK_KEY_bracketright, ctrl,
             G_CALLBACK(+[](GtkMenuItem*, gpointer u) {
                 static_cast<MainWindow*>(u)->dispatch_cmd(
                     {{"cmd", "select_account"}, {"dir", "next"}});
             }),
             this);
    GtkWidget* acct_root = gtk_menu_item_new_with_mnemonic("A_ccount");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(acct_root), acct_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar_), acct_root);
}

void MainWindow::build_views() {
    timelines_store_ = gtk_list_store_new(1, G_TYPE_STRING);
    posts_store_ = gtk_list_store_new(1, G_TYPE_STRING);
    timelines_view_ = make_list_view(timelines_store_, "Timelines");
    posts_view_ = make_list_view(posts_store_, "Timeline");

    g_signal_connect(posts_view_, "key-press-event", G_CALLBACK(&MainWindow::on_posts_key), this);
    g_signal_connect(posts_view_, "cursor-changed",
                     G_CALLBACK(&MainWindow::on_posts_cursor_changed), this);
    g_signal_connect(timelines_view_, "cursor-changed",
                     G_CALLBACK(&MainWindow::on_timelines_cursor_changed), this);
    // Shift+Up/Down in the timelines list reorder the timelines. Swallow the
    // key so the list doesn't also move the cursor — the core re-emits the
    // list with the moved timeline reselected.
    g_signal_connect(timelines_view_, "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer u) -> gboolean {
                         const guint state =
                             event->state & gtk_accelerator_get_default_mod_mask();
                         if ((event->keyval != GDK_KEY_Up && event->keyval != GDK_KEY_Down) ||
                             state != GDK_SHIFT_MASK)
                             return FALSE;
                         static_cast<MainWindow*>(u)->dispatch_cmd(
                             {{"cmd", "reorder_timeline"},
                              {"dir", event->keyval == GDK_KEY_Up ? "up" : "down"}});
                         return TRUE;
                     }),
                     this);
}

// ---------------------------------------------------------------- helpers ---

Timeline* MainWindow::current() {
    if (current_ < 0 || current_ >= static_cast<int>(timelines_.size()))
        return nullptr;
    return &timelines_[static_cast<size_t>(current_)];
}

int MainWindow::selected_row() {
    GtkTreePath* path = nullptr;
    gtk_tree_view_get_cursor(GTK_TREE_VIEW(posts_view_), &path, nullptr);
    if (!path)
        return -1;
    const int row = gtk_tree_path_get_indices(path)[0];
    gtk_tree_path_free(path);
    return row;
}

std::string MainWindow::selected_id() {
    Timeline* tc = current();
    const int row = selected_row();
    if (!tc || row < 0 || row >= static_cast<int>(tc->rows.size()))
        return {};
    return tc->rows[static_cast<size_t>(row)].id;
}

void MainWindow::dispatch_cmd(const json& cmd) {
    if (!core_)
        return;
    const std::string s = cmd.dump();
    fastsm_core_dispatch(core_, s.c_str(), s.size());
}

void MainWindow::announce(const std::string& message) {
    if (speaker_ && !message.empty())
        speaker_->speak(message, /*interrupt=*/true);
}

bool MainWindow::confirm(const std::string& text, const std::string& title) {
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(window_), static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL),
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", text.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
    const int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_YES;
}

void MainWindow::open_url(const std::string& url) {
    if (url.empty())
        return;
    // Under WSL the user's real browser lives on the Windows side: prefer
    // wslview (wslu) when present, else Windows' own URL handler via rundll32
    // (safe with '&' in OAuth URLs — the URL is a single argv, no shell).
    // Elsewhere use the GTK handler.
    if (g_getenv("WSL_DISTRO_NAME")) {
        gchar* wslview[] = {const_cast<gchar*>("wslview"), const_cast<gchar*>(url.c_str()),
                            nullptr};
        if (g_spawn_async(nullptr, wslview, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr,
                          nullptr, nullptr))
            return;
        gchar* rundll[] = {const_cast<gchar*>("/mnt/c/Windows/System32/rundll32.exe"),
                           const_cast<gchar*>("url.dll,FileProtocolHandler"),
                           const_cast<gchar*>(url.c_str()), nullptr};
        if (g_spawn_async(nullptr, rundll, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr,
                          nullptr))
            return;
    }
    gtk_show_uri_on_window(GTK_WINDOW(window_), url.c_str(), GDK_CURRENT_TIME, nullptr);
}

// ----------------------------------------------------------- list binding ---

void MainWindow::populate_timelines_list() {
    updating_ = true;
    gtk_list_store_clear(timelines_store_);
    for (const auto& tl : timelines_) {
        GtkTreeIter iter;
        gtk_list_store_append(timelines_store_, &iter);
        gtk_list_store_set(timelines_store_, &iter, 0, tl.title.c_str(), -1);
    }
    if (!timelines_.empty()) {
        GtkTreePath* path = gtk_tree_path_new_from_indices(current_, -1);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(timelines_view_), path, nullptr, FALSE);
        gtk_tree_path_free(path);
    }
    updating_ = false;
}

void MainWindow::bind_current_to_view() {
    updating_ = true;
    gtk_list_store_clear(posts_store_);
    Timeline* tc = current();
    // User lists allow a multi-selection (Shift+arrows) so batch actions can act
    // on several users at once, like the Windows list view. Post lists stay
    // single-selection.
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(posts_view_)),
                                tc && tc->user_list ? GTK_SELECTION_MULTIPLE
                                                    : GTK_SELECTION_SINGLE);
    if (tc) {
        for (const auto& row : tc->rows) {
            GtkTreeIter iter;
            gtk_list_store_append(posts_store_, &iter);
            gtk_list_store_set(posts_store_, &iter, 0, row.text.c_str(), -1);
        }
        if (!tc->rows.empty()) {
            int target = 0;
            if (!tc->selected_id.empty())
                if (const int idx = index_of_id(*tc, tc->selected_id); idx >= 0)
                    target = idx;
            set_posts_cursor(target);
        }
    }
    updating_ = false;
}

bool MainWindow::refresh_current_rows_text(const std::vector<Row>& old_rows) {
    Timeline* tc = current();
    if (!tc)
        return false;
    // Only a same-length, same-order, same-id update can be edited in place; any
    // structural change (a post streamed in, a gap filled, a deletion) needs the
    // full rebind so rows are inserted/removed and the cursor re-anchored.
    if (old_rows.size() != tc->rows.size())
        return false;
    for (size_t i = 0; i < old_rows.size(); ++i)
        if (old_rows[i].id != tc->rows[i].id)
            return false;
    // Same rows: rewrite only the cells whose text actually changed, without
    // touching the cursor (moving it would make Orca re-read the row).
    GtkTreeModel* model = GTK_TREE_MODEL(posts_store_);
    updating_ = true;
    for (size_t i = 0; i < tc->rows.size(); ++i) {
        if (old_rows[i].text == tc->rows[i].text)
            continue;
        GtkTreeIter iter;
        if (gtk_tree_model_iter_nth_child(model, &iter, nullptr, static_cast<int>(i)))
            gtk_list_store_set(posts_store_, &iter, 0, tc->rows[i].text.c_str(), -1);
    }
    updating_ = false;
    return true;
}

void MainWindow::set_posts_cursor(int row) {
    GtkTreePath* path = gtk_tree_path_new_from_indices(row, -1);
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(posts_view_), path, nullptr, FALSE);
    gtk_tree_path_free(path);
}

int MainWindow::index_of_id(const Timeline& tl, const std::string& id) const {
    for (size_t i = 0; i < tl.rows.size(); ++i)
        if (tl.rows[i].id == id)
            return static_cast<int>(i);
    return -1;
}

void MainWindow::maybe_load_older(int row) {
    if (load_pending_)
        return;
    Timeline* tc = current();
    if (!tc)
        return;
    const int count = static_cast<int>(tc->rows.size());
    // A tracked middle gap within a few rows -> fill it transparently. In
    // reversed mode older posts sit above, so scan both directions.
    for (int d = 0; d <= 5; ++d) {
        for (int g : {row + d, row - d}) {
            if (g < 0 || g >= count)
                continue;
            if (tc->rows[static_cast<size_t>(g)].gap_after) {
                load_pending_ = true;
                dispatch_cmd({{"cmd", "load_gap"}, {"id", tc->rows[static_cast<size_t>(g)].id}});
                return;
            }
        }
    }
    const bool near_edge = tc->reversed ? (row <= 9) : (row >= count - 10);
    if (count > 0 && near_edge && settings_.value("auto_load_older", true)) {
        load_pending_ = true;
        dispatch_cmd({{"cmd", "load_older"}, {"automatic", true}});
    }
}

void MainWindow::first_letter_nav(gunichar ch) {
    Timeline* tc = current();
    if (!tc || tc->rows.empty())
        return;
    const int count = static_cast<int>(tc->rows.size());
    const int start = std::max(selected_row(), 0);
    for (int i = 1; i <= count; ++i) {
        const int idx = (start + i) % count;
        const std::string& text = tc->rows[static_cast<size_t>(idx)].text;
        if (!text.empty() &&
            std::tolower(static_cast<unsigned char>(text[0])) == static_cast<int>(ch)) {
            set_posts_cursor(idx); // cursor-changed fires -> note_selection etc.
            return;
        }
    }
    if (settings_.value("boundary_sound", true))
        dispatch_cmd({{"cmd", "play_earcon"}, {"name", "boundary"}});
}

// ----------------------------------------------------------------- media ---

void MainWindow::ev_media_open(const json& e) {
    const std::string kind = e.value("kind", std::string{});
    const std::string url = e.value("url", std::string{});
    const std::string title = e.value("title", std::string{});
    if (url.empty())
        return;
    // Only audio streams in the in-app player; images/video/gifv open in the
    // system handler (browser), same as Windows.
    if (!kind.empty() && kind != "audio") {
        open_url(url);
        return;
    }
    if (settings_.value("media_background", false)) { // no window; stop with Ctrl+S
        play_media_background(url, title);
        return;
    }
    MediaPlayerOptions opts;
    opts.device = settings_.value("media_device", std::string{});
    opts.volume = settings_.value("media_volume", 100);
    // Up/Down in the player move the same level the Sounds page shows, and it
    // sticks: the core persists it and hands it back on the next settings event.
    opts.on_volume = [this](int level) {
        dispatch_cmd({{"cmd", "set_media_volume"}, {"volume", level}});
    };
    const bool played = show_media_player(GTK_WINDOW(window_), title, url,
                                          [this](const std::string& m) { announce(m); },
                                          std::move(opts));
    if (!played) // couldn't render it (unsupported codec) -> system player
        open_url(url);
}

void MainWindow::play_media_background(const std::string& url, const std::string& title) {
    if (!media_bg_)
        media_bg_ = std::make_unique<MediaPlayback>();
    media_bg_->set_output_device(settings_.value("media_device", std::string{}));
    media_bg_->set_volume(settings_.value("media_volume", 100));
    if (media_bg_->play(url)) {
        announce("Playing " + title);
        if (!media_bg_timer_)
            media_bg_timer_ = g_timeout_add(1000, +[](gpointer u) -> gboolean {
                auto* self = static_cast<MainWindow*>(u);
                if (self->media_bg_ && self->media_bg_->completed()) {
                    self->media_bg_->stop();
                    self->media_bg_timer_ = 0;
                    return G_SOURCE_REMOVE;
                }
                return G_SOURCE_CONTINUE;
            }, this);
    } else {
        open_url(url);
    }
}

void MainWindow::stop_media() {
    if (media_bg_ && media_bg_->active()) {
        media_bg_->stop();
        if (media_bg_timer_) {
            g_source_remove(media_bg_timer_);
            media_bg_timer_ = 0;
        }
        announce("Stopped");
    }
}

// A menu of the post's attachments; choosing one plays it.
void MainWindow::ev_media_picker(const json& e) {
    const std::string id = e.value("id", std::string{});
    const json items = e.value("items", json::array());
    if (items.empty())
        return;
    GtkWidget* menu = gtk_menu_new();
    for (const auto& item : items) {
        GtkWidget* mi =
            gtk_menu_item_new_with_label(item.value("title", std::string{}).c_str());
        json cmd = {{"cmd", "play_media"},
                    {"id", id},
                    {"url", item.value("url", std::string{})},
                    {"kind", item.value("kind", std::string{})},
                    {"title", item.value("title", std::string{})}};
        g_object_set_data_full(G_OBJECT(mi), "fastsm-cmd",
                               g_strdup(cmd.dump().c_str()), g_free);
        g_signal_connect(mi, "activate",
                         G_CALLBACK(+[](GtkMenuItem* m, gpointer u) {
                             const auto* s = static_cast<const char*>(
                                 g_object_get_data(G_OBJECT(m), "fastsm-cmd"));
                             if (s)
                                 static_cast<MainWindow*>(u)->dispatch_cmd(json::parse(s));
                         }),
                         this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
    gtk_widget_show_all(menu);
    g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkMenuShell* m, gpointer) {
                         g_idle_add(
                             +[](gpointer w) -> gboolean {
                                 gtk_widget_destroy(GTK_WIDGET(w));
                                 return G_SOURCE_REMOVE;
                             },
                             m);
                     }),
                     nullptr);
    gtk_menu_popup_at_widget(GTK_MENU(menu), posts_view_, GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                             nullptr);
}

// ------------------------------------------------------------------ tray ---

// The tray icon (Hide/Show + Exit, double-purpose activate), mirroring the
// Windows notification-area icon. GtkStatusIcon is deprecated but remains the
// only X11 tray API; desktops without a tray host (GNOME without extensions,
// WSLg) simply never embed it, and Hide Window then refuses rather than
// stranding the window.
void MainWindow::add_tray_icon() {
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    tray_ = gtk_status_icon_new_from_icon_name("applications-internet");
    gtk_status_icon_set_tooltip_text(tray_, "FastSMRW");
    gtk_status_icon_set_title(tray_, "FastSMRW");
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_signal_connect(tray_, "activate", G_CALLBACK(+[](GtkStatusIcon*, gpointer u) {
                         auto* self = static_cast<MainWindow*>(u);
                         if (gtk_widget_get_visible(self->window_))
                             self->do_hide_window();
                         else
                             self->surface_window();
                     }),
                     this);
    g_signal_connect(
        tray_, "popup-menu", G_CALLBACK(+[](GtkStatusIcon*, guint button, guint time, gpointer u) {
            auto* self = static_cast<MainWindow*>(u);
            GtkWidget* menu = gtk_menu_new();
            const bool visible = gtk_widget_get_visible(self->window_);
            GtkWidget* toggle =
                gtk_menu_item_new_with_mnemonic(visible ? "_Hide FastSMRW" : "_Show FastSMRW");
            g_signal_connect(toggle, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer u2) {
                                 auto* s = static_cast<MainWindow*>(u2);
                                 if (gtk_widget_get_visible(s->window_))
                                     s->do_hide_window();
                                 else
                                     s->surface_window();
                             }),
                             self);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), toggle);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
            GtkWidget* exit_item = gtk_menu_item_new_with_mnemonic("E_xit");
            g_signal_connect(exit_item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer u2) {
                                 gtk_widget_destroy(static_cast<MainWindow*>(u2)->window_);
                             }),
                             self);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), exit_item);
            gtk_widget_show_all(menu);
            gtk_menu_popup(GTK_MENU(menu), nullptr, nullptr, nullptr, nullptr, button, time);
        }),
        this);
}

void MainWindow::do_hide_window() {
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    const bool tray_ok = tray_ && gtk_status_icon_is_embedded(tray_);
    G_GNUC_END_IGNORE_DEPRECATIONS
    const bool invisible_on = invisible_mode_ != "off" && invisible_.running();
    if (!tray_ok && !invisible_on) {
        announce("Can't hide: no system tray is available and the invisible interface is off, "
                 "so there would be no way to bring the window back.");
        return;
    }
    gtk_widget_hide(window_);
    dispatch_cmd({{"cmd", "set_window_shown"}, {"shown", false}});
}

void MainWindow::surface_window() {
    gtk_window_present(GTK_WINDOW(window_));
    dispatch_cmd({{"cmd", "set_window_shown"}, {"shown", true}});
}

// ---------------------------------------------------------------- actions ---

void MainWindow::compose(const std::string& mode) {
    json cmd = {{"cmd", "compose_context"}, {"mode", mode}};
    if (mode != "new") {
        const std::string id = selected_id();
        if (id.empty())
            return;
        cmd["id"] = id;
    }
    dispatch_cmd(cmd);
}

// Enter/Shift+Enter run the configurable interact actions (Behavior settings),
// same values as Windows: post_info | thread | reply | links | play_media.
void MainWindow::run_post_action(const std::string& action) {
    const std::string id = selected_id();
    if (id.empty())
        return;
    if (action == "thread")
        dispatch_cmd({{"cmd", "open_thread"}, {"id", id}});
    else if (action == "reply")
        compose("reply");
    else if (action == "links")
        dispatch_cmd({{"cmd", "open_post_links"}, {"id", id}});
    else if (action == "play_media")
        dispatch_cmd({{"cmd", "play_media"}, {"id", id}});
    else
        dispatch_cmd({{"cmd", "post_info"}, {"id", id}});
}

void MainWindow::do_enter_post_action() {
    // A grouped like/boost notification: Enter opens the list of everyone in it.
    Timeline* tc = current();
    const int row = selected_row();
    if (tc && row >= 0 && row < static_cast<int>(tc->rows.size())) {
        const Row& r = tc->rows[static_cast<size_t>(row)];
        if (r.group_actors == "favorited_by") {
            dispatch_cmd({{"cmd", "open_favorited_by"}, {"id", r.id}});
            return;
        }
        if (r.group_actors == "reblogged_by") {
            dispatch_cmd({{"cmd", "open_reblogged_by"}, {"id", r.id}});
            return;
        }
    }
    // The Conversations feed is a list of threads: Enter always opens the thread,
    // whatever the general post-Enter setting is.
    if (tc && tc->enter_opens_thread) {
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_thread"}, {"id", id}});
        return;
    }
    run_post_action(settings_.value("enter_post_action", std::string("post_info")));
}

// Enter on a follow-request notification: a two-item Accept/Reject popup, like
// the Windows app. The command strings are built here, synchronously, so the
// menu never touches the (mutable) row list after it pops up.
void MainWindow::do_follow_request_action(const Row& r) {
    GtkWidget* menu = gtk_menu_new();
    const char* labels[] = {"_Accept", "_Reject"};
    const char* actions[] = {"authorize_request", "reject_request"};
    for (int i = 0; i < 2; ++i) {
        GtkWidget* mi = gtk_menu_item_new_with_mnemonic(labels[i]);
        json cmd(json::object());
        cmd["cmd"] = "set_relationship";
        cmd["account_id"] = r.account_id;
        cmd["acct"] = r.acct;
        cmd["action"] = actions[i];
        g_object_set_data_full(G_OBJECT(mi), "fastsm-cmd", g_strdup(cmd.dump().c_str()), g_free);
        g_signal_connect(mi, "activate", G_CALLBACK(+[](GtkMenuItem* m, gpointer u) {
                             const auto* s = static_cast<const char*>(
                                 g_object_get_data(G_OBJECT(m), "fastsm-cmd"));
                             if (s)
                                 static_cast<MainWindow*>(u)->dispatch_cmd(json::parse(s));
                         }),
                         this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
    gtk_widget_show_all(menu);
    g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkMenuShell* m, gpointer) {
                         g_idle_add(
                             +[](gpointer w) -> gboolean {
                                 gtk_widget_destroy(GTK_WIDGET(w));
                                 return G_SOURCE_REMOVE;
                             },
                             m);
                     }),
                     nullptr);
    gtk_menu_popup_at_widget(GTK_MENU(menu), posts_view_, GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                             nullptr);
}

// Enter on a user-list row runs the configurable action (Behavior settings),
// mirroring the Windows do_enter_user_action.
void MainWindow::do_enter_user_action() {
    const std::string a = settings_.value("enter_user_action", std::string("actions"));
    const std::string id = selected_id();
    if (a == "profile") {
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_user_profile"}, {"id", id}});
    } else if (a == "timeline") {
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_user_timeline"}, {"id", id}});
    } else {
        show_user_actions();
    }
}

// The selected user rows' ids (the multi-selection), falling back to the focused
// row — the same collection rule as the Windows show_user_actions.
std::vector<std::string> MainWindow::selected_user_row_ids() {
    std::vector<std::string> ids;
    Timeline* tc = current();
    if (!tc)
        return ids;
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(posts_view_));
    GList* paths = gtk_tree_selection_get_selected_rows(sel, nullptr);
    for (GList* p = paths; p; p = p->next) {
        const int row = gtk_tree_path_get_indices(static_cast<GtkTreePath*>(p->data))[0];
        if (row >= 0 && row < static_cast<int>(tc->rows.size()))
            ids.push_back(tc->rows[static_cast<size_t>(row)].id);
    }
    g_list_free_full(paths, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));
    if (ids.empty()) {
        const int row = selected_row();
        if (row >= 0 && row < static_cast<int>(tc->rows.size()))
            ids.push_back(tc->rows[static_cast<size_t>(row)].id);
    }
    return ids;
}

// The user-row actions menu, acting on every selected row ("(N)" in the labels
// for a multi-selection). On the Follow Requests list it offers Accept/Reject
// instead of the follow/mute/block set — same shape as the Windows menu.
void MainWindow::show_user_actions() {
    Timeline* tc = current();
    if (!tc)
        return;
    const std::vector<std::string> ids = selected_user_row_ids();
    if (ids.empty())
        return;
    const bool requests = tc->kind == "followRequests";
    static const char* kUserLabels[] = {"_Follow", "_Unfollow", "_Mute",
                                        "Un_mute", "_Block",    "Unbl_ock"};
    static const char* kUserActs[] = {"follow", "unfollow", "mute", "unmute", "block", "unblock"};
    static const char* kReqLabels[] = {"_Accept", "_Reject"};
    static const char* kReqActs[] = {"authorize_request", "reject_request"};
    const char* const* labels = requests ? kReqLabels : kUserLabels;
    const char* const* acts = requests ? kReqActs : kUserActs;
    const int count = requests ? 2 : 6;
    json jids(json::array());
    for (const auto& id : ids)
        jids.push_back(id);
    const std::string ids_json = jids.dump();
    GtkWidget* menu = gtk_menu_new();
    for (int i = 0; i < count; ++i) {
        std::string label = labels[i];
        if (ids.size() > 1)
            label += " (" + std::to_string(ids.size()) + ")";
        GtkWidget* mi = gtk_menu_item_new_with_mnemonic(label.c_str());
        g_object_set_data_full(G_OBJECT(mi), "fastsm-act", g_strdup(acts[i]), g_free);
        g_object_set_data_full(G_OBJECT(mi), "fastsm-ids", g_strdup(ids_json.c_str()), g_free);
        g_signal_connect(mi, "activate", G_CALLBACK(+[](GtkMenuItem* m, gpointer u) {
                             const auto* act = static_cast<const char*>(
                                 g_object_get_data(G_OBJECT(m), "fastsm-act"));
                             const auto* rids = static_cast<const char*>(
                                 g_object_get_data(G_OBJECT(m), "fastsm-ids"));
                             if (!act || !rids)
                                 return;
                             std::vector<std::string> parsed;
                             for (const auto& v : json::parse(rids))
                                 parsed.push_back(v.get<std::string>());
                             static_cast<MainWindow*>(u)->run_user_action(act, parsed);
                         }),
                         this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
    gtk_widget_show_all(menu);
    g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkMenuShell* m, gpointer) {
                         g_idle_add(
                             +[](gpointer w) -> gboolean {
                                 gtk_widget_destroy(GTK_WIDGET(w));
                                 return G_SOURCE_REMOVE;
                             },
                             m);
                     }),
                     nullptr);
    gtk_menu_popup_at_widget(GTK_MENU(menu), posts_view_, GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                             nullptr);
}

void MainWindow::run_user_action(const std::string& action,
                                 const std::vector<std::string>& row_ids) {
    if (row_ids.empty())
        return;
    const std::string many = std::to_string(row_ids.size()) + " users?";
    if (action == "follow" && settings_.value("confirm_follow", false)) {
        if (!confirm(row_ids.size() > 1 ? "Follow " + many : "Follow this user?", "Follow"))
            return;
    } else if (action == "unfollow" && settings_.value("confirm_unfollow", false)) {
        if (!confirm(row_ids.size() > 1 ? "Unfollow " + many : "Unfollow this user?", "Unfollow"))
            return;
    } else if (action == "block" && settings_.value("confirm_block", true)) {
        if (!confirm(row_ids.size() > 1 ? "Block " + many : "Block this user?", "Block"))
            return;
    } else if (action == "unblock" && settings_.value("confirm_unblock", false)) {
        if (!confirm(row_ids.size() > 1 ? "Unblock " + many : "Unblock this user?", "Unblock"))
            return;
    }
    json ids(json::array());
    for (const auto& id : row_ids)
        ids.push_back(id);
    dispatch_cmd({{"cmd", "user_action"}, {"action", action}, {"ids", std::move(ids)}});
}

void MainWindow::do_secondary_post_action() {
    run_post_action(settings_.value("secondary_post_action", std::string("play_media")));
}

void MainWindow::do_find() {
    auto text = prompt_text("Find", "Find in timeline:", find_query_);
    if (!text || text->empty())
        return;
    find_query_ = *text;
    const int row = selected_row();
    find_from(row < 0 ? 0 : row, 1); // from the current row (inclusive), forward
}

void MainWindow::do_find_next() {
    if (find_query_.empty()) {
        do_find();
        return;
    }
    find_from(selected_row() + 1, 1);
}

void MainWindow::do_find_prev() {
    if (find_query_.empty()) {
        do_find();
        return;
    }
    find_from(selected_row() - 1, -1);
}

void MainWindow::find_from(int start_row, int dir) {
    Timeline* tc = current();
    if (!tc || tc->rows.empty() || find_query_.empty())
        return;
    const int n = static_cast<int>(tc->rows.size());
    gchar* query = g_utf8_casefold(find_query_.c_str(), -1);
    for (int off = 0; off < n; ++off) { // scan in `dir`, wrapping around once
        const int i = ((start_row + dir * off) % n + n) % n;
        gchar* hay = g_utf8_casefold(tc->rows[static_cast<size_t>(i)].text.c_str(), -1);
        const bool hit = strstr(hay, query) != nullptr;
        g_free(hay);
        if (hit) {
            g_free(query);
            // Select normally (no updating_ guard) so the move is noted to the
            // core and the screen reader reads the matched post.
            set_posts_cursor(i);
            gtk_widget_grab_focus(posts_view_);
            return;
        }
    }
    g_free(query);
    announce("Not found");
}

std::optional<std::string> MainWindow::prompt_text(const std::string& title,
                                                   const std::string& label,
                                                   const std::string& prefill) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        title.c_str(), GTK_WINDOW(window_),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    GtkWidget* prompt = gtk_label_new(label.c_str());
    gtk_label_set_xalign(GTK_LABEL(prompt), 0.0f);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    atk_object_set_name(gtk_widget_get_accessible(entry), label.c_str());
    if (!prefill.empty())
        gtk_entry_set_text(GTK_ENTRY(entry), prefill.c_str());
    gtk_box_pack_start(GTK_BOX(box), prompt, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, TRUE, TRUE,
                       0);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(entry);
    std::optional<std::string> result;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
        result = gtk_entry_get_text(GTK_ENTRY(entry));
    gtk_widget_destroy(dialog);
    return result;
}

void MainWindow::do_boost() {
    Timeline* tc = current();
    const int row = selected_row();
    if (!tc || row < 0 || row >= static_cast<int>(tc->rows.size()))
        return;
    const Row& r = tc->rows[static_cast<size_t>(row)];
    const bool boosting = !r.boosted;
    if (boosting && settings_.value("confirm_boost", false) && !confirm("Boost this post?", "Boost"))
        return;
    if (!boosting && settings_.value("confirm_unboost", false) &&
        !confirm("Unboost this post?", "Unboost"))
        return;
    dispatch_cmd({{"cmd", "toggle_boost"}, {"id", r.id}});
}

void MainWindow::do_favorite() {
    Timeline* tc = current();
    const int row = selected_row();
    if (!tc || row < 0 || row >= static_cast<int>(tc->rows.size()))
        return;
    const Row& r = tc->rows[static_cast<size_t>(row)];
    const bool favoriting = !r.favorited;
    if (favoriting && settings_.value("confirm_favorite", false) &&
        !confirm("Favorite this post?", "Favorite"))
        return;
    if (!favoriting && settings_.value("confirm_unfavorite", false) &&
        !confirm("Unfavorite this post?", "Unfavorite"))
        return;
    dispatch_cmd({{"cmd", "toggle_favorite"}, {"id", r.id}});
}

void MainWindow::do_bookmark() {
    const std::string id = selected_id();
    if (!id.empty())
        dispatch_cmd({{"cmd", "toggle_bookmark"}, {"id", id}});
}

void MainWindow::do_delete_post() {
    Timeline* tc = current();
    const int row = selected_row();
    if (!tc || row < 0 || row >= static_cast<int>(tc->rows.size()))
        return;
    const Row& r = tc->rows[static_cast<size_t>(row)];
    if (!r.is_mine)
        return;
    if (settings_.value("confirm_delete_post", true) &&
        !confirm("Delete this post?", "Delete Post"))
        return;
    dispatch_cmd({{"cmd", "delete_post"}, {"id", r.id}});
}

void MainWindow::do_load_older() {
    if (load_pending_)
        return;
    Timeline* tc = current();
    if (!tc || tc->rows.empty())
        return;
    load_pending_ = true;
    dispatch_cmd({{"cmd", "load_older"}});
}

void MainWindow::do_add_account() {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Add Account", GTK_WINDOW(window_),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);

    GtkWidget* platform_label = gtk_label_new_with_mnemonic("_Platform:");
    GtkWidget* combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Mastodon");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "Bluesky");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(platform_label), combo);

    GtkWidget* service_label = gtk_label_new_with_mnemonic("_Instance:");
    GtkWidget* service_entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(service_entry), TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(service_label), service_entry);

    GtkWidget* handle_label = gtk_label_new_with_mnemonic("_Handle:");
    GtkWidget* handle_entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(handle_entry), TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(handle_label), handle_entry);

    GtkWidget* pass_label = gtk_label_new_with_mnemonic("App _password:");
    GtkWidget* pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);
    gtk_entry_set_activates_default(GTK_ENTRY(pass_entry), TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(pass_label), pass_entry);

    gtk_grid_attach(GTK_GRID(grid), platform_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), combo, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), service_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), service_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), handle_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), handle_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), pass_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), pass_entry, 1, 3, 1, 1);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid, TRUE, TRUE,
                       0);

    struct Ctx {
        GtkWidget *service_label, *service_entry, *handle_label, *handle_entry, *pass_label,
            *pass_entry;
    } ctx{service_label, service_entry, handle_label, handle_entry, pass_label, pass_entry};

    auto update_fields = +[](GtkComboBox* c, gpointer user) {
        auto* x = static_cast<Ctx*>(user);
        const bool bluesky = gtk_combo_box_get_active(c) == 1;
        gtk_label_set_text_with_mnemonic(GTK_LABEL(x->service_label),
                                         bluesky ? "_Service:" : "_Instance:");
        gtk_widget_set_visible(x->handle_label, bluesky);
        gtk_widget_set_visible(x->handle_entry, bluesky);
        gtk_widget_set_visible(x->pass_label, bluesky);
        gtk_widget_set_visible(x->pass_entry, bluesky);
        // Auto-fill Bluesky's default service, but don't let it linger as a
        // bogus Mastodon instance when toggling back (the field is shared).
        const char* text = gtk_entry_get_text(GTK_ENTRY(x->service_entry));
        if (bluesky) {
            if (!*text)
                gtk_entry_set_text(GTK_ENTRY(x->service_entry), "bsky.social");
        } else if (g_strcmp0(text, "bsky.social") == 0) {
            gtk_entry_set_text(GTK_ENTRY(x->service_entry), "");
        }
    };
    g_signal_connect(combo, "changed", G_CALLBACK(update_fields), &ctx);

    gtk_widget_show_all(dialog);
    update_fields(GTK_COMBO_BOX(combo), &ctx); // hide the Bluesky rows initially

    const int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        json cmd = {{"cmd", "add_account"}};
        if (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)) == 0) {
            cmd["platform"] = "mastodon";
            cmd["instance"] = gtk_entry_get_text(GTK_ENTRY(service_entry));
        } else {
            cmd["platform"] = "bluesky";
            cmd["service"] = gtk_entry_get_text(GTK_ENTRY(service_entry));
            cmd["handle"] = gtk_entry_get_text(GTK_ENTRY(handle_entry));
            cmd["app_password"] = gtk_entry_get_text(GTK_ENTRY(pass_entry));
        }
        dispatch_cmd(cmd);
    }
    gtk_widget_destroy(dialog);
}

void MainWindow::restore_selection(const std::string& id) {
    Timeline* tc = current();
    if (!tc || id.empty())
        return;
    const int idx = index_of_id(*tc, id);
    if (idx < 0)
        return;
    tc->selected_id = id;
    updating_ = true;
    set_posts_cursor(idx);
    updating_ = false;
}

// ----------------------------------------------------------------- events ---

void MainWindow::event_sink(void* user, const char* json_str, size_t len) {
    struct Payload {
        MainWindow* win;
        std::string json;
    };
    auto* payload = new Payload{static_cast<MainWindow*>(user), std::string(json_str, len)};
    g_idle_add(
        +[](gpointer data) -> gboolean {
            auto* p = static_cast<Payload*>(data);
            p->win->on_event(p->json);
            delete p;
            return G_SOURCE_REMOVE;
        },
        payload);
}

void MainWindow::on_event(const std::string& js) {
    json e;
    try {
        e = json::parse(js);
    } catch (...) {
        return;
    }
    const std::string ev = e.value("event", std::string{});
    if (ev == "compose_context")
        ev_compose_context(e);
    else if (ev == "timelines_changed")
        ev_timelines_changed(e);
    else if (ev == "timeline_updated")
        ev_timeline_updated(e);
    else if (ev == "announce")
        announce(e.value("message", std::string{}));
    else if (ev == "settings")
        ev_settings(e);
    else if (ev == "spawnable_timelines")
        ev_spawnable(e);
    else if (ev == "select_row")
        restore_selection(e.value("id", std::string{}));
    else if (ev == "copy_to_clipboard")
        ev_copy(e);
    else if (ev == "url_picker")
        ev_url_picker(e);
    else if (ev == "confirm")
        ev_confirm(e);
    else if (ev == "user_picker")
        ev_user_picker(e);
    else if (ev == "user_suggestions")
        ev_user_suggestions(e);
    else if (ev == "post_info")
        ev_post_info(e);
    else if (ev == "user_profile")
        ev_user_profile(e);
    else if (ev == "account_settings")
        ev_account_settings(e);
    else if (ev == "keymap")
        ev_keymap(e);
    else if (ev == "action_catalog")
        ev_action_catalog(e);
    else if (ev == "invisible_ui_action")
        ev_invisible_ui_action(e);
    else if (ev == "follow_request_prompt")
        ev_follow_request_prompt(e);
    else if (ev == "hashtag_prompt")
        ev_hashtag_prompt(e);
    else if (ev == "hashtag_timeline_picker")
        ev_hashtag_timeline_picker(e);
    else if (ev == "lists")
        ev_lists(e);
    else if (ev == "user_lists")
        ev_user_lists(e);
    else if (ev == "followed_hashtags")
        ev_followed_hashtags(e);
    else if (ev == "trending_hashtags")
        ev_trending_hashtags(e);
    else if (ev == "alias_prompt")
        ev_alias_prompt(e);
    else if (ev == "aliases_list")
        ev_aliases_list(e);
    else if (ev == "client_filter")
        ev_client_filter(e);
    else if (ev == "server_filters")
        ev_server_filters(e);
    else if (ev == "profile_editor")
        ev_profile_editor(e);
    else if (ev == "open_url")
        open_url(e.value("url", std::string{}));
    else if (ev == "media_open")
        ev_media_open(e);
    else if (ev == "media_picker")
        ev_media_picker(e);
    else if (ev == "update_status") {
        // No packaged Linux download yet: announce, and offer the releases page.
        const bool silent = e.value("silent", false);
        const std::string error = e.value("error", std::string{});
        if (!error.empty()) {
            if (!silent)
                announce(error);
        } else if (!e.value("available", false)) {
            if (!silent)
                announce("You're running the latest version.");
        } else {
            const std::string version = e.value("version", std::string{});
            if (confirm("Version " + version +
                            " is available. Open the releases page in your browser?",
                        "Check for Updates"))
                open_url("https://github.com/masonasons/FastSMRW/releases");
        }
    } else if (ev == "update_error")
        announce(e.value("error", std::string("Update failed.")));
    // Everything else (dialog-opener events for features not yet built on
    // Linux) is ignored for now; sounds and announcements come from the core.
}

void MainWindow::ev_compose_context(const json& e) {
    const std::string keep_id = selected_id();
    auto cmd = show_compose_dialog(GTK_WINDOW(window_), e, [this](const std::string& partial) {
        return pick_mention(partial);
    });
    restore_selection(keep_id);
    if (cmd)
        dispatch_cmd(*cmd);
}

// The @-mention picker: type part of a handle, arrow the matches, Enter
// inserts. Matches come from autocomplete_users -> user_suggestions while the
// picker's nested loop pumps.
std::optional<std::string> MainWindow::pick_mention(const std::string& partial) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Mention", GTK_WINDOW(window_),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, "_Insert", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 360, 380);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    GtkWidget* query_label = gtk_label_new_with_mnemonic("_Handle:");
    gtk_label_set_xalign(GTK_LABEL(query_label), 0.0f);
    mention_query_ = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(mention_query_), partial.c_str());
    gtk_editable_select_region(GTK_EDITABLE(mention_query_), 0, -1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(query_label), mention_query_);
    gtk_box_pack_start(GTK_BOX(box), query_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), mention_query_, FALSE, FALSE, 0);

    mention_store_ = gtk_list_store_new(1, G_TYPE_STRING);
    mention_view_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(mention_store_));
    g_object_unref(mention_store_);
    gtk_tree_view_append_column(
        GTK_TREE_VIEW(mention_view_), gtk_tree_view_column_new_with_attributes(
                                          "", gtk_cell_renderer_text_new(), "text", 0, nullptr));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(mention_view_), FALSE);
    atk_object_set_name(gtk_widget_get_accessible(mention_view_), "Matches");
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), mention_view_);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, TRUE, TRUE,
                       0);

    g_signal_connect(mention_query_, "changed", G_CALLBACK(+[](GtkEditable* e, gpointer u) {
                         static_cast<MainWindow*>(u)->dispatch_cmd(
                             {{"cmd", "autocomplete_users"},
                              {"query", gtk_entry_get_text(GTK_ENTRY(e))}});
                     }),
                     this);
    g_signal_connect_swapped(mention_view_, "row-activated",
                             G_CALLBACK(+[](gpointer d, GtkTreePath*, GtkTreeViewColumn*) {
                                 gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
                             }),
                             dialog);

    mention_dialog_ = dialog;
    mention_handles_.clear();
    gtk_widget_show_all(dialog);
    // Land on the results (matches are already coming for the seeded partial)
    // so a screen reader arrows straight through them; with nothing typed
    // there is nothing to land on yet, so start in the search box.
    gtk_widget_grab_focus(partial.empty() ? mention_query_ : mention_view_);
    dispatch_cmd({{"cmd", "autocomplete_users"}, {"query", partial}}); // seed matches

    std::optional<std::string> result;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        GtkTreePath* path = nullptr;
        gtk_tree_view_get_cursor(GTK_TREE_VIEW(mention_view_), &path, nullptr);
        if (path) {
            const int row = gtk_tree_path_get_indices(path)[0];
            gtk_tree_path_free(path);
            if (row >= 0 && row < static_cast<int>(mention_handles_.size()))
                result = mention_handles_[static_cast<size_t>(row)];
        }
    }
    mention_dialog_ = nullptr;
    mention_query_ = nullptr;
    mention_store_ = nullptr;
    mention_view_ = nullptr;
    mention_handles_.clear();
    gtk_widget_destroy(dialog);
    return result;
}

void MainWindow::ev_user_suggestions(const json& e) {
    if (!mention_dialog_)
        return;
    // Drop a stale reply: apply only if it still matches what's typed now.
    if (e.value("query", std::string{}) !=
        std::string(gtk_entry_get_text(GTK_ENTRY(mention_query_))))
        return;
    gtk_list_store_clear(mention_store_);
    mention_handles_.clear();
    for (const auto& u : e.value("users", json::array())) {
        const std::string handle = u.value("acct", std::string{});
        if (handle.empty())
            continue;
        std::string label = u.value("label", std::string{});
        if (label.empty())
            label = "@" + handle;
        GtkTreeIter iter;
        gtk_list_store_append(mention_store_, &iter);
        gtk_list_store_set(mention_store_, &iter, 0, label.c_str(), -1);
        mention_handles_.push_back(handle);
    }
    if (!mention_handles_.empty()) {
        GtkTreePath* first = gtk_tree_path_new_first();
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(mention_view_), first, nullptr, FALSE);
        gtk_tree_path_free(first);
    }
}

void MainWindow::ev_timelines_changed(const json& e) {
    const size_t prev_count = timelines_.size();
    // Timeline kinds repeat across accounts, so only carry rows/position within
    // the SAME account (a spawn/close) — never across an account switch.
    const std::string account = e.value("account", std::string{});
    const bool same_account = account == current_account_;
    current_account_ = account;
    std::vector<Timeline> next;
    for (const auto& t : e.value("timelines", json::array())) {
        Timeline tl;
        tl.title = t.value("title", std::string{});
        tl.kind = t.value("kind", std::string{});
        tl.dismissable = t.value("dismissable", false);
        tl.pinned = t.value("pinned", false);
        tl.muted = t.value("muted", false);
        tl.auto_read = t.value("auto_read", false);
        tl.user_list = t.value("user_list", false);
        tl.enter_opens_thread = t.value("enter_opens_thread", false);
        if (same_account)
            for (const auto& old : timelines_)
                if (old.kind == tl.kind) {
                    tl.rows = old.rows;
                    tl.selected_id = old.selected_id;
                    break;
                }
        next.push_back(std::move(tl));
    }
    timelines_ = std::move(next);
    current_ = e.value("current", 0);
    if (current_ < 0 || current_ >= static_cast<int>(timelines_.size()))
        current_ = 0;
    load_pending_ = false;
    populate_timelines_list();
    bind_current_to_view();
    // When a NEW timeline appears (opening a thread etc.), land focus on the
    // posts so the user is on the content — but never steal it from a dialog.
    if (timelines_.size() > prev_count && gtk_window_is_active(GTK_WINDOW(window_)))
        gtk_widget_grab_focus(posts_view_);
}

void MainWindow::ev_timeline_updated(const json& e) {
    const int index = e.value("index", -1);
    if (index < 0 || index >= static_cast<int>(timelines_.size()))
        return;
    Timeline& tl = timelines_[static_cast<size_t>(index)];
    tl.reversed = e.value("reversed", false);
    // Keep the old rows so a content-only re-emit (relative timestamps ticking
    // over on the timeline the user is sitting on) can update cells in place
    // instead of rebuilding the whole model and re-announcing the focused row.
    std::vector<Row> old_rows = std::move(tl.rows);
    tl.rows.clear();
    for (const auto& r : e.value("rows", json::array())) {
        Row row;
        row.id = r.value("id", std::string{});
        row.text = r.value("text", std::string{});
        row.favorited = r.value("favorited", false);
        row.boosted = r.value("boosted", false);
        row.bookmarked = r.value("bookmarked", false);
        row.muted = r.value("muted", false);
        row.is_mine = r.value("is_mine", false);
        row.gap_after = r.value("gap_after", false);
        row.follow_request = r.value("follow_request", false);
        row.account_id = r.value("account_id", std::string{});
        row.acct = r.value("acct", std::string{});
        row.group_actors = r.value("group_actors", std::string{});
        tl.rows.push_back(std::move(row));
    }
    // Adopt the core's remembered position on first load, or when the tracked
    // row vanished (the core re-anchors it to keep the reading position).
    if (tl.selected_id.empty() || index_of_id(tl, tl.selected_id) < 0) {
        const std::string core_id = e.value("selected_id", std::string{});
        if (!core_id.empty() && index_of_id(tl, core_id) >= 0)
            tl.selected_id = core_id;
    }
    if (index == current_) {
        load_pending_ = false;
        if (!refresh_current_rows_text(old_rows))
            bind_current_to_view();
    }
}

void MainWindow::ev_settings(const json& e) {
    settings_ = e.value("settings", json::object());
    soundpacks_.clear();
    for (const auto& p : e.value("soundpacks", json::array()))
        soundpacks_.push_back(p.get<std::string>());
    sound_devices_.clear();
    for (const auto& d : e.value("sound_devices", json::array()))
        sound_devices_.push_back(d.get<std::string>());
    // A media volume change while something is already playing in the background
    // should be heard now, not next time. (The device only applies to a new
    // stream — it's chosen when the graph is built.)
    if (media_bg_)
        media_bg_->set_volume(settings_.value("media_volume", 100));
    apply_invisible();
    if (action_catalog_.empty()) // load once so the Keyboard Manager has its actions
        dispatch_cmd({{"cmd", "get_action_catalog"}});
}

void MainWindow::do_settings() {
    auto s = fastsm::store::settings_from_json(settings_);
    AudioChoices audio;
    audio.soundpacks = soundpacks_;
    audio.sound_devices = sound_devices_;      // the core's mixer devices
    audio.media_devices = media_output_devices(); // GStreamer's, which can differ
    if (auto result = show_settings_dialog(GTK_WINDOW(window_), std::move(s), audio))
        dispatch_cmd(
            {{"cmd", "update_settings"}, {"settings", fastsm::store::settings_to_json(*result)}});
}

// The original FastSM's Linux invisible interface is flat always-on hotkeys,
// so every non-off mode maps to the evdev hotkey driver here.
void MainWindow::apply_invisible() {
    const std::string mode = settings_.value("invisible_mode", std::string("off"));
    if (mode == invisible_mode_)
        return;
    invisible_mode_ = mode;
    if (mode == "off")
        invisible_.stop();
    else
        dispatch_cmd({{"cmd", "get_keymap"}}); // ev_keymap installs the hotkeys
}

void MainWindow::ev_action_catalog(const json& e) {
    action_catalog_.clear();
    for (const auto& a : e.value("actions", json::array()))
        action_catalog_.push_back({a.value("id", std::string{}), a.value("label", std::string{}),
                                   a.value("default_key", std::string{})});
}

void MainWindow::open_keymap_manager() {
    if (action_catalog_.empty()) {
        announce("Keyboard actions are still loading; try again in a moment.");
        dispatch_cmd({{"cmd", "get_action_catalog"}});
        return;
    }
    const std::string active = settings_.value("invisible_keymap", std::string("default"));
    KeymapManagerDialog dlg(action_catalog_, active,
                            [this](const json& cmd) { dispatch_cmd(cmd); });
    keymap_mgr_ = &dlg;
    dlg.run(GTK_WINDOW(window_));
    keymap_mgr_ = nullptr;
}

void MainWindow::ev_keymap(const json& e) {
    if (keymap_mgr_)
        keymap_mgr_->on_keymap(e); // the manager is open: feed it (same UI thread)
    // Only (re)bind the global hotkeys for the ACTIVE keymap — ignore events for
    // other keymaps the manager is merely browsing. Mirrors Windows ev_keymap.
    const std::string active = settings_.value("invisible_keymap", std::string("default"));
    if (e.value("name", std::string{}) != active)
        return;
    if (invisible_mode_ == "off")
        return;
    std::unordered_map<std::string, std::string> bindings;
    // Bind to a named object first: iterating .items() on the temporary returned
    // by value() would dangle (the proxy outlives the temporary).
    const json binds = e.value("bindings", json::object());
    for (const auto& [key, action] : binds.items())
        if (action.is_string())
            bindings[key] = action.get<std::string>();
    std::string error;
    if (!invisible_.start(std::move(bindings),
                          [this](const std::string& action) {
                              dispatch_cmd({{"cmd", "perform_action"}, {"action", action}});
                          },
                          error))
        announce(error);
}

void MainWindow::ev_invisible_ui_action(const json& e) {
    const std::string action = e.value("action", std::string{});
    if (action == "ToggleWindow") {
        if (gtk_widget_get_visible(window_))
            do_hide_window();
        else
            surface_window();
    } else if (action == "Options") {
        do_settings();
    } else if (action == "KeymapManager") {
        open_keymap_manager();
    } else if (action == "Find") {
        do_find();
    } else if (action == "FindNext") {
        do_find_next();
    } else if (action == "FindPrev") {
        do_find_prev();
    } else if (action == "NewTimeline") {
        dispatch_cmd({{"cmd", "get_spawnable"}});
    } else if (action == "UserActions") {
        show_user_actions();
    }
    // Other UI-side actions (media, dialogs not yet built on Linux) are
    // ignored for now.
}

// The core-driven accept/reject choice (the core's Enter action can't show UI).
// A dialog works even while the window is hidden, unlike the row popup menu.
void MainWindow::ev_follow_request_prompt(const json& e) {
    const std::string account_id = e.value("account_id", std::string{});
    const std::string acct = e.value("acct", std::string{});
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Follow request", GTK_WINDOW(window_),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Accept",
        GTK_RESPONSE_YES, "_Reject", GTK_RESPONSE_NO, "_Cancel", GTK_RESPONSE_CANCEL, nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    const std::string text = "Accept the follow request from @" + acct + "?";
    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 12);
    gtk_container_add(GTK_CONTAINER(content), label);
    gtk_widget_show_all(dlg);
    const gint response = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (response != GTK_RESPONSE_YES && response != GTK_RESPONSE_NO)
        return;
    dispatch_cmd({{"cmd", "set_relationship"},
                  {"account_id", account_id},
                  {"acct", acct},
                  {"action", response == GTK_RESPONSE_YES ? "authorize_request" : "reject_request"}});
}

void MainWindow::ev_spawnable(const json& e) {
    std::vector<std::string> kinds, params, inputs;
    GtkListStore* store = gtk_list_store_new(1, G_TYPE_STRING);
    for (const auto& t : e.value("timelines", json::array())) {
        kinds.push_back(t.value("kind", std::string{}));
        params.push_back(t.value("param", std::string{}));
        inputs.push_back(t.value("input", std::string{}));
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, t.value("title", std::string{}).c_str(), -1);
    }
    if (kinds.empty()) {
        g_object_unref(store);
        announce("No more timelines to add for this account.");
        return;
    }

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "New Timeline", GTK_WINDOW(window_),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 420);

    GtkWidget* view = make_list_view(store, "Timeline kinds");
    g_object_unref(store);
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget* input_label = gtk_label_new("");
    GtkWidget* input_entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(input_entry), TRUE);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), input_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), input_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, TRUE, TRUE,
                       0);

    struct Ctx {
        std::vector<std::string>* inputs;
        GtkWidget *input_label, *input_entry;
    } ctx{&inputs, input_label, input_entry};

    auto update_input = +[](GtkTreeView* v, gpointer user) {
        auto* x = static_cast<Ctx*>(user);
        GtkTreePath* path = nullptr;
        gtk_tree_view_get_cursor(v, &path, nullptr);
        if (!path)
            return;
        const int idx = gtk_tree_path_get_indices(path)[0];
        gtk_tree_path_free(path);
        const std::string& label =
            (idx >= 0 && idx < static_cast<int>(x->inputs->size())) ? (*x->inputs)[idx]
                                                                    : std::string{};
        gtk_label_set_text(GTK_LABEL(x->input_label), label.c_str());
        gtk_widget_set_visible(x->input_label, !label.empty());
        gtk_widget_set_visible(x->input_entry, !label.empty());
        atk_object_set_name(gtk_widget_get_accessible(x->input_entry),
                            label.empty() ? "Value" : label.c_str());
    };
    g_signal_connect(view, "cursor-changed", G_CALLBACK(update_input), &ctx);
    g_signal_connect_swapped(view, "row-activated",
                             G_CALLBACK(+[](gpointer d, GtkTreePath*, GtkTreeViewColumn*) {
                                 gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
                             }),
                             dialog);

    gtk_widget_show_all(dialog);
    GtkTreePath* first = gtk_tree_path_new_first();
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(view), first, nullptr, FALSE);
    gtk_tree_path_free(first);
    gtk_widget_grab_focus(view);

    const int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        GtkTreePath* path = nullptr;
        gtk_tree_view_get_cursor(GTK_TREE_VIEW(view), &path, nullptr);
        if (path) {
            const int idx = gtk_tree_path_get_indices(path)[0];
            gtk_tree_path_free(path);
            if (idx >= 0 && idx < static_cast<int>(kinds.size())) {
                json cmd = {{"cmd", "spawn_timeline"}, {"kind", kinds[static_cast<size_t>(idx)]}};
                const std::string value = gtk_entry_get_text(GTK_ENTRY(input_entry));
                if (!value.empty() && !inputs[static_cast<size_t>(idx)].empty())
                    cmd["value"] = value;
                if (!params[static_cast<size_t>(idx)].empty())
                    cmd["param"] = params[static_cast<size_t>(idx)];
                dispatch_cmd(cmd);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

void MainWindow::ev_copy(const json& e) {
    const std::string text = e.value("text", std::string{});
    if (!text.empty())
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), -1);
}

void MainWindow::ev_post_info(const json& e) {
    const std::string id = e.value("id", std::string{});
    const std::string keep_id = selected_id();
    auto res = show_post_info_dialog(GTK_WINDOW(window_), e);
    restore_selection(keep_id);
    if (!res)
        return;
    const std::string& act = res->action;
    if (act == "vote") {
        json choices = json::array();
        for (int c : res->choices)
            choices.push_back(c);
        dispatch_cmd({{"cmd", "vote_poll"}, {"id", id}, {"choices", std::move(choices)}});
    } else if (act == "reply") {
        dispatch_cmd({{"cmd", "compose_context"}, {"mode", "reply"}, {"id", id}});
    } else if (act == "boost") {
        dispatch_cmd({{"cmd", "toggle_boost"}, {"id", id}});
    } else if (act == "favorite") {
        dispatch_cmd({{"cmd", "toggle_favorite"}, {"id", id}});
    } else if (act == "quote") {
        dispatch_cmd({{"cmd", "compose_context"}, {"mode", "quote"}, {"id", id}});
    } else if (act == "browser") {
        dispatch_cmd({{"cmd", "open_status"}, {"id", id}});
    } else if (act == "links") {
        dispatch_cmd({{"cmd", "open_post_links"}, {"id", id}});
    } else if (act == "thread") {
        dispatch_cmd({{"cmd", "open_thread"}, {"id", id}});
    } else if (act == "author") {
        dispatch_cmd({{"cmd", "open_user_timeline"}, {"id", id}});
    } else if (act == "delete") {
        if (!settings_.value("confirm_delete_post", true) ||
            confirm("Delete this post? This can't be undone.", "Delete Post"))
            dispatch_cmd({{"cmd", "delete_post"}, {"id", id}});
    } else if (act == "mute_conv") {
        dispatch_cmd({{"cmd", "toggle_mute_conversation"}, {"id", id}});
    } else if (act == "favorited_by") {
        dispatch_cmd({{"cmd", "open_favorited_by"}, {"id", id}});
    } else if (act == "boosted_by") {
        dispatch_cmd({{"cmd", "open_reblogged_by"}, {"id", id}});
    } else if (act == "report") {
        if (auto r = show_report_dialog(GTK_WINDOW(window_), /*remote=*/false)) {
            json cmd = {{"cmd", "report"},
                        {"id", id},
                        {"category", r->category},
                        {"forward", r->forward}};
            if (!r->comment.empty())
                cmd["comment"] = r->comment;
            dispatch_cmd(cmd);
        }
    }
}

void MainWindow::ev_user_profile(const json& e) {
    const std::string account_id = e.value("account_id", std::string{});
    const std::string acct = e.value("acct", std::string{});
    const std::string keep_id = selected_id();
    auto action = show_user_profile_dialog(GTK_WINDOW(window_), e);
    restore_selection(keep_id);
    if (!action)
        return;
    auto set_rel = [&](const char* a) {
        dispatch_cmd({{"cmd", "set_relationship"},
                      {"account_id", account_id},
                      {"acct", acct},
                      {"action", a}});
    };
    const bool following = e.value("following", false);
    const bool requested = e.value("requested", false);
    if (*action == "view_posts")
        dispatch_cmd({{"cmd", "open_user_timeline"}, {"account_id", account_id}, {"acct", acct}});
    else if (*action == "followers")
        dispatch_cmd({{"cmd", "open_followers"}, {"account_id", account_id}, {"acct", acct}});
    else if (*action == "following")
        dispatch_cmd({{"cmd", "open_following"}, {"account_id", account_id}, {"acct", acct}});
    else if (*action == "browser")
        open_url(e.value("url", std::string{}));
    else if (*action == "follow") {
        if (following || requested) {
            if (!settings_.value("confirm_unfollow", false) ||
                confirm("Unfollow @" + acct + "?", "Unfollow"))
                set_rel("unfollow");
        } else if (!settings_.value("confirm_follow", false) ||
                   confirm("Follow @" + acct + "?", "Follow")) {
            set_rel("follow");
        }
    } else if (*action == "mute")
        set_rel(e.value("muting", false) ? "unmute" : "mute");
    else if (*action == "block") {
        if (e.value("blocking", false)) {
            if (!settings_.value("confirm_unblock", false) ||
                confirm("Unblock @" + acct + "?", "Unblock"))
                set_rel("unblock");
        } else if (!settings_.value("confirm_block", true) ||
                   confirm("Block @" + acct + "?", "Block")) {
            set_rel("block");
        }
    } else if (*action == "boosts") {
        set_rel(e.value("showing_reblogs", true) ? "hide_boosts" : "show_boosts");
    } else if (*action == "message") {
        dispatch_cmd({{"cmd", "message_user"}, {"account_id", account_id}, {"acct", acct}});
    } else if (*action == "lists") {
        // Fetch the lists + this user's membership; ev_user_lists opens the
        // checklist when they arrive.
        dispatch_cmd({{"cmd", "get_user_lists"}, {"account_id", account_id}, {"acct", acct}});
    } else if (*action == "report") {
        const bool remote = acct.find('@') != std::string::npos;
        if (auto r = show_report_dialog(GTK_WINDOW(window_), remote)) {
            json cmd = {{"cmd", "report"},         {"account_id", account_id},
                        {"acct", acct},            {"category", r->category},
                        {"forward", r->forward}};
            if (!r->comment.empty())
                cmd["comment"] = r->comment;
            dispatch_cmd(cmd);
        }
    }
}

void MainWindow::ev_account_settings(const json& e) {
    std::vector<std::string> packs;
    for (const auto& p : e.value("soundpacks", json::array()))
        packs.push_back(p.get<std::string>());
    if (packs.empty())
        packs.push_back("Default");
    const std::string acct = e.value("acct", std::string{});
    const std::string current = e.value("soundpack", std::string("Default"));

    const std::string title =
        acct.empty() ? "Account Settings" : "Account Settings for @" + acct;
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        title.c_str(), GTK_WINDOW(window_),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    GtkWidget* label = gtk_label_new_with_mnemonic("_Soundpack for this account:");
    GtkWidget* combo = gtk_combo_box_text_new();
    int active = 0;
    for (size_t i = 0; i < packs.size(); ++i) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), packs[i].c_str());
        if (packs[i] == current)
            active = static_cast<int>(i);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), combo);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, TRUE, TRUE,
                       0);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const int sel = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        if (sel >= 0 && sel < static_cast<int>(packs.size()))
            dispatch_cmd({{"cmd", "set_account_settings"},
                          {"soundpack", packs[static_cast<size_t>(sel)]}});
    }
    gtk_widget_destroy(dialog);
}

void MainWindow::ev_confirm(const json& e) {
    // The core asks, the core wrote the words; we only put a yes/no around them
    // and send its command back if the answer is yes.
    const std::string text = e.value("text", std::string{});
    if (text.empty() || !e.contains("command"))
        return;
    if (confirm(text, e.value("title", std::string("Confirm"))))
        dispatch_cmd(e["command"]);
}

void MainWindow::ev_user_picker(const json& e) {
    const json users = e.value("users", json::array());
    if (users.empty())
        return;
    const std::string purpose = e.value("purpose", std::string{});
    const std::string row_id = e.value("id", std::string{});

    GtkWidget* menu = gtk_menu_new();
    struct Pick {
        MainWindow* win;
        std::string purpose, row_id, account_id, acct;
        bool manual;
    };
    auto on_pick = +[](GtkMenuItem* mi, gpointer) {
        // Copy: the menu (and the attached Pick) is destroyed on an idle that
        // can run inside the modal prompt below.
        const Pick p = *static_cast<Pick*>(g_object_get_data(G_OBJECT(mi), "fastsm-pick"));
        MainWindow* self = p.win;
        if (p.manual) {
            // Act on someone by handle even if they aren't in this post.
            auto handle = self->prompt_text("Type a handle", "Handle (user@instance):");
            if (!handle || handle->empty())
                return;
            if (p.purpose == "timeline")
                self->dispatch_cmd({{"cmd", "open_user_timeline"}, {"handle", *handle}});
            else if (p.purpose == "message")
                self->dispatch_cmd({{"cmd", "message_user"}, {"handle", *handle}});
            else if (p.purpose == "follow_toggle")
                self->dispatch_cmd({{"cmd", "follow_toggle"}, {"handle", *handle}});
            else if (p.purpose == "alias")
                self->dispatch_cmd({{"cmd", "begin_alias"}, {"handle", *handle}});
            else
                self->dispatch_cmd({{"cmd", "open_user_profile"}, {"handle", *handle}});
            return;
        }
        if (p.purpose == "timeline")
            self->dispatch_cmd({{"cmd", "open_user_timeline"},
                                {"account_id", p.account_id},
                                {"acct", p.acct}});
        else if (p.purpose == "message")
            self->dispatch_cmd(
                {{"cmd", "message_user"}, {"account_id", p.account_id}, {"acct", p.acct}});
        else if (p.purpose == "follow_toggle")
            self->dispatch_cmd(
                {{"cmd", "follow_toggle"}, {"account_id", p.account_id}, {"acct", p.acct}});
        else if (p.purpose == "alias")
            self->dispatch_cmd(
                {{"cmd", "begin_alias"}, {"id", p.row_id}, {"account_id", p.account_id}});
        else
            self->dispatch_cmd(
                {{"cmd", "open_user_profile"}, {"id", p.row_id}, {"account_id", p.account_id}});
    };
    auto attach = [&](GtkWidget* item, Pick* pick) {
        g_object_set_data_full(G_OBJECT(item), "fastsm-pick", pick, +[](gpointer d) {
            delete static_cast<Pick*>(d);
        });
        g_signal_connect(item, "activate", G_CALLBACK(on_pick), nullptr);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    };
    for (const auto& u : users) {
        const std::string acct = u.value("acct", std::string{});
        GtkWidget* item = gtk_menu_item_new_with_label(("@" + acct).c_str());
        attach(item, new Pick{this, purpose, row_id, u.value("id", std::string{}), acct, false});
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    GtkWidget* manual = gtk_menu_item_new_with_mnemonic("_Type a handle…");
    attach(manual, new Pick{this, purpose, row_id, {}, {}, true});

    gtk_widget_show_all(menu);
    g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkMenuShell* m, gpointer) {
                         g_idle_add(
                             +[](gpointer w) -> gboolean {
                                 gtk_widget_destroy(GTK_WIDGET(w));
                                 return G_SOURCE_REMOVE;
                             },
                             m);
                     }),
                     nullptr);
    gtk_menu_popup_at_widget(GTK_MENU(menu), posts_view_, GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                             nullptr);
}

namespace {

// A modal list-of-strings manager: a tree view plus action buttons that
// resolve to (button_index, selected_row). Returns nullopt when closed.
std::optional<std::pair<int, int>>
run_list_manager(GtkWindow* parent, const std::string& title,
                 const std::vector<std::string>& rows,
                 const std::vector<std::string>& buttons) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        title.c_str(), parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Close",
        GTK_RESPONSE_CANCEL, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 420);
    for (size_t i = 0; i < buttons.size(); ++i)
        gtk_dialog_add_button(GTK_DIALOG(dialog), buttons[i].c_str(), 100 + static_cast<int>(i));
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), 100);

    GtkListStore* store = gtk_list_store_new(1, G_TYPE_STRING);
    for (const auto& row : rows) {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, row.c_str(), -1);
    }
    GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), gtk_tree_view_column_new_with_attributes(
                                                         "", renderer, "text", 0, nullptr));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
    atk_object_set_name(gtk_widget_get_accessible(view), title.c_str());
    g_signal_connect_swapped(view, "row-activated",
                             G_CALLBACK(+[](gpointer d, GtkTreePath*, GtkTreeViewColumn*) {
                                 gtk_dialog_response(GTK_DIALOG(d), 100); // first action
                             }),
                             dialog);
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(scroll), 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll, TRUE,
                       TRUE, 0);

    gtk_widget_show_all(dialog);
    if (!rows.empty()) {
        GtkTreePath* first = gtk_tree_path_new_first();
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(view), first, nullptr, FALSE);
        gtk_tree_path_free(first);
    }
    gtk_widget_grab_focus(view);

    std::optional<std::pair<int, int>> result;
    const int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response >= 100 && response < 100 + static_cast<int>(buttons.size())) {
        int row = -1; // callers decide whether a button needs a selection
        GtkTreePath* path = nullptr;
        gtk_tree_view_get_cursor(GTK_TREE_VIEW(view), &path, nullptr);
        if (path) {
            row = gtk_tree_path_get_indices(path)[0];
            gtk_tree_path_free(path);
        }
        result = {response - 100, row};
    }
    gtk_widget_destroy(dialog);
    return result;
}

} // namespace

void MainWindow::ev_lists(const json& e) {
    if (!e.value("supported", false)) {
        announce("Lists are only available for Mastodon accounts.");
        return;
    }
    std::vector<json> lists;
    std::vector<std::string> rows;
    for (const auto& l : e.value("lists", json::array())) {
        lists.push_back(l);
        rows.push_back(l.value("title", std::string{}));
    }
    // After create/rename/delete the core re-emits lists, reopening this
    // dialog with the fresh set.
    auto choice = run_list_manager(GTK_WINDOW(window_), "Lists", rows,
                                   {"_Open Timeline", "_Create…", "_Rename…", "_Delete"});
    if (!choice)
        return;
    const int button = choice->first;
    const int row = choice->second;
    if (button == 1) { // Create needs no selection
        if (auto title = prompt_text("Create List", "List name:"); title && !title->empty())
            dispatch_cmd({{"cmd", "create_list"}, {"title", *title}});
        return;
    }
    if (row < 0 || row >= static_cast<int>(lists.size()))
        return;
    const json& l = lists[static_cast<size_t>(row)];
    const std::string id = l.value("id", std::string{});
    const std::string title = l.value("title", std::string{});
    if (button == 0) {
        dispatch_cmd({{"cmd", "spawn_timeline"}, {"kind", "list"}, {"param", id}});
    } else if (button == 2) {
        if (auto name = prompt_text("Rename List", "List name:", title); name && !name->empty())
            dispatch_cmd({{"cmd", "rename_list"}, {"id", id}, {"title", *name}});
    } else if (button == 3) {
        if (confirm("Delete the list \"" + title + "\"?", "Delete List"))
            dispatch_cmd({{"cmd", "delete_list"}, {"id", id}});
    }
}

void MainWindow::ev_user_lists(const json& e) {
    if (!e.value("supported", false))
        return;
    const std::string account_id = e.value("account_id", std::string{});
    const std::string acct = e.value("acct", std::string{});
    const json lists = e.value("lists", json::array());
    if (lists.empty()) {
        announce("You have no lists. Create one from Account, Lists.");
        return;
    }

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        ("Lists for @" + acct).c_str(), GTK_WINDOW(window_),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Check a list to add them to it:"), FALSE,
                       FALSE, 6);
    std::vector<std::pair<std::string, GtkWidget*>> checks; // (list_id, check)
    std::vector<bool> before;
    for (const auto& l : lists) {
        GtkWidget* check =
            gtk_check_button_new_with_label(l.value("title", std::string{}).c_str());
        const bool member = l.value("member", false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), member);
        gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
        checks.emplace_back(l.value("id", std::string{}), check);
        before.push_back(member);
    }
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), box);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 380, 380);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll, TRUE,
                       TRUE, 0);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        for (size_t i = 0; i < checks.size(); ++i) { // apply only the changes
            const bool now = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checks[i].second));
            if (now != static_cast<bool>(before[i]))
                dispatch_cmd({{"cmd", "set_user_list"},
                              {"list_id", checks[i].first},
                              {"account_id", account_id},
                              {"add", now}});
        }
    }
    gtk_widget_destroy(dialog);
}

void MainWindow::ev_followed_hashtags(const json& e) {
    if (!e.value("supported", false)) {
        announce("Following hashtags is only available for Mastodon accounts.");
        return;
    }
    std::vector<std::string> names, rows;
    for (const auto& t : e.value("tags", json::array())) {
        names.push_back(t.value("name", std::string{}));
        rows.push_back("#" + names.back());
    }
    // After Unfollow the core re-emits followed_hashtags, which reopens this
    // dialog with the fresh list.
    auto choice = run_list_manager(GTK_WINDOW(window_), "Followed Hashtags", rows,
                                   {"_Open Timeline", "_Unfollow"});
    if (!choice || choice->second < 0 || choice->second >= static_cast<int>(names.size()))
        return;
    const std::string& name = names[static_cast<size_t>(choice->second)];
    if (choice->first == 0)
        dispatch_cmd({{"cmd", "spawn_timeline"}, {"kind", "hashtag"}, {"value", name}});
    else
        dispatch_cmd({{"cmd", "unfollow_hashtag"}, {"name", name}});
}

void MainWindow::ev_trending_hashtags(const json& e) {
    if (!e.value("supported", false)) {
        announce("Trending hashtags are only available for Mastodon accounts.");
        return;
    }
    std::vector<std::string> names, rows;
    for (const auto& t : e.value("tags", json::array())) {
        names.push_back(t.value("name", std::string{}));
        rows.push_back("#" + names.back());
    }
    auto choice = run_list_manager(GTK_WINDOW(window_), "Trending Hashtags", rows,
                                   {"_Open Timeline", "_Follow"});
    if (!choice || choice->second < 0 || choice->second >= static_cast<int>(names.size()))
        return;
    const std::string& name = names[static_cast<size_t>(choice->second)];
    if (choice->first == 0)
        dispatch_cmd({{"cmd", "spawn_timeline"}, {"kind", "hashtag"}, {"value", name}});
    else
        dispatch_cmd({{"cmd", "follow_hashtag"}, {"name", name}});
}

void MainWindow::ev_alias_prompt(const json& e) {
    const std::string key = e.value("key", std::string{});
    if (key.empty())
        return;
    const std::string handle = e.value("handle", std::string{});
    auto result = prompt_text("User Alias", "Alias for @" + handle + " (empty to clear):",
                              e.value("current", std::string{}));
    if (!result)
        return; // cancelled
    if (result->empty())
        dispatch_cmd({{"cmd", "clear_alias"}, {"key", key}, {"handle", handle}});
    else
        dispatch_cmd({{"cmd", "set_alias"}, {"key", key}, {"handle", handle}, {"alias", *result}});
}

namespace {

struct ExpiryOption {
    const char* label;
    int seconds;
};
constexpr ExpiryOption kExpiryOptions[] = {{"Never", 0},        {"30 minutes", 1800},
                                           {"1 hour", 3600},    {"6 hours", 21600},
                                           {"12 hours", 43200}, {"1 day", 86400},
                                           {"1 week", 604800}};
constexpr const char* kFilterContexts[] = {"home", "notifications", "public", "thread", "account"};
const char* context_label(const std::string& token) {
    if (token == "home")
        return "_Home and lists";
    if (token == "notifications")
        return "_Notifications";
    if (token == "public")
        return "_Public timelines";
    if (token == "thread")
        return "Conversation _threads";
    return "Profile_s";
}

std::string filter_row_label(const json& f) {
    std::string label = f.value("title", std::string{});
    std::string kws;
    if (auto it = f.find("keywords"); it != f.end() && it->is_array())
        for (const auto& k : *it) {
            if (!kws.empty())
                kws += ", ";
            kws += k.value("keyword", std::string{});
        }
    if (!kws.empty())
        label += " \xE2\x80\x94 " + kws; // em dash
    label += f.value("action", std::string("warn")) == "hide" ? " (Hide)" : " (Warn)";
    return label;
}

} // namespace

// Add/edit one Mastodon server filter; prefills from `filter` (empty object =
// Add) and replaces it with the edited JSON on save. Mirrors the Windows
// sub-dialog: title, action, expiry, keywords one-per-line, whole-word, contexts.
bool MainWindow::run_edit_filter_dialog(json& filter) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        filter.contains("id") ? "Edit Filter" : "Add Filter", GTK_WINDOW(window_),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 440, 520);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, TRUE, TRUE,
                       0);

    GtkWidget* title_label = gtk_label_new_with_mnemonic("_Title:");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    GtkWidget* title_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(title_entry), filter.value("title", std::string{}).c_str());
    gtk_label_set_mnemonic_widget(GTK_LABEL(title_label), title_entry);
    gtk_box_pack_start(GTK_BOX(box), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), title_entry, FALSE, FALSE, 0);

    GtkWidget* action_label = gtk_label_new_with_mnemonic("_Action:");
    gtk_label_set_xalign(GTK_LABEL(action_label), 0.0f);
    GtkWidget* action_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(action_combo), "Hide completely");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(action_combo), "Show with warning");
    gtk_combo_box_set_active(GTK_COMBO_BOX(action_combo),
                             filter.value("action", std::string("hide")) == "warn" ? 1 : 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(action_label), action_combo);
    gtk_box_pack_start(GTK_BOX(box), action_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), action_combo, FALSE, FALSE, 0);

    GtkWidget* exp_label = gtk_label_new_with_mnemonic("E_xpires:");
    gtk_label_set_xalign(GTK_LABEL(exp_label), 0.0f);
    GtkWidget* exp_combo = gtk_combo_box_text_new();
    for (const auto& o : kExpiryOptions)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(exp_combo), o.label);
    gtk_combo_box_set_active(GTK_COMBO_BOX(exp_combo), 0); // Never (edits don't restore time left)
    gtk_label_set_mnemonic_widget(GTK_LABEL(exp_label), exp_combo);
    gtk_box_pack_start(GTK_BOX(box), exp_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), exp_combo, FALSE, FALSE, 0);

    GtkWidget* kw_label = gtk_label_new_with_mnemonic("_Keywords (one per line):");
    gtk_label_set_xalign(GTK_LABEL(kw_label), 0.0f);
    GtkWidget* kw_view = gtk_text_view_new();
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(kw_view), FALSE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(kw_label), kw_view);
    atk_object_set_name(gtk_widget_get_accessible(kw_view), "Keywords, one per line");
    bool whole_word = true;
    {
        std::string keywords;
        if (auto it = filter.find("keywords"); it != filter.end() && it->is_array())
            for (const auto& k : *it) {
                if (!keywords.empty())
                    keywords += "\n";
                keywords += k.value("keyword", std::string{});
                whole_word = k.value("whole_word", true);
            }
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(kw_view)),
                                 keywords.c_str(), -1);
    }
    GtkWidget* kw_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(kw_scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(kw_scroll), kw_view);
    gtk_widget_set_vexpand(kw_scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(box), kw_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), kw_scroll, TRUE, TRUE, 0);

    GtkWidget* whole_check = gtk_check_button_new_with_mnemonic("Match _whole words only");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(whole_check), whole_word);
    gtk_box_pack_start(GTK_BOX(box), whole_check, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Filter in:"), FALSE, FALSE, 0);
    std::vector<std::pair<std::string, GtkWidget*>> ctx_checks;
    std::vector<std::string> stored_ctx;
    if (auto it = filter.find("context"); it != filter.end() && it->is_array())
        for (const auto& c : *it)
            if (c.is_string())
                stored_ctx.push_back(c.get<std::string>());
    for (const char* token : kFilterContexts) {
        GtkWidget* check = gtk_check_button_new_with_mnemonic(context_label(token));
        // Default all on for a new filter, else reflect the stored set.
        const bool on = stored_ctx.empty()
                            ? true
                            : std::find(stored_ctx.begin(), stored_ctx.end(), token) !=
                                  stored_ctx.end();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), on);
        gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
        ctx_checks.emplace_back(token, check);
    }

    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(title_entry);

    bool saved = false;
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const std::string title = gtk_entry_get_text(GTK_ENTRY(title_entry));
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(kw_view));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* raw = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        json keywords = json::array();
        const bool whole = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(whole_check));
        {
            std::stringstream ss(raw ? raw : "");
            std::string line;
            while (std::getline(ss, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                    line.pop_back();
                size_t first = line.find_first_not_of(" \t");
                if (first != std::string::npos && first > 0)
                    line = line.substr(first);
                if (!line.empty())
                    keywords.push_back({{"keyword", line}, {"whole_word", whole}});
            }
        }
        g_free(raw);
        json context = json::array();
        for (const auto& [token, check] : ctx_checks)
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)))
                context.push_back(token);
        if (title.empty() || keywords.empty() || context.empty()) {
            announce("A filter needs a title, at least one keyword, and at least one context.");
            continue; // keep the dialog open for fixes
        }
        json out;
        if (auto id = filter.find("id"); id != filter.end() && id->is_string())
            out["id"] = *id; // editing keeps the id
        out["title"] = title;
        out["action"] =
            gtk_combo_box_get_active(GTK_COMBO_BOX(action_combo)) == 1 ? "warn" : "hide";
        out["context"] = std::move(context);
        const int expi = gtk_combo_box_get_active(GTK_COMBO_BOX(exp_combo));
        out["expires_in"] = kExpiryOptions[expi < 0 ? 0 : expi].seconds;
        out["keywords"] = std::move(keywords);
        filter = std::move(out);
        saved = true;
        break;
    }
    gtk_widget_destroy(dialog);
    return saved;
}

void MainWindow::ev_server_filters(const json& e) {
    if (!e.value("supported", false)) {
        announce("Server filters are only available for Mastodon accounts.");
        return;
    }
    std::vector<json> filters;
    std::vector<std::string> rows;
    for (const auto& f : e.value("filters", json::array())) {
        filters.push_back(f);
        rows.push_back(filter_row_label(f));
    }
    // Saves/deletes make the core re-emit server_filters, reopening this fresh.
    auto choice = run_list_manager(GTK_WINDOW(window_), "Server Filters", rows,
                                   {"_Add…", "_Edit…", "_Delete"});
    if (!choice)
        return;
    if (choice->first == 0) { // Add needs no selection
        json f = json::object();
        if (run_edit_filter_dialog(f))
            dispatch_cmd({{"cmd", "save_server_filter"}, {"filter", f}});
        return;
    }
    const int row = choice->second;
    if (row < 0 || row >= static_cast<int>(filters.size()))
        return;
    if (choice->first == 1) {
        json f = filters[static_cast<size_t>(row)];
        if (run_edit_filter_dialog(f))
            dispatch_cmd({{"cmd", "save_server_filter"}, {"filter", f}});
    } else if (choice->first == 2) {
        const json& f = filters[static_cast<size_t>(row)];
        if (confirm("Delete the filter \"" + f.value("title", std::string{}) + "\"?",
                    "Delete Filter"))
            dispatch_cmd({{"cmd", "delete_server_filter"}, {"id", f.value("id", std::string{})}});
    }
}

void MainWindow::ev_profile_editor(const json& e) {
    const bool simple = e.value("simple", false); // Bluesky: name + bio only
    const int max_fields = e.value("max_fields", 4);

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Edit Profile", GTK_WINDOW(window_),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 480, 560);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    GtkWidget* name_label = gtk_label_new_with_mnemonic("Display _name:");
    gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
    GtkWidget* name_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(name_entry), e.value("display_name", std::string{}).c_str());
    gtk_label_set_mnemonic_widget(GTK_LABEL(name_label), name_entry);
    gtk_box_pack_start(GTK_BOX(box), name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), name_entry, FALSE, FALSE, 0);

    GtkWidget* bio_label = gtk_label_new_with_mnemonic("_Bio:");
    gtk_label_set_xalign(GTK_LABEL(bio_label), 0.0f);
    GtkWidget* bio_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(bio_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(bio_view), FALSE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(bio_label), bio_view);
    atk_object_set_name(gtk_widget_get_accessible(bio_view), "Bio");
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(bio_view)),
                             e.value("note", std::string{}).c_str(), -1);
    GtkWidget* bio_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(bio_scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(bio_scroll), bio_view);
    gtk_widget_set_vexpand(bio_scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(box), bio_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), bio_scroll, TRUE, TRUE, 0);

    std::vector<std::pair<GtkWidget*, GtkWidget*>> field_entries; // (name, value)
    GtkWidget* privacy_combo = nullptr;
    std::vector<std::pair<std::string, GtkWidget*>> flag_checks;
    if (!simple) {
        const json fields = e.value("fields", json::array());
        gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Profile metadata (label and content):"),
                           FALSE, FALSE, 0);
        for (int i = 0; i < max_fields; ++i) {
            GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            GtkWidget* fname = gtk_entry_new();
            GtkWidget* fvalue = gtk_entry_new();
            const std::string n = std::to_string(i + 1);
            atk_object_set_name(gtk_widget_get_accessible(fname), ("Field " + n + " label").c_str());
            atk_object_set_name(gtk_widget_get_accessible(fvalue),
                                ("Field " + n + " content").c_str());
            if (i < static_cast<int>(fields.size())) {
                gtk_entry_set_text(GTK_ENTRY(fname),
                                   fields[i].value("name", std::string{}).c_str());
                gtk_entry_set_text(GTK_ENTRY(fvalue),
                                   fields[i].value("value", std::string{}).c_str());
            }
            gtk_box_pack_start(GTK_BOX(row), fname, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(row), fvalue, TRUE, TRUE, 0);
            gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
            field_entries.emplace_back(fname, fvalue);
        }

        GtkWidget* privacy_label = gtk_label_new_with_mnemonic("Default post _privacy:");
        gtk_label_set_xalign(GTK_LABEL(privacy_label), 0.0f);
        privacy_combo = gtk_combo_box_text_new();
        const std::vector<std::pair<std::string, std::string>> privs = {
            {"public", "Public"}, {"unlisted", "Quiet public"}, {"private", "Followers"}};
        const std::string cur = e.value("privacy", std::string("public"));
        int active = 0;
        for (size_t i = 0; i < privs.size(); ++i) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(privacy_combo),
                                           privs[i].second.c_str());
            if (privs[i].first == cur)
                active = static_cast<int>(i);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(privacy_combo), active);
        gtk_label_set_mnemonic_widget(GTK_LABEL(privacy_label), privacy_combo);
        gtk_box_pack_start(GTK_BOX(box), privacy_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), privacy_combo, FALSE, FALSE, 0);

        const std::vector<std::pair<std::string, std::string>> flags = {
            {"locked", "_Require follow requests (locked)"},
            {"bot", "This is an a_utomated (bot) account"},
            {"discoverable", "List in the profile _directory"},
            {"sensitive", "Mark my media as sensiti_ve by default"}};
        for (const auto& [key, label] : flags) {
            GtkWidget* check = gtk_check_button_new_with_mnemonic(label.c_str());
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), e.value(key, false));
            gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
            flag_checks.emplace_back(key, check);
        }
    }

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), box);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll, TRUE,
                       TRUE, 0);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(name_entry);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bio_view));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* note = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        json fields = json::array();
        for (const auto& [fname, fvalue] : field_entries) {
            const std::string n = gtk_entry_get_text(GTK_ENTRY(fname));
            const std::string v = gtk_entry_get_text(GTK_ENTRY(fvalue));
            if (!n.empty() || !v.empty())
                fields.push_back({{"name", n}, {"value", v}});
        }
        json cmd = {{"cmd", "update_profile"},
                    {"display_name", gtk_entry_get_text(GTK_ENTRY(name_entry))},
                    {"note", note ? note : ""},
                    {"fields", std::move(fields)}};
        g_free(note);
        if (privacy_combo) {
            static const char* kPrivs[] = {"public", "unlisted", "private"};
            const int sel = gtk_combo_box_get_active(GTK_COMBO_BOX(privacy_combo));
            cmd["privacy"] = kPrivs[sel < 0 || sel > 2 ? 0 : sel];
        }
        for (const auto& [key, check] : flag_checks)
            cmd[key] =
                static_cast<bool>(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)));
        dispatch_cmd(cmd);
    }
    gtk_widget_destroy(dialog);
}

void MainWindow::ev_aliases_list(const json& e) {
    std::vector<json> aliases;
    std::vector<std::string> rows;
    for (const auto& a : e.value("aliases", json::array())) {
        aliases.push_back(a);
        rows.push_back(a.value("alias", std::string{}) + " (@" +
                       a.value("handle", std::string{}) + ")");
    }
    auto choice =
        run_list_manager(GTK_WINDOW(window_), "Aliases", rows, {"_Edit…", "_Remove"});
    if (!choice || choice->second < 0 || choice->second >= static_cast<int>(aliases.size()))
        return;
    const json& a = aliases[static_cast<size_t>(choice->second)];
    const std::string key = a.value("key", std::string{});
    const std::string handle = a.value("handle", std::string{});
    if (key.empty())
        return;
    if (choice->first == 0) { // Edit
        auto result = prompt_text("User Alias", "Alias for @" + handle + " (empty to clear):",
                                  a.value("alias", std::string{}));
        if (!result)
            return;
        if (result->empty())
            dispatch_cmd({{"cmd", "clear_alias"}, {"key", key}, {"handle", handle}});
        else
            dispatch_cmd(
                {{"cmd", "set_alias"}, {"key", key}, {"handle", handle}, {"alias", *result}});
    } else { // Remove
        dispatch_cmd({{"cmd", "clear_alias"}, {"key", key}, {"handle", handle}});
    }
    dispatch_cmd({{"cmd", "list_aliases"}}); // refresh (reopens with the new list)
}

void MainWindow::ev_client_filter(const json& e) {
    if (!e.value("available", false)) {
        announce("Open a timeline first to filter it.");
        return;
    }
    const json f = e.value("filter", json::object());

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Client Filters", GTK_WINDOW(window_),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, "C_lear", 90, "_Apply", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Show these kinds of posts:"), FALSE, FALSE,
                       6);
    // Field key + label, mirroring the core's ClientFilter / the Windows dialog.
    const std::vector<std::pair<std::string, std::string>> kinds = {
        {"original", "_Original posts"},   {"replies", "_Replies"},
        {"replies_to_me", "Replies to _me"}, {"threads", "_Threads"},
        {"replies_to_unfollowed", "Bluesky replies to people you don't _follow"},
        {"boosts", "_Boosts"},             {"quotes", "_Quotes"},
        {"media", "Posts with me_dia"},    {"no_media", "Posts _without media"},
        {"my_posts", "M_y posts"},         {"my_replies", "My repl_ies"}};
    std::vector<std::pair<std::string, GtkWidget*>> checks;
    for (const auto& [key, label] : kinds) {
        GtkWidget* check = gtk_check_button_new_with_mnemonic(label.c_str());
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), f.value(key, true));
        gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 0);
        checks.emplace_back(key, check);
    }
    GtkWidget* text_label = gtk_label_new_with_mnemonic("Only posts containing te_xt:");
    gtk_label_set_xalign(GTK_LABEL(text_label), 0.0f);
    GtkWidget* text_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(text_entry), f.value("text", std::string{}).c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(text_entry), TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(text_label), text_entry);
    gtk_box_pack_start(GTK_BOX(box), text_label, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(box), text_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, TRUE, TRUE,
                       0);
    gtk_widget_show_all(dialog);

    const int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        json nf;
        for (const auto& [key, check] : checks)
            nf[key] = static_cast<bool>(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)));
        nf["text"] = gtk_entry_get_text(GTK_ENTRY(text_entry));
        dispatch_cmd({{"cmd", "set_client_filter"}, {"filter", nf}});
        announce("Filter applied.");
    } else if (response == 90) {
        dispatch_cmd({{"cmd", "clear_client_filter"}});
        announce("Filter cleared.");
    }
    gtk_widget_destroy(dialog);
}

void MainWindow::ev_hashtag_prompt(const json& e) {
    std::string prefill;
    if (const json tags = e.value("tags", json::array()); !tags.empty() && tags[0].is_string())
        prefill = tags[0].get<std::string>();
    auto name = prompt_text("Follow Hashtag", "Hashtag to follow:", prefill);
    if (name && !name->empty())
        dispatch_cmd({{"cmd", "follow_hashtag"}, {"name", *name}});
}

void MainWindow::ev_url_picker(const json& e) {
    const json links = e.value("links", json::array());
    if (links.empty())
        return;
    GtkWidget* menu = gtk_menu_new();
    for (const auto& l : links) {
        const std::string title = l.value("title", std::string{});
        const std::string url = l.value("url", std::string{});
        // Show the title with the actual URL in parentheses (unless equal).
        const std::string label = (title.empty() || title == url) ? url : title + " (" + url + ")";
        GtkWidget* item = gtk_menu_item_new_with_label(label.c_str());
        g_object_set_data_full(G_OBJECT(item), "fastsm-url", g_strdup(url.c_str()), g_free);
        g_signal_connect(item, "activate",
                         G_CALLBACK(+[](GtkMenuItem* mi, gpointer u) {
                             auto* self = static_cast<MainWindow*>(u);
                             const auto* url = static_cast<const char*>(
                                 g_object_get_data(G_OBJECT(mi), "fastsm-url"));
                             if (url)
                                 self->open_url(url);
                         }),
                         this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);
    // Destroy after the click lands: the item's activate fires after popdown,
    // so defer destruction to an idle instead of destroying on deactivate.
    g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkMenuShell* m, gpointer) {
                         g_idle_add(
                             +[](gpointer w) -> gboolean {
                                 gtk_widget_destroy(GTK_WIDGET(w));
                                 return G_SOURCE_REMOVE;
                             },
                             m);
                     }),
                     nullptr);
    gtk_menu_popup_at_widget(GTK_MENU(menu), posts_view_, GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                             nullptr);
}

void MainWindow::ev_hashtag_timeline_picker(const json& e) {
    const json tags = e.value("tags", json::array());
    if (tags.empty())
        return;
    GtkWidget* menu = gtk_menu_new();
    for (const auto& t : tags) {
        if (!t.is_string())
            continue;
        const std::string tag = t.get<std::string>();
        GtkWidget* item = gtk_menu_item_new_with_label(("#" + tag).c_str());
        g_object_set_data_full(G_OBJECT(item), "fastsm-tag", g_strdup(tag.c_str()), g_free);
        g_signal_connect(item, "activate",
                         G_CALLBACK(+[](GtkMenuItem* mi, gpointer u) {
                             auto* self = static_cast<MainWindow*>(u);
                             const auto* tag = static_cast<const char*>(
                                 g_object_get_data(G_OBJECT(mi), "fastsm-tag"));
                             if (tag)
                                 self->dispatch_cmd({{"cmd", "spawn_timeline"},
                                                     {"kind", "hashtag"},
                                                     {"value", tag}});
                         }),
                         this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);
    // Destroy after the click lands (activate fires after popdown) — defer to idle.
    g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkMenuShell* m, gpointer) {
                         g_idle_add(
                             +[](gpointer w) -> gboolean {
                                 gtk_widget_destroy(GTK_WIDGET(w));
                                 return G_SOURCE_REMOVE;
                             },
                             m);
                     }),
                     nullptr);
    gtk_menu_popup_at_widget(GTK_MENU(menu), posts_view_, GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                             nullptr);
}

// -------------------------------------------------------------- keyboard ---

gboolean MainWindow::on_posts_key(GtkWidget*, GdkEventKey* event, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    const guint state = event->state & gtk_accelerator_get_default_mod_mask();
    const bool shift = state & GDK_SHIFT_MASK;
    const bool ctrl = state & GDK_CONTROL_MASK;
    const bool alt = state & GDK_MOD1_MASK;
    const guint lower = gdk_keyval_to_lower(event->keyval);

    // Shift+letter: first-letter navigation to the next post whose spoken text
    // starts with that letter. Plain letters stay post actions (below).
    if (shift && !ctrl && !alt && lower >= GDK_KEY_a && lower <= GDK_KEY_z) {
        self->first_letter_nav(static_cast<gunichar>('a' + (lower - GDK_KEY_a)));
        return TRUE;
    }
    // Shift+Enter: the secondary interact action (Behavior settings). Follow
    // requests and user lists keep their Enter behavior even with Shift held.
    if (shift && !ctrl && !alt && lower == GDK_KEY_Return) {
        Timeline* tc = self->current();
        const int row = self->selected_row();
        if (tc && row >= 0 && row < static_cast<int>(tc->rows.size()) &&
            tc->rows[static_cast<size_t>(row)].follow_request)
            self->do_follow_request_action(tc->rows[static_cast<size_t>(row)]);
        else if (tc && tc->user_list)
            self->do_enter_user_action();
        else
            self->do_secondary_post_action();
        return TRUE;
    }
    // Ctrl+arrows: movement units — jump by the active unit, cycle which one.
    if (ctrl && !alt && !shift) {
        switch (lower) {
        case GDK_KEY_Up:
        case GDK_KEY_Down: {
            const std::string from = self->selected_id();
            if (!from.empty())
                self->dispatch_cmd({{"cmd", "move"},
                                    {"from_id", from},
                                    {"dir", lower == GDK_KEY_Up ? "prev" : "next"}});
            return TRUE;
        }
        case GDK_KEY_Left:
            self->dispatch_cmd({{"cmd", "cycle_movement"}, {"dir", "prev"}});
            return TRUE;
        case GDK_KEY_Right:
            self->dispatch_cmd({{"cmd", "cycle_movement"}, {"dir", "next"}});
            return TRUE;
        default:
            break;
        }
    }
    if (ctrl || alt || shift)
        return FALSE;

    switch (lower) {
    case GDK_KEY_Up:
    case GDK_KEY_Down: {
        Timeline* tc = self->current();
        const int count = tc ? static_cast<int>(tc->rows.size()) : 0;
        const int row = self->selected_row();
        const bool at_edge =
            (lower == GDK_KEY_Up && row <= 0) || (lower == GDK_KEY_Down && row >= count - 1);
        if (at_edge && count > 0 && self->settings_.value("boundary_sound", true))
            self->dispatch_cmd({{"cmd", "play_earcon"}, {"name", "boundary"}});
        return FALSE; // let the list move the cursor
    }
    case GDK_KEY_Left:
        self->dispatch_cmd({{"cmd", "select_timeline"}, {"dir", "prev"}});
        return TRUE;
    case GDK_KEY_Right:
        self->dispatch_cmd({{"cmd", "select_timeline"}, {"dir", "next"}});
        return TRUE;
    case GDK_KEY_b:
        self->do_boost();
        return TRUE;
    case GDK_KEY_f:
        self->do_favorite();
        return TRUE;
    case GDK_KEY_m:
        self->do_bookmark();
        return TRUE;
    case GDK_KEY_a:
        self->dispatch_cmd({{"cmd", "toggle_auto_read"}});
        return TRUE;
    case GDK_KEY_p: {
        const std::string id = self->selected_id();
        if (!id.empty())
            self->dispatch_cmd({{"cmd", "toggle_pin_post"}, {"id", id}});
        return TRUE;
    }
    case GDK_KEY_space: { // open the post's thread (Mac parity)
        const std::string id = self->selected_id();
        if (!id.empty())
            self->dispatch_cmd({{"cmd", "open_thread"}, {"id", id}});
        return TRUE;
    }
    case GDK_KEY_Return: { // follow requests and user lists first; else the
                           // configurable interact action (default: post info)
        Timeline* tc = self->current();
        const int row = self->selected_row();
        if (tc && row >= 0 && row < static_cast<int>(tc->rows.size()) &&
            tc->rows[static_cast<size_t>(row)].follow_request) {
            self->do_follow_request_action(tc->rows[static_cast<size_t>(row)]);
            return TRUE;
        }
        if (tc && tc->user_list) {
            self->do_enter_user_action();
            return TRUE;
        }
        self->do_enter_post_action();
        return TRUE;
    }
    case GDK_KEY_d: { // direct-message someone in the post (M is taken by bookmark)
        const std::string id = self->selected_id();
        if (!id.empty())
            self->dispatch_cmd({{"cmd", "message_user"}, {"id", id}, {"pick", true}});
        return TRUE;
    }
    case GDK_KEY_u: { // open the author's posts
        const std::string id = self->selected_id();
        if (!id.empty())
            self->dispatch_cmd({{"cmd", "open_user_timeline"}, {"id", id}, {"pick", true}});
        return TRUE;
    }
    case GDK_KEY_h: // follow a hashtag; prompt pre-fills with this post's hashtags
        self->dispatch_cmd({{"cmd", "follow_hashtag_prompt"}, {"id", self->selected_id()}});
        return TRUE;
    case GDK_KEY_r:
        self->compose("reply");
        return TRUE;
    case GDK_KEY_q:
        self->compose("quote");
        return TRUE;
    case GDK_KEY_e:
        self->compose("edit");
        return TRUE;
    case GDK_KEY_Delete:
        self->do_delete_post();
        return TRUE;
    case GDK_KEY_period:
        self->do_load_older();
        return TRUE;
    default:
        return FALSE;
    }
}

void MainWindow::on_posts_cursor_changed(GtkTreeView*, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    if (self->updating_)
        return;
    Timeline* tc = self->current();
    const int row = self->selected_row();
    if (!tc || row < 0 || row >= static_cast<int>(tc->rows.size()))
        return;
    tc->selected_id = tc->rows[static_cast<size_t>(row)].id;
    self->dispatch_cmd({{"cmd", "note_selection"}, {"id", tc->selected_id}});
    self->maybe_load_older(row);
}

void MainWindow::on_timelines_cursor_changed(GtkTreeView* view, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    if (self->updating_)
        return;
    GtkTreePath* path = nullptr;
    gtk_tree_view_get_cursor(view, &path, nullptr);
    if (!path)
        return;
    const int row = gtk_tree_path_get_indices(path)[0];
    gtk_tree_path_free(path);
    if (row >= 0 && row != self->current_)
        self->dispatch_cmd({{"cmd", "select_timeline"}, {"index", row}});
}

} // namespace fastsmgtk
