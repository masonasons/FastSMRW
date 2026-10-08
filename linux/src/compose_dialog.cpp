#include "compose_dialog.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "fastsm/util/base64.hpp"

using nlohmann::json;

namespace fastsmgtk {

namespace {

// Poll durations, mirroring the Windows composer (default: 1 day).
struct DurationItem {
    const char* label;
    int seconds;
};
constexpr DurationItem kDurations[] = {{"5 minutes", 300},   {"30 minutes", 1800},
                                       {"1 hour", 3600},     {"6 hours", 21600},
                                       {"1 day", 86400},     {"3 days", 259200},
                                       {"7 days", 604800}};

struct Attachment {
    std::string filename, mime, data_base64, alt;
};

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// --------------------------------------------------------- attachments ---
// The Attachments sub-dialog (mirrors IDD_ATTACHMENTS): the staged files,
// Add/Remove, and an alt-text field applied to the selected item with Set.

struct AttCtx {
    std::vector<Attachment>* list = nullptr;
    GtkListStore* store = nullptr;
    GtkTreeView* view = nullptr;
    GtkWidget* alt_entry = nullptr;
};
AttCtx att_ctx;

int att_cursor() {
    GtkTreePath* path = nullptr;
    gtk_tree_view_get_cursor(att_ctx.view, &path, nullptr);
    if (!path)
        return -1;
    const int row = gtk_tree_path_get_indices(path)[0];
    gtk_tree_path_free(path);
    return row;
}

void att_refill(int cursor) {
    gtk_list_store_clear(att_ctx.store);
    for (const auto& a : *att_ctx.list) {
        GtkTreeIter iter;
        gtk_list_store_append(att_ctx.store, &iter);
        const std::string label =
            a.alt.empty() ? a.filename : a.filename + " \xE2\x80\x94 " + a.alt;
        gtk_list_store_set(att_ctx.store, &iter, 0, label.c_str(), -1);
    }
    if (cursor >= 0 && cursor < static_cast<int>(att_ctx.list->size())) {
        GtkTreePath* path = gtk_tree_path_new_from_indices(cursor, -1);
        gtk_tree_view_set_cursor(att_ctx.view, path, nullptr, FALSE);
        gtk_tree_path_free(path);
    }
}

void att_load_alt() {
    const int row = att_cursor();
    const std::string alt = (row >= 0 && row < static_cast<int>(att_ctx.list->size()))
                                ? (*att_ctx.list)[static_cast<size_t>(row)].alt
                                : std::string{};
    gtk_entry_set_text(GTK_ENTRY(att_ctx.alt_entry), alt.c_str());
}

bool run_attachments_dialog(GtkWindow* parent, std::vector<Attachment>& attachments) {
    std::vector<Attachment> work = attachments; // Cancel discards edits

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Attachments", parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "A_dd…",
        100, "_Remove", 101, "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 440, 380);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Attachments:"), FALSE, FALSE, 0);

