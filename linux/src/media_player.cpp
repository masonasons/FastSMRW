#include "media_player.hpp"

namespace fastsmgtk {

namespace {

GtkWidget* g_player = nullptr; // the one player window, if open
// What the open player sends its keys to; emptied when the core closes it, so
// closing then doesn't tell the core to stop what it has already stopped.
std::function<void(const nlohmann::json&)> g_dispatch;

void send(const nlohmann::json& cmd) {
    if (g_dispatch)
        g_dispatch(cmd);
}

std::string caption(const std::string& title) {
    return "Playing: " + (title.empty() ? std::string("Media") : title);
}

gboolean on_key(GtkWidget*, GdkEventKey* event, gpointer) {
    // Connected before the dialog's own handling, so Space reaches the player
    // rather than pressing the focused Close button.
    if (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))
        return FALSE;
    switch (event->keyval) {
    case GDK_KEY_space:
        send({{"cmd", "media_toggle"}});
        return TRUE;
    case GDK_KEY_Left:
    case GDK_KEY_Right:
        send({{"cmd", "media_seek"}, {"by", event->keyval == GDK_KEY_Left ? -5.0 : 5.0}});
        return TRUE;
    case GDK_KEY_Up:
    case GDK_KEY_Down:
        send({{"cmd", "media_volume"}, {"dir", event->keyval == GDK_KEY_Up ? 1 : -1}});
        return TRUE;
    case GDK_KEY_Page_Up: // the same, by a bigger step
    case GDK_KEY_Page_Down:
        send({{"cmd", "media_volume"},
              {"dir", event->keyval == GDK_KEY_Page_Up ? 1 : -1},
              {"coarse", true}});
        return TRUE;
    case GDK_KEY_Home: // straight to full or silent
    case GDK_KEY_End:
        send({{"cmd", "media_volume"}, {"to", event->keyval == GDK_KEY_Home ? 100 : 0}});
        return TRUE;
    case GDK_KEY_p:
    case GDK_KEY_P:
        send({{"cmd", "media_position"}});
        return TRUE;
    default:
        return FALSE; // Escape closes the dialog -> destroy -> media_stop
    }
}

} // namespace

void show_media_player(GtkWindow* parent, const std::string& title,
                       std::function<void(const nlohmann::json&)> dispatch) {
    g_dispatch = std::move(dispatch);
    if (g_player) {
        gtk_window_set_title(GTK_WINDOW(g_player), caption(title).c_str());
        gtk_window_present(GTK_WINDOW(g_player));
        return;
    }
    // Not modal: the main window stays usable while something plays, as on
    // Windows. The caption is what Orca announces.
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        caption(title).c_str(), parent, GTK_DIALOG_DESTROY_WITH_PARENT, "_Close",
        GTK_RESPONSE_CLOSE, nullptr);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(area), 12);
    GtkWidget* label = gtk_label_new(
        "Space pauses, Left and Right seek, Up and Down change the volume, Page Up "
        "and Page Down change it by more, Home and End set it to full or silent, "
        "P says where it is, Escape stops.");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(area), label, TRUE, TRUE, 0);

    g_signal_connect(dialog, "key-press-event", G_CALLBACK(on_key), nullptr);
    // Close, Escape, the window's close box: all end in destroy.
    g_signal_connect(dialog, "response",
                     G_CALLBACK(+[](GtkDialog* d, gint, gpointer) {
                         gtk_widget_destroy(GTK_WIDGET(d));
                     }),
                     nullptr);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget* w, gpointer) {
                         // Closed here: playback stops with it. Closed by the
                         // core (close_media_player) the dispatch is gone first.
                         send({{"cmd", "media_stop"}});
                         if (g_player == w) {
                             g_player = nullptr;
                             g_dispatch = nullptr;
                         }
                     }),
                     nullptr);
    g_player = dialog;
    gtk_widget_show_all(dialog);
    gtk_window_present(GTK_WINDOW(dialog));
}

void retitle_media_player(const std::string& title) {
    if (g_player)
        gtk_window_set_title(GTK_WINDOW(g_player), caption(title).c_str());
}

void close_media_player() {
    GtkWidget* w = g_player;
    if (!w)
        return;
    g_player = nullptr;
    g_dispatch = nullptr; // the core already knows: don't tell it to stop
    gtk_widget_destroy(w);
}

} // namespace fastsmgtk
