#include "settings_dialog.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>

#include "fastsm/timeline/movement.hpp"

#include "speech_detail_dialog.hpp"

using fastsm::present::CwMode;
using fastsm::present::EmojiRemoval;
using fastsm::store::AppSettings;

namespace fastsmgtk {

namespace {

// Builds labeled rows onto notebook pages and records how to read each widget
// back into the settings struct when the user accepts.
struct Builder {
    GtkWidget* notebook = nullptr;
    std::vector<std::function<void()>> apply;

    GtkWidget* page(const char* title) {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_container_set_border_width(GTK_CONTAINER(box), 12);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box,
                                 gtk_label_new_with_mnemonic(title));
        return box;
    }

    void check(GtkWidget* box, const char* label, bool* field) {
        GtkWidget* button = gtk_check_button_new_with_mnemonic(label);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), *field);
        gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
        apply.push_back([button, field] {
            *field = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
        });
    }

    GtkWidget* labeled(GtkWidget* box, const char* label, GtkWidget* control) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget* l = gtk_label_new_with_mnemonic(label);
        gtk_label_set_mnemonic_widget(GTK_LABEL(l), control);
        gtk_box_pack_start(GTK_BOX(row), l, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), control, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
        return control;
    }

    // Combo over (value, label) pairs bound to a string field.
    void combo(GtkWidget* box, const char* label,
               const std::vector<std::pair<std::string, std::string>>& options,
               std::string* field) {
        GtkWidget* c = gtk_combo_box_text_new();
        int active = 0;
        for (size_t i = 0; i < options.size(); ++i) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), options[i].second.c_str());
            if (options[i].first == *field)
                active = static_cast<int>(i);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
        labeled(box, label, c);
        apply.push_back([c, options, field] {
            const int sel = gtk_combo_box_get_active(GTK_COMBO_BOX(c));
            if (sel >= 0 && sel < static_cast<int>(options.size()))
                *field = options[static_cast<size_t>(sel)].first;
        });
    }

    // Combo over labels bound to an int (enum value = index, or values[]).
    void combo_int(GtkWidget* box, const char* label, const std::vector<std::string>& labels,
                   const std::vector<int>& values, int* field) {
        GtkWidget* c = gtk_combo_box_text_new();
        int active = 0;
        for (size_t i = 0; i < labels.size(); ++i) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), labels[i].c_str());
            if (values[i] == *field)
                active = static_cast<int>(i);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
        labeled(box, label, c);
        apply.push_back([c, values, field] {
            const int sel = gtk_combo_box_get_active(GTK_COMBO_BOX(c));
            if (sel >= 0 && sel < static_cast<int>(values.size()))
                *field = values[static_cast<size_t>(sel)];
        });
    }

    void spin(GtkWidget* box, const char* label, int min, int max, int* field) {
        GtkWidget* s = gtk_spin_button_new_with_range(min, max, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), *field);
        labeled(box, label, s);
        apply.push_back([s, field] {
            *field = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(s));
        });
    }

    void entry(GtkWidget* box, const char* label, std::string* field) {
        GtkWidget* e = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(e), field->c_str());
        labeled(box, label, e);
        apply.push_back([e, field] { *field = gtk_entry_get_text(GTK_ENTRY(e)); });
    }
};