    GtkListStore* store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_append_column(
        GTK_TREE_VIEW(view), gtk_tree_view_column_new_with_attributes(
                                 "", gtk_cell_renderer_text_new(), "text", 0, nullptr));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
    atk_object_set_name(gtk_widget_get_accessible(view), "Attachments");
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    GtkWidget* alt_label = gtk_label_new_with_mnemonic("Alt te_xt (image description):");
    gtk_label_set_xalign(GTK_LABEL(alt_label), 0.0f);
    GtkWidget* alt_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* alt_entry = gtk_entry_new();
    gtk_label_set_mnemonic_widget(GTK_LABEL(alt_label), alt_entry);
    GtkWidget* set_btn = gtk_button_new_with_mnemonic("_Set");
    gtk_box_pack_start(GTK_BOX(alt_row), alt_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(alt_row), set_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), alt_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), alt_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, TRUE, TRUE,
                       0);

    att_ctx = {&work, store, GTK_TREE_VIEW(view), alt_entry};
    g_signal_connect(view, "cursor-changed",
                     G_CALLBACK(+[](GtkTreeView*, gpointer) { att_load_alt(); }), nullptr);
    g_signal_connect(set_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
                         const int row = att_cursor();
                         if (row >= 0 && row < static_cast<int>(att_ctx.list->size())) {
                             (*att_ctx.list)[static_cast<size_t>(row)].alt =
                                 gtk_entry_get_text(GTK_ENTRY(att_ctx.alt_entry));
                             att_refill(row);
                         }
                     }),
                     nullptr);

    att_refill(0);
    gtk_widget_show_all(dialog);
    att_load_alt();
    gtk_widget_grab_focus(view);

    int response;
    while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) == 100 || response == 101) {
        if (response == 100) { // Add…
            GtkWidget* chooser = gtk_file_chooser_dialog_new(
                "Add Media", GTK_WINDOW(dialog), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel",
                GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_ACCEPT, nullptr);
            if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
                gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
                gchar* contents = nullptr;
                gsize length = 0;
                if (path && g_file_get_contents(path, &contents, &length, nullptr)) {
                    Attachment a;
                    gchar* base = g_path_get_basename(path);
                    a.filename = base;
                    g_free(base);
                    gboolean uncertain = FALSE;
                    gchar* ctype = g_content_type_guess(path, nullptr, 0, &uncertain);
                    gchar* mime = ctype ? g_content_type_get_mime_type(ctype) : nullptr;
                    a.mime = mime ? mime : "application/octet-stream";
                    g_free(ctype);
                    g_free(mime);
                    a.data_base64 =
                        fastsm::util::base64_encode(std::string_view(contents, length));
                    g_free(contents);
                    work.push_back(std::move(a));
                    att_refill(static_cast<int>(work.size()) - 1);
                }
                g_free(path);
            }
            gtk_widget_destroy(chooser);
        } else { // Remove
            const int row = att_cursor();
            if (row >= 0 && row < static_cast<int>(work.size())) {
                work.erase(work.begin() + row);
                att_refill(row >= static_cast<int>(work.size())
                               ? static_cast<int>(work.size()) - 1
                               : row);
                att_load_alt();
            }
        }
        gtk_widget_grab_focus(view);
    }
    const bool ok = response == GTK_RESPONSE_OK;
    if (ok)
        attachments = std::move(work);
    gtk_widget_destroy(dialog);
    att_ctx = AttCtx{};
    return ok;
}

// ------------------------------------------------------------- composer ---

// Title-bar character counter, like Windows: "Reply (487)".
struct CounterCtx {
    GtkWidget* dialog = nullptr;
    GtkTextBuffer* buffer = nullptr;
    std::string base_title;
    int max_chars = 500;
};
CounterCtx counter_ctx;

void update_counter() {
    if (!counter_ctx.dialog)
        return;
    const int len = gtk_text_buffer_get_char_count(counter_ctx.buffer);
    const std::string title = counter_ctx.base_title + " (" +
                              std::to_string(counter_ctx.max_chars - len) + ")";
    gtk_window_set_title(GTK_WINDOW(counter_ctx.dialog), title.c_str());
}

// Ctrl+Enter always sends; plain Enter sends too when the "Enter to send"
// setting is on (matching the Windows composer).
struct SendKeyCtx {
    GtkDialog* dialog;
    bool enter_to_send;
};

gboolean on_text_key(GtkWidget*, GdkEventKey* event, gpointer user) {
    auto* ctx = static_cast<SendKeyCtx*>(user);
    if (event->keyval != GDK_KEY_Return && event->keyval != GDK_KEY_KP_Enter)
        return FALSE;
    const guint state = event->state & gtk_accelerator_get_default_mod_mask();
    if ((state & GDK_CONTROL_MASK) || (state == 0 && ctx->enter_to_send)) {
        gtk_dialog_response(ctx->dialog, GTK_RESPONSE_OK);
        return TRUE;
    }
    return FALSE;
}

} // namespace

