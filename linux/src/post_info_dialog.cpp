#include "post_info_dialog.hpp"

using nlohmann::json;

namespace fastsmgtk {

namespace {

// Response ids for the action buttons (GTK reserves the small negatives).
constexpr int kActionBase = 100;

} // namespace

std::optional<PostInfoResult> show_post_info_dialog(GtkWindow* parent, const json& e) {
    const bool quote_ok = e.contains("features") && e["features"].value("quote_posts", false);
    const bool browser_ok = e.value("has_url", false);
    const bool is_mine = e.value("is_mine", false);
    const bool mute_ok = e.contains("features") && e["features"].value("mute_conversations", false);
    const bool muted = e.value("muted", false);
    const int fav_count = e.value("favorites_count", 0);
    const int boost_count = e.value("boosts_count", 0);
    const json poll = e.value("poll", json::object());
    const bool has_poll = poll.is_object() && poll.contains("options");

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Post Info", parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Close",
        GTK_RESPONSE_CANCEL, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 520);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, TRUE, TRUE,
                       0);

    // The post's text and its details in two labelled read-only fields, both
    // focusable so a screen reader can review them line by line. Separate, so
    // selecting all in the first copies just the post (issue 37).
    auto add_text_field = [&](const char* mnemonic_label, const char* name,
                              const std::string& text) {
        GtkWidget* label = gtk_label_new_with_mnemonic(mnemonic_label);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        GtkWidget* view = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
        gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(view), FALSE);
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), view);
        atk_object_set_name(gtk_widget_get_accessible(view), name);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), text.c_str(), -1);
        GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
        gtk_container_add(GTK_CONTAINER(scroll), view);
        gtk_widget_set_vexpand(scroll, TRUE);
        gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
        return view;
    };
    const std::string body = e.value("body", std::string{});
    GtkWidget* body_view = add_text_field("Po_st text:", "Post text", body);
    GtkWidget* details_view =
        add_text_field("Post d_etails:", "Post details", e.value("details", std::string{}));

    // Votable poll: a checklist (multi-choice) or radio group (single).
    std::vector<GtkWidget*> poll_toggles;
    if (has_poll) {
        GtkWidget* frame = gtk_frame_new("Poll options");
        GtkWidget* pbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(frame), pbox);
        const bool multiple = poll.value("multiple", false);
        GtkWidget* radio_group_head = nullptr;
        for (const auto& o : poll.value("options", json::array())) {
            const std::string title = o.get<std::string>();
            GtkWidget* toggle;
            if (multiple) {
                toggle = gtk_check_button_new_with_label(title.c_str());
            } else if (!radio_group_head) {
                toggle = radio_group_head = gtk_radio_button_new_with_label(nullptr, title.c_str());
            } else {
                toggle = gtk_radio_button_new_with_label_from_widget(
                    GTK_RADIO_BUTTON(radio_group_head), title.c_str());
            }
            gtk_box_pack_start(GTK_BOX(pbox), toggle, FALSE, FALSE, 0);
            poll_toggles.push_back(toggle);
        }
        gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);
    }

    // The action set, mirroring the Windows/Mac dialog.
    std::vector<std::string> actions;
    auto add_action = [&](const std::string& action, const std::string& label) {
        GtkWidget* button = gtk_button_new_with_mnemonic(label.c_str());
        const int response = kActionBase + static_cast<int>(actions.size());
        g_object_set_data(G_OBJECT(button), "fastsm-response", GINT_TO_POINTER(response));
        g_signal_connect(button, "clicked",
                         G_CALLBACK(+[](GtkButton* b, gpointer d) {
                             gtk_dialog_response(GTK_DIALOG(d),
                                                 GPOINTER_TO_INT(g_object_get_data(
                                                     G_OBJECT(b), "fastsm-response")));
                         }),
                         dialog);
        actions.push_back(action);
        return button;
    };

    // A plain grid, NOT GtkFlowBox: the flow box wraps each button in its own
    // focusable child accessible, so screen readers announce every button twice.
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    int packed = 0;
    auto pack = [&](GtkWidget* b) {
        gtk_grid_attach(GTK_GRID(grid), b, packed % 4, packed / 4, 1, 1);
        ++packed;
    };

    if (has_poll)
        pack(add_action("vote", "_Vote"));
    pack(add_action("reply", "_Reply"));
    pack(add_action("boost", "_Boost"));
    pack(add_action("favorite", "_Favorite"));
    if (quote_ok)
        pack(add_action("quote", "_Quote"));
    pack(add_action("thread", "View _Thread"));
    pack(add_action("author", "View _Author"));
    pack(add_action("links", "Open _Links"));
    if (browser_ok)
        pack(add_action("browser", "Open in Bro_wser"));
    pack(add_action("favorited_by", "Favorited By (" + std::to_string(fav_count) + ")"));
    pack(add_action("boosted_by", "Boosted By (" + std::to_string(boost_count) + ")"));
    if (mute_ok)
        pack(add_action("mute_conv", muted ? "Un_mute Conversation" : "_Mute Conversation"));
    if (is_mine)
        pack(add_action("delete", "_Delete"));
    pack(add_action("report", "Repo_rt…"));
    gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);
    // Read the post first; a post with no text (media only) starts on the details.
    gtk_widget_grab_focus(body.empty() ? details_view : body_view);

    std::optional<PostInfoResult> result;
    const int response = gtk_dialog_run(GTK_DIALOG(dialog));
    const int index = response - kActionBase;
    if (index >= 0 && index < static_cast<int>(actions.size())) {
        PostInfoResult r;
        r.action = actions[static_cast<size_t>(index)];
        if (r.action == "vote")
            for (size_t i = 0; i < poll_toggles.size(); ++i)
                if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(poll_toggles[i])))
                    r.choices.push_back(static_cast<int>(i));
        if (r.action != "vote" || !r.choices.empty())
            result = std::move(r);
    }
    gtk_widget_destroy(dialog);
    return result;
}

} // namespace fastsmgtk