// The active movement units and their cycle order: toggle and reorder, same as
// the Windows Movement Units sub-dialog off the Timelines page.
void edit_movement_units(GtkWindow* parent,
                         std::vector<AppSettings::MovementUnitPref>& prefs) {
    using fastsm::MovementUnit;
    const auto catalog = MovementUnit::catalog();
    // Normalize like the core: saved order first (unknowns dropped), then any
    // catalog unit not yet saved appended enabled.
    std::vector<AppSettings::MovementUnitPref> units;
    for (const auto& p : prefs) {
        for (const auto& u : catalog)
            if (u.key() == p.unit) {
                units.push_back(p);
                break;
            }
    }
    for (const auto& u : catalog) {
        bool seen = false;
        for (const auto& p : units)
            seen = seen || p.unit == u.key();
        if (!seen)
            units.push_back({u.key(), true});
    }
    auto title_of = [](const std::string& key) {
        MovementUnit u;
        return MovementUnit::from_key(key, u) ? u.title() : key;
    };

    // A reorderable checked list, matching the Windows/Mac editor exactly:
    // Space toggles the focused row's checkbox, Ctrl+Up/Ctrl+Down move it.
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Movement Units", parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 380, 400);

    GtkListStore* store = gtk_list_store_new(2, G_TYPE_BOOLEAN, G_TYPE_STRING);
    GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    GtkCellRenderer* toggle = gtk_cell_renderer_toggle_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), gtk_tree_view_column_new_with_attributes(
                                                         "", toggle, "active", 0, nullptr));
    gtk_tree_view_append_column(
        GTK_TREE_VIEW(view), gtk_tree_view_column_new_with_attributes(
                                 "", gtk_cell_renderer_text_new(), "text", 1, nullptr));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
    atk_object_set_name(gtk_widget_get_accessible(view), "Movement units");
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(scroll), 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll, TRUE,
                       TRUE, 0);

    struct Ctx {
        std::vector<AppSettings::MovementUnitPref>* units;
        GtkListStore* store;
        GtkTreeView* view;
        std::string (*title_of)(const std::string&);
    };
    static std::string (*title_fn)(const std::string&) = +[](const std::string& key) {
        MovementUnit u;
        return MovementUnit::from_key(key, u) ? u.title() : key;
    };
    static Ctx ctx; // modal + single-threaded
    ctx = {&units, store, GTK_TREE_VIEW(view), title_fn};

    auto refill = +[](Ctx* c, int cursor) {
        gtk_list_store_clear(c->store);
        for (const auto& p : *c->units) {
            GtkTreeIter iter;
            gtk_list_store_append(c->store, &iter);
            gtk_list_store_set(c->store, &iter, 0, p.enabled ? TRUE : FALSE, 1,
                               c->title_of(p.unit).c_str(), -1);
        }
        if (cursor >= 0 && cursor < static_cast<int>(c->units->size())) {
            GtkTreePath* path = gtk_tree_path_new_from_indices(cursor, -1);
            gtk_tree_view_set_cursor(c->view, path, nullptr, FALSE);
            gtk_tree_path_free(path);
        }
    };
    auto toggle_row = +[](Ctx* c, int row) {
        if (row < 0 || row >= static_cast<int>(c->units->size()))
            return;
        auto& pref = (*c->units)[static_cast<size_t>(row)];
        pref.enabled = !pref.enabled;
        GtkTreeIter iter;
        if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(c->store), &iter, nullptr, row))
            gtk_list_store_set(c->store, &iter, 0, pref.enabled ? TRUE : FALSE, -1);
    };
    static decltype(refill) refill_fn;
    static decltype(toggle_row) toggle_fn;
    refill_fn = refill;
    toggle_fn = toggle_row;

    // Mouse click on the checkbox cell.
    g_signal_connect(toggle, "toggled",
                     G_CALLBACK(+[](GtkCellRendererToggle*, gchar* path_str, gpointer) {
                         toggle_fn(&ctx, atoi(path_str));
                     }),
                     nullptr);
    // Space toggles; Ctrl+Up/Ctrl+Down reorder (Windows/Mac parity).
    g_signal_connect(
        view, "key-press-event",
        G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer) -> gboolean {
            const guint state = event->state & gtk_accelerator_get_default_mod_mask();
            GtkTreePath* path = nullptr;
            gtk_tree_view_get_cursor(ctx.view, &path, nullptr);
            int row = -1;
            if (path) {
                row = gtk_tree_path_get_indices(path)[0];
                gtk_tree_path_free(path);
            }
            if (event->keyval == GDK_KEY_space && state == 0) {
                toggle_fn(&ctx, row);
                return TRUE;
            }
            if ((event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down) &&
                state == GDK_CONTROL_MASK && row >= 0) {
                const int to = event->keyval == GDK_KEY_Up ? row - 1 : row + 1;
                if (to >= 0 && to < static_cast<int>(ctx.units->size())) {
                    std::swap((*ctx.units)[static_cast<size_t>(row)],
                              (*ctx.units)[static_cast<size_t>(to)]);
                    refill_fn(&ctx, to);
                }
                return TRUE;
            }
            return FALSE;
        }),
        nullptr);

    refill(&ctx, 0);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(view);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
        prefs = std::move(units);
    gtk_widget_destroy(dialog);
}