std::optional<json> show_compose_dialog(GtkWindow* parent, const json& ctx,
                                        const MentionPicker& pick_mention) {
    const std::string mode = ctx.value("mode", std::string("new"));
    const bool editing = mode == "edit";
    const json features = ctx.value("features", json::object());
    const bool has_cw = features.value("content_warning", false);
    const bool has_visibility = features.value("visibility", false) && !editing;
    const bool has_polls = features.value("polls", false) && !editing;
    const bool has_schedule = features.value("scheduling", false) && !editing;
    const bool has_media = features.value("media", false) && !editing;
    const std::string base_title = ctx.value("title", std::string("New Post"));

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        base_title.c_str(), parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancel",
        GTK_RESPONSE_CANCEL, editing ? "_Save" : "_Post", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 620, 560);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    // Context ("Replying to X: …"), like the label at the top of the Windows
    // dialog.
    const std::string context_label = ctx.value("context_label", std::string{});
    if (!context_label.empty()) {
        GtkWidget* label = gtk_label_new(context_label.c_str());
        gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 80);
        gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    }

    // Content warning — before the post text, matching the Windows tab order.
    GtkWidget* cw_entry = nullptr;
    if (has_cw) {
        GtkWidget* cw_label = gtk_label_new_with_mnemonic("Content _warning:");
        gtk_label_set_xalign(GTK_LABEL(cw_label), 0.0f);
        cw_entry = gtk_entry_new();
        gtk_entry_set_activates_default(GTK_ENTRY(cw_entry), TRUE);
        gtk_label_set_mnemonic_widget(GTK_LABEL(cw_label), cw_entry);
        const std::string cw = ctx.value("prefill_cw", std::string{});
        if (!cw.empty())
            gtk_entry_set_text(GTK_ENTRY(cw_entry), cw.c_str());
        gtk_box_pack_start(GTK_BOX(box), cw_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), cw_entry, FALSE, FALSE, 0);
    }

    // Reply recipients: checked by default; only the checked get mentioned.
    std::vector<std::pair<std::string, GtkWidget*>> recipient_checks;
    const json participants = ctx.value("reply_participants", json::array());
    if (!participants.empty()) {
        GtkWidget* frame = gtk_frame_new("Recipients");
        GtkWidget* rbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(frame), rbox);
        for (const auto& p : participants) {
            const std::string acct = p.value("acct", std::string{});
            const std::string display = p.value("display_name", acct);
            GtkWidget* check = gtk_check_button_new_with_label(display.c_str());
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), p.value("checked", true));
            gtk_box_pack_start(GTK_BOX(rbox), check, FALSE, FALSE, 0);
            recipient_checks.emplace_back(acct, check);
        }
        gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);
    }

    // The post body.
    GtkWidget* text_label = gtk_label_new_with_mnemonic("_Post:");
    gtk_label_set_xalign(GTK_LABEL(text_label), 0.0f);
    GtkWidget* text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(text_label), text_view);
    atk_object_set_name(gtk_widget_get_accessible(text_view), "Post text");
    GtkWidget* text_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(text_scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(text_scroll), text_view);
    gtk_widget_set_vexpand(text_scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(box), text_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), text_scroll, TRUE, TRUE, 0);

    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    const std::string prefill = ctx.value("prefill_text", std::string{});
    if (!prefill.empty()) {
        gtk_text_buffer_set_text(buffer, prefill.c_str(), -1);
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_place_cursor(buffer, &end); // caret at the end, like Windows
    }

    SendKeyCtx send_ctx{GTK_DIALOG(dialog), ctx.value("enter_to_send", false)};
    g_signal_connect(text_view, "key-press-event", G_CALLBACK(on_text_key), &send_ctx);

    // Live character counter in the title bar.
    counter_ctx = {dialog, buffer, base_title, ctx.value("max_chars", 500)};
    g_signal_connect(buffer, "changed",
                     G_CALLBACK(+[](GtkTextBuffer*, gpointer) { update_counter(); }), nullptr);

    // Visibility.
    GtkWidget* vis_combo = nullptr;
    if (has_visibility) {
        GtkWidget* vis_label = gtk_label_new_with_mnemonic("_Visibility:");
        gtk_label_set_xalign(GTK_LABEL(vis_label), 0.0f);
        vis_combo = gtk_combo_box_text_new();
        for (const char* name : {"Public", "Quiet public", "Followers", "Specific people"})
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(vis_combo), name);
        gtk_combo_box_set_active(GTK_COMBO_BOX(vis_combo), ctx.value("default_visibility", 0));
        gtk_label_set_mnemonic_widget(GTK_LABEL(vis_label), vis_combo);
        gtk_box_pack_start(GTK_BOX(box), vis_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), vis_combo, FALSE, FALSE, 0);
    }

    // Language (always shown, like Windows; the list rides the event).
    std::vector<std::string> lang_codes;
    GtkWidget* lang_combo = gtk_combo_box_text_new();
    for (const auto& l : ctx.value("languages", json::array())) {
        lang_codes.push_back(l.value("code", std::string{}));
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(lang_combo),
                                       l.value("name", std::string{}).c_str());
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(lang_combo), 0);
    {
        GtkWidget* lang_label = gtk_label_new_with_mnemonic("_Language:");
        gtk_label_set_xalign(GTK_LABEL(lang_label), 0.0f);
        gtk_label_set_mnemonic_widget(GTK_LABEL(lang_label), lang_combo);
        gtk_box_pack_start(GTK_BOX(box), lang_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), lang_combo, FALSE, FALSE, 0);
    }

    // Poll: the option fields appear when "Add poll" is checked.
    GtkWidget* poll_check = nullptr;
    GtkWidget* poll_box = nullptr;
    std::vector<GtkWidget*> poll_options;
    GtkWidget* poll_multi = nullptr;
    GtkWidget* poll_duration = nullptr;
    if (has_polls) {
        poll_check = gtk_check_button_new_with_mnemonic("Add p_oll");
        gtk_box_pack_start(GTK_BOX(box), poll_check, FALSE, FALSE, 0);
        poll_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        for (int i = 1; i <= 4; ++i) {
            GtkWidget* opt = gtk_entry_new();
            atk_object_set_name(gtk_widget_get_accessible(opt),
                                ("Poll option " + std::to_string(i)).c_str());
            gtk_box_pack_start(GTK_BOX(poll_box), opt, FALSE, FALSE, 0);
            poll_options.push_back(opt);
        }
        poll_multi = gtk_check_button_new_with_mnemonic("Allow _multiple choices");
        gtk_box_pack_start(GTK_BOX(poll_box), poll_multi, FALSE, FALSE, 0);
        GtkWidget* dur_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget* dur_label = gtk_label_new_with_mnemonic("_Duration:");
        poll_duration = gtk_combo_box_text_new();
        for (const auto& d : kDurations)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(poll_duration), d.label);
        gtk_combo_box_set_active(GTK_COMBO_BOX(poll_duration), 4); // 1 day
        gtk_label_set_mnemonic_widget(GTK_LABEL(dur_label), poll_duration);
        gtk_box_pack_start(GTK_BOX(dur_row), dur_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(dur_row), poll_duration, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(poll_box), dur_row, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), poll_box, FALSE, FALSE, 0);
        g_signal_connect(poll_check, "toggled", G_CALLBACK(+[](GtkToggleButton* b, gpointer p) {
                             gtk_widget_set_visible(static_cast<GtkWidget*>(p),
                                                    gtk_toggle_button_get_active(b));
                         }),
                         poll_box);
    }

    // Schedule: a local "YYYY-MM-DD HH:MM" time, shown when checked.
    GtkWidget* sched_check = nullptr;
    GtkWidget* sched_entry = nullptr;
    if (has_schedule) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        sched_check = gtk_check_button_new_with_mnemonic("_Schedule");
        sched_entry = gtk_entry_new();
        atk_object_set_name(gtk_widget_get_accessible(sched_entry),
                            "Scheduled time, year dash month dash day space hour colon minute");
        GDateTime* now = g_date_time_new_now_local();
        GDateTime* in_an_hour = g_date_time_add_hours(now, 1);
        gchar* seed = g_date_time_format(in_an_hour, "%Y-%m-%d %H:%M");
        gtk_entry_set_text(GTK_ENTRY(sched_entry), seed);
        g_free(seed);
        g_date_time_unref(in_an_hour);
        g_date_time_unref(now);
        gtk_box_pack_start(GTK_BOX(row), sched_check, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), sched_entry, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
        g_signal_connect(sched_check, "toggled", G_CALLBACK(+[](GtkToggleButton* b, gpointer p) {
                             gtk_widget_set_visible(static_cast<GtkWidget*>(p),
                                                    gtk_toggle_button_get_active(b));
                         }),
                         sched_entry);
    }

    // Autocomplete @mention (Alt+A via the mnemonic) — hidden without a picker.
    GtkWidget* mention_btn = nullptr;
    struct MentionBtnCtx {
        const MentionPicker* picker;
        GtkTextBuffer* buffer;
        GtkWidget* text_view;
    };
    static MentionBtnCtx mention_ctx;
    if (pick_mention) {
        mention_btn = gtk_button_new_with_mnemonic("_Autocomplete @mention (Alt+A)");
        mention_ctx = {&pick_mention, buffer, text_view};
        g_signal_connect(
            mention_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
                // The partial handle under the caret: scan back to whitespace/@.
                GtkTextIter caret;
                gtk_text_buffer_get_iter_at_mark(mention_ctx.buffer, &caret,
                                                 gtk_text_buffer_get_insert(mention_ctx.buffer));
                GtkTextIter start = caret;
                while (!gtk_text_iter_is_start(&start)) {
                    GtkTextIter prev = start;
                    gtk_text_iter_backward_char(&prev);
                    const gunichar ch = gtk_text_iter_get_char(&prev);
                    if (g_unichar_isspace(ch))
                        break;
                    start = prev;
                    if (ch == '@')
                        break;
                }
                // Forward to the end of the handle too, the way FastSM does.
                // Completing with the caret inside a half-typed handle used to
                // replace only the part before it and leave the rest behind
                // ("@ali|ce" became "@alice ce").
                GtkTextIter end = caret;
                while (!gtk_text_iter_is_end(&end)) {
                    if (g_unichar_isspace(gtk_text_iter_get_char(&end)))
                        break;
                    gtk_text_iter_forward_char(&end);
                }
                gchar* raw = gtk_text_buffer_get_text(mention_ctx.buffer, &start, &end, FALSE);
                std::string partial = raw ? raw : "";
                g_free(raw);
                if (!partial.empty() && partial[0] == '@')
                    partial.erase(0, 1);
                auto handle = (*mention_ctx.picker)(partial);
                if (handle && !handle->empty()) {
                    // A space only where there isn't one already, so completing
                    // mid-sentence doesn't leave a double space.
                    const bool space_follows =
                        !gtk_text_iter_is_end(&end) &&
                        g_unichar_isspace(gtk_text_iter_get_char(&end));
                    gtk_text_buffer_delete(mention_ctx.buffer, &start, &end);
                    const std::string insert =
                        "@" + *handle + (space_follows ? "" : " ");
                    gtk_text_buffer_insert(mention_ctx.buffer, &start, insert.c_str(), -1);
                }
                gtk_widget_grab_focus(mention_ctx.text_view);
            }),
            nullptr);
        gtk_box_pack_start(GTK_BOX(box), mention_btn, FALSE, FALSE, 0);
    }

    // Media… opens the Attachments sub-dialog; the label tracks the count.
    static std::vector<Attachment> attachments;
    attachments.clear();
    GtkWidget* media_status = nullptr;
    if (has_media) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget* media_btn = gtk_button_new_with_mnemonic("_Media…");
        media_status = gtk_label_new("No attachments");
        gtk_label_set_xalign(GTK_LABEL(media_status), 0.0f);
        struct MediaBtnCtx {
            GtkWidget* dialog;
            GtkWidget* status;
        };
        static MediaBtnCtx media_ctx;
        media_ctx = {dialog, media_status};
        g_signal_connect(media_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
                             if (run_attachments_dialog(GTK_WINDOW(media_ctx.dialog),
                                                        attachments)) {
                                 const std::string text =
                                     attachments.empty()
                                         ? "No attachments"
                                         : std::to_string(attachments.size()) +
                                               " attachment(s)";
                                 gtk_label_set_text(GTK_LABEL(media_ctx.status), text.c_str());
                             }
                         }),
                         nullptr);
        gtk_box_pack_start(GTK_BOX(row), media_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), media_status, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    }

    GtkWidget* outer_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(outer_scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(outer_scroll), box);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), outer_scroll,
                       TRUE, TRUE, 0);

    gtk_widget_show_all(dialog);
    if (poll_box)
        gtk_widget_set_visible(poll_box, FALSE);
    if (sched_entry)
        gtk_widget_set_visible(sched_entry, FALSE);
    update_counter();
    gtk_widget_grab_focus(text_view);

    std::optional<json> result;
    int response;
    while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) == GTK_RESPONSE_OK) {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar* raw = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        const std::string body = trimmed(raw ? raw : "");
        g_free(raw);

        const bool poll_active =
            poll_check && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(poll_check));
        std::vector<std::string> options;
        if (poll_active)
            for (GtkWidget* opt : poll_options) {
                const std::string o = trimmed(gtk_entry_get_text(GTK_ENTRY(opt)));
                if (!o.empty())
                    options.push_back(o);
            }
        // Same refusals as Windows: no empty post (unless media), no
        // one-option poll. Stay open so the user can fix it.
        if ((body.empty() && attachments.empty()) ||
            (poll_active && options.size() < 2)) {
            gtk_widget_error_bell(dialog);
            continue;
        }

        gint64 scheduled_at = 0;
        if (sched_check && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sched_check))) {
            int y = 0, mo = 0, d = 0, h = 0, mi = 0;
            if (std::sscanf(gtk_entry_get_text(GTK_ENTRY(sched_entry)), "%d-%d-%d %d:%d", &y,
                            &mo, &d, &h, &mi) == 5) {
                if (GDateTime* when = g_date_time_new_local(y, mo, d, h, mi, 0)) {
                    scheduled_at = g_date_time_to_unix(when);
                    g_date_time_unref(when);
                }
            }
            if (scheduled_at == 0) {
                gtk_widget_error_bell(dialog); // unparsable time: fix or uncheck
                continue;
            }
        }

        json draft;
        draft["text"] = body;
        if (cw_entry) {
            const std::string cw = trimmed(gtk_entry_get_text(GTK_ENTRY(cw_entry)));
            if (!cw.empty())
                draft["spoiler_text"] = cw;
        }
        if (vis_combo) {
            const int sel = gtk_combo_box_get_active(GTK_COMBO_BOX(vis_combo));
            draft["visibility"] = sel < 0 ? 0 : sel;
        }
        {
            const int sel = gtk_combo_box_get_active(GTK_COMBO_BOX(lang_combo));
            if (sel >= 0 && sel < static_cast<int>(lang_codes.size()))
                draft["language"] = lang_codes[static_cast<size_t>(sel)];
        }
        if (poll_active) {
            json poll;
            poll["options"] = options;
            poll["multiple"] =
                static_cast<bool>(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(poll_multi)));
            const int di = gtk_combo_box_get_active(GTK_COMBO_BOX(poll_duration));
            poll["expires_in_seconds"] =
                (di >= 0 && di < static_cast<int>(G_N_ELEMENTS(kDurations)))
                    ? kDurations[di].seconds
                    : 86400;
            draft["poll"] = std::move(poll);
        }
        if (scheduled_at > 0)
            draft["scheduled_at"] = scheduled_at;
        if (!attachments.empty()) {
            json atts = json::array();
            for (const auto& a : attachments)
                atts.push_back({{"filename", a.filename},
                                {"mime", a.mime},
                                {"data", a.data_base64},
                                {"alt", a.alt}});
            draft["attachments"] = std::move(atts);
        }
        json mentions = json::array();
        for (const auto& [acct, check] : recipient_checks)
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)))
                mentions.push_back(acct);
        if (!mentions.empty())
            draft["mentions"] = std::move(mentions);
        for (const char* key :
             {"reply_to_id", "reply_to_url", "quoted_status_id", "quoted_status_cid",
              "quoted_status_url"})
            if (ctx.contains(key))
                draft[key] = ctx[key];

        json cmd = {{"cmd", "post"}, {"draft", std::move(draft)}};
        if (ctx.contains("edit_id"))
            cmd["edit_id"] = ctx["edit_id"];
        result = std::move(cmd);
        break;
    }
    gtk_widget_destroy(dialog);
    counter_ctx = CounterCtx{};
    attachments.clear();
    return result;
}

} // namespace fastsmgtk