// Edit one category's typed speech-field list via the shared checked-list
// modal (mirrors edit_speech in windows/src/settings_dialog.cpp).
template <class Field>
void edit_speech_items(GtkWindow* parent, const std::string& title,
                       std::vector<fastsm::present::SpeechItem<Field>>& items) {
    std::vector<SpeechDetailRow> rows;
    for (const auto& it : items)
        rows.push_back({static_cast<int>(it.field),
                        fastsm::present::field_display_name(it.field), it.enabled, it.before,
                        it.after, it.no_separator_after});
    if (!run_speech_detail(parent, title, rows, /*with_wrap=*/true))
        return;
    std::vector<fastsm::present::SpeechItem<Field>> out;
    for (const auto& r : rows) {
        fastsm::present::SpeechItem<Field> item(static_cast<Field>(r.id), r.enabled);
        item.before = r.before;
        item.after = r.after;
        item.no_separator_after = r.no_sep_after;
        out.push_back(std::move(item));
    }
    items = std::move(out);
}

// Context for the seven Configure… buttons (modal + single-threaded).
struct SpeechBtnCtx {
    GtkWidget* dialog = nullptr;
    AppSettings* settings = nullptr;
};
SpeechBtnCtx speech_btn_ctx;

const std::vector<std::string> kEmojiLabels = {"Off (keep emoji)", "Unicode emoji",
                                               "Custom :shortcode: emoji", "Both"};
const std::vector<int> kEmojiValues = {static_cast<int>(EmojiRemoval::None),
                                       static_cast<int>(EmojiRemoval::Unicode),
                                       static_cast<int>(EmojiRemoval::Mastodon),
                                       static_cast<int>(EmojiRemoval::Both)};

} // namespace

std::optional<AppSettings> show_settings_dialog(GtkWindow* parent, AppSettings settings,
                                                const AudioChoices& audio) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Settings", parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 520);

    Builder b;
    b.notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), b.notebook, TRUE,
                       TRUE, 0);

    // --- General (mirrors the Windows page order throughout) ---
    {
        GtkWidget* p = b.page("_General");
        b.check(p, "Pressing _Enter in the composer sends the post (else Ctrl+Enter)",
                &settings.enter_to_send);
    }
    // --- Timelines ---
    {
        GtkWidget* p = b.page("_Timelines");
        b.spin(p, "Posts _cached per timeline (0 turns caching off):", AppSettings::kCacheLimitMin,
               AppSettings::kCacheLimitMax, &settings.cache_limit);
        std::vector<std::string> labels;
        std::vector<int> values;
        for (int secs : AppSettings::kAutoRefreshOptions) {
            values.push_back(secs);
            labels.push_back(secs == 0 ? "Off"
                                       : (secs < 60 ? std::to_string(secs) + " seconds"
                                                    : std::to_string(secs / 60) + " minute" +
                                                          (secs > 60 ? "s" : "")));
        }
        b.combo_int(p, "Auto-_refresh:", labels, values, &settings.auto_refresh_seconds);
        b.check(p, "_Stream in real time (Mastodon)", &settings.streaming_enabled);
        b.check(p, "Show m_entions in the Notifications timeline",
                &settings.show_mentions_in_notifications);
        b.check(p, "Re_verse timelines (newest at the bottom)", &settings.reverse_timelines);
        b.check(p, "Automatically load _older posts when you reach the end",
                &settings.auto_load_older);
        b.check(p, "S_ync home position with the server (Mastodon)",
                &settings.sync_home_position);
        GtkWidget* mu_button = gtk_button_new_with_mnemonic("Mo_vement Units…");
        struct MuCtx {
            GtkWidget* dialog;
            AppSettings* settings;
        };
        static MuCtx mu_ctx;
        mu_ctx = {dialog, &settings};
        g_signal_connect(mu_button, "clicked",
                         G_CALLBACK(+[](GtkButton*, gpointer u) {
                             auto* c = static_cast<MuCtx*>(u);
                             edit_movement_units(GTK_WINDOW(c->dialog),
                                                 c->settings->movement_units);
                         }),
                         &mu_ctx);
        gtk_box_pack_start(GTK_BOX(p), mu_button, FALSE, FALSE, 0);
    }
    // --- Audio ---
    {
        GtkWidget* p = b.page("_Audio");
        b.check(p, "_Play sounds", &settings.sounds_enabled);
        // "System default" is the empty setting; a saved device that isn't here
        // right now is kept as an entry of its own so simply opening Settings
        // with the headset unplugged doesn't reset the choice.
        auto device_options = [](const std::vector<std::string>& devices,
                                 const std::string& current) {
            std::vector<std::pair<std::string, std::string>> options{{"", "System default"}};
            bool found = false;
            for (const auto& d : devices) {
                options.push_back({d, d});
                found = found || d == current;
            }
            if (!current.empty() && !found)
                options.push_back({current, current + " (not connected)"});
            return options;
        };
        std::vector<std::pair<std::string, std::string>> packs;
        bool have_current = false;
        for (const auto& pack : audio.soundpacks) {
            packs.push_back({pack, pack});
            have_current = have_current || pack == settings.soundpack;
        }
        if (packs.empty() || !have_current)
            packs.insert(packs.begin(), {settings.soundpack, settings.soundpack});
        b.combo(p, "Sound_pack:", packs, &settings.soundpack);
        b.spin(p, "Sound effect vo_lume (percent):", 0, 100, &settings.sound_volume);
        b.spin(p, "_Media volume (percent):", 0, 100, &settings.media_volume);
        b.combo(p, "Sound effects _device:",
                device_options(audio.sound_devices, settings.sound_device),
                &settings.sound_device);
        b.combo(p, "Media d_evice:", device_options(audio.media_devices, settings.media_device),
                &settings.media_device);
        b.check(p, "Play a _boundary chime at the top or bottom of a timeline",
                &settings.boundary_sound);
    }
    // --- Earcons ---
    {
        GtkWidget* p = b.page("Ear_cons");
        gtk_box_pack_start(GTK_BOX(p),
                           gtk_label_new("Play a short sound when moving onto a post with:"),
                           FALSE, FALSE, 0);
        b.check(p, "_Image (post has an image)", &settings.earcon_image);
        b.check(p, "_Media (post has video, audio, or a GIF)", &settings.earcon_media);
        b.check(p, "M_ention (post mentions you)", &settings.earcon_mention);
        b.check(p, "_Pinned (post is pinned to a profile)", &settings.earcon_pinned);
        b.check(p, "P_oll (post has a poll)", &settings.earcon_poll);
    }
    // --- Speech ---
    {
        GtkWidget* p = b.page("_Speech");
        gtk_box_pack_start(GTK_BOX(p),
                           gtk_label_new("Configure what is spoken for each kind of row:"),
                           FALSE, FALSE, 0);
        speech_btn_ctx = {dialog, &settings};
        GtkWidget* btn_grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(btn_grid), 6);
        gtk_grid_set_column_spacing(GTK_GRID(btn_grid), 6);
        int btn_n = 0;
        auto speech_btn = [&](const char* label, GCallback cb) {
            GtkWidget* button = gtk_button_new_with_mnemonic(label);
            g_signal_connect(button, "clicked", cb, nullptr);
            gtk_grid_attach(GTK_GRID(btn_grid), button, btn_n % 2, btn_n / 2, 1, 1);
            ++btn_n;
        };
        speech_btn("Configure _Posts…", G_CALLBACK(+[](GtkButton*, gpointer) {
                       edit_speech_items(GTK_WINDOW(speech_btn_ctx.dialog),
                                         "Speech Details — Posts",
                                         speech_btn_ctx.settings->speech.status);
                   }));
        speech_btn("Configure _Users…", G_CALLBACK(+[](GtkButton*, gpointer) {
                       edit_speech_items(GTK_WINDOW(speech_btn_ctx.dialog),
                                         "Speech Details — Users",
                                         speech_btn_ctx.settings->speech.user);
                   }));
        speech_btn("Configure _Notifications…", G_CALLBACK(+[](GtkButton*, gpointer) {
                       edit_speech_items(GTK_WINDOW(speech_btn_ctx.dialog),
                                         "Speech Details — Notifications",
                                         speech_btn_ctx.settings->speech.notification);
                   }));
        speech_btn("Auto-r_ead…", G_CALLBACK(+[](GtkButton*, gpointer) {
                       edit_speech_items(GTK_WINDOW(speech_btn_ctx.dialog),
                                         "Speech Details — Auto-read Posts",
                                         speech_btn_ctx.settings->speech.autoread);
                   }));
        speech_btn("Copy: Pos_ts…", G_CALLBACK(+[](GtkButton*, gpointer) {
                       edit_speech_items(GTK_WINDOW(speech_btn_ctx.dialog),
                                         "Copy Details — Posts",
                                         speech_btn_ctx.settings->speech.copy_status);
                   }));
        speech_btn("Copy: Us_ers…", G_CALLBACK(+[](GtkButton*, gpointer) {
                       edit_speech_items(GTK_WINDOW(speech_btn_ctx.dialog),
                                         "Copy Details — Users",
                                         speech_btn_ctx.settings->speech.copy_user);
                   }));
        speech_btn("Copy: Not_ifications…", G_CALLBACK(+[](GtkButton*, gpointer) {
                       edit_speech_items(GTK_WINDOW(speech_btn_ctx.dialog),
                                         "Copy Details — Notifications",
                                         speech_btn_ctx.settings->speech.copy_notification);
                   }));
        gtk_box_pack_start(GTK_BOX(p), btn_grid, FALSE, FALSE, 0);
        int cw = static_cast<int>(settings.text.cw);
        static int cw_holder; // enum bridged through an int field
        cw_holder = cw;
        b.combo_int(p, "Content _warnings:",
                    {"Show the warning, hide the post", "Show the warning and the post",
                     "Ignore the warning, show the post"},
                    {static_cast<int>(CwMode::Hide), static_cast<int>(CwMode::Show),
                     static_cast<int>(CwMode::Ignore)},
                    &cw_holder);
        b.apply.push_back([&settings] { settings.text.cw = static_cast<CwMode>(cw_holder); });
        static int post_emoji_holder;
        post_emoji_holder = static_cast<int>(settings.text.post_emoji);
        b.combo_int(p, "Remove emoji from post _text:", kEmojiLabels, kEmojiValues,
                    &post_emoji_holder);
        b.apply.push_back([&settings] {
            settings.text.post_emoji = static_cast<EmojiRemoval>(post_emoji_holder);
        });
        static int name_emoji_holder;
        name_emoji_holder = static_cast<int>(settings.text.name_emoji);
        b.combo_int(p, "Remove emoji from display _names:", kEmojiLabels, kEmojiValues,
                    &name_emoji_holder);
        b.apply.push_back([&settings] {
            settings.text.name_emoji = static_cast<EmojiRemoval>(name_emoji_holder);
        });
        b.spin(p, "Collapse leading @_mentions beyond (0 = read all):",
               AppSettings::kMaxMentionsMin, AppSettings::kMaxMentionsMax,
               &settings.text.max_mentions);
        b.check(p,
                "_Repeat the item when you hit the top or bottom of a timeline (invisible "
                "interface / layer)",
                &settings.invisible_repeat_at_edge);
        b.check(p, "Speak _absolute times instead of \"5 minutes ago\"",
                &settings.text.absolute_time);
        b.entry(p, "Separator spoken between _fields:", &settings.speech.separator);
    }
    // --- Advanced ---
    {
        GtkWidget* p = b.page("Ad_vanced");
        b.spin(p, "API calls per _load (about 40 posts each):", AppSettings::kFetchPagesMin,
               AppSettings::kFetchPagesMax, &settings.fetch_pages);
    }
    // --- Confirmations ---
    {
        GtkWidget* p = b.page("Confir_mations");
        gtk_box_pack_start(GTK_BOX(p), gtk_label_new("Ask for confirmation before:"), FALSE,
                           FALSE, 0);
        b.check(p, "_Boosting", &settings.confirm_boost);
        b.check(p, "Unb_oosting", &settings.confirm_unboost);
        b.check(p, "_Favoriting", &settings.confirm_favorite);
        b.check(p, "Un_favoriting", &settings.confirm_unfavorite);
        b.check(p, "_Clearing a timeline", &settings.confirm_clear_timeline);
        b.check(p, "Fo_llowing a user", &settings.confirm_follow);
        b.check(p, "U_nfollowing a user", &settings.confirm_unfollow);
        b.check(p, "Bloc_king a user", &settings.confirm_block);
        b.check(p, "_Unblocking a user", &settings.confirm_unblock);
        b.check(p, "_Deleting a post", &settings.confirm_delete_post);
    }
    // --- Behavior ---
    {
        GtkWidget* p = b.page("_Behavior");
        b.combo(p, "Pressing _Enter on a post:",
                {{"post_info", "Post information"},
                 {"thread", "View thread"},
                 {"reply", "Reply"},
                 {"links", "Open links"}},
                &settings.enter_post_action);
        b.combo(p, "Pressing Enter on a _user:",
                {{"actions", "User actions"}, {"profile", "Profile"}, {"timeline", "Their posts"}},
                &settings.enter_user_action);
        b.combo(p, "Shift+Enter (_secondary action) on a post:",
                {{"play_media", "Play media"},
                 {"post_info", "Post information"},
                 {"thread", "View thread"},
                 {"reply", "Reply"},
                 {"links", "Open links"}},
                &settings.secondary_post_action);
        b.check(p, "Play _audio in the background (no player window)",
                &settings.media_background);
        b.check(p,
                "In a _reply, mention the person you're replying to up front and put\n"
                "the other mentions at the end",
                &settings.reply_mentions_at_end);
    }
    // --- Invisible interface ---
    {
        GtkWidget* p = b.page("_Invisible interface");
        gtk_box_pack_start(
            GTK_BOX(p),
            gtk_label_new("Control FastSMRW from anywhere with system-wide hotkeys\n"
                          "(reads the keyboard directly; requires membership in the input "
                          "group).\nThe keymap file lives in your config folder's keymaps "
                          "directory."),
            FALSE, FALSE, 0);
        b.combo(p, "_Mode:", {{"off", "Off"}, {"hotkey", "System-wide hotkeys"}},
                &settings.invisible_mode);
    }
    // --- Updates ---
    {
        GtkWidget* p = b.page("_Updates");
        b.combo(p, "Update _channel:",
                {{"stable", "Stable releases"}, {"latest", "Latest (rolling)"}},
                &settings.update_branch);
        b.check(p, "_Check for updates automatically at startup",
                &settings.check_updates_on_startup);
    }

    gtk_widget_show_all(dialog);

    std::optional<AppSettings> result;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        for (const auto& apply : b.apply)
            apply();
        result = std::move(settings);
    }
    gtk_widget_destroy(dialog);
    return result;
}

} // namespace fastsmgtk
