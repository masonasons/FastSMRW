#include "media_player.hpp"

#include <algorithm>

#include <gst/gst.h>

#include "fastsm/media/media_player.hpp" // kVolumeStep / kVolumeStepCoarse

namespace fastsmgtk {

namespace {

// GStreamer has to be up before anything (including enumerating devices) works.
void ensure_gst_init() {
    static bool inited = false;
    if (!inited) {
        gst_init(nullptr, nullptr);
        inited = true;
    }
}

// Hands each audio output GStreamer can see to `visit`, along with its display
// name; returning true stops the walk. The monitor is torn down either way.
template <class Visit> void enum_audio_sinks(Visit visit) {
    ensure_gst_init();
    GstDeviceMonitor* monitor = gst_device_monitor_new();
    if (!monitor)
        return;
    gst_device_monitor_add_filter(monitor, "Audio/Sink", nullptr);
    gst_device_monitor_start(monitor);
    GList* devices = gst_device_monitor_get_devices(monitor);
    for (GList* l = devices; l; l = l->next) {
        GstDevice* device = GST_DEVICE(l->data);
        gchar* name = gst_device_get_display_name(device);
        const bool stop = name && *name && visit(std::string(name), device);
        g_free(name);
        if (stop)
            break;
    }
    g_list_free_full(devices, gst_object_unref);
    gst_device_monitor_stop(monitor);
    gst_object_unref(monitor);
}

} // namespace

std::vector<std::string> media_output_devices() {
    std::vector<std::string> names;
    enum_audio_sinks([&](const std::string& name, GstDevice*) {
        // Two devices can share a display name (the same card through different
        // profiles); one entry per name is all the user can pick from.
        if (std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(name);
        return false; // keep walking
    });
    return names;
}

struct MediaPlayback::Impl {
    GstElement* playbin = nullptr;
    bool done = false;
    int volume = 100;
    std::string device; // "" = the system's own output device

    // Point playbin at the chosen device by giving it that device's own sink
    // element. No match (unplugged, renamed) leaves playbin to its default sink
    // rather than failing to play.
    void apply_device() {
        if (device.empty() || !playbin)
            return;
        enum_audio_sinks([&](const std::string& name, GstDevice* dev) {
            if (name != device)
                return false;
            if (GstElement* sink = gst_device_create_element(dev, nullptr))
                g_object_set(playbin, "audio-sink", sink, nullptr); // playbin takes it
            return true;
        });
    }

    // Drain the bus; EOS or an error (bad codec, dead stream) marks us done.
    void poll_bus() {
        if (!playbin)
            return;
        GstBus* bus = gst_element_get_bus(playbin);
        while (GstMessage* msg = gst_bus_pop_filtered(
                   bus, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR))) {
            done = true;
            gst_message_unref(msg);
        }
        gst_object_unref(bus);
    }
};

MediaPlayback::MediaPlayback() : impl_(new Impl) { ensure_gst_init(); }

MediaPlayback::~MediaPlayback() {
    stop();
    delete impl_;
}

bool MediaPlayback::play(const std::string& url) {
    stop();
    impl_->done = false;
    impl_->playbin = gst_element_factory_make("playbin", nullptr);
    if (!impl_->playbin)
        return false;
    g_object_set(impl_->playbin, "uri", url.c_str(), nullptr);
    impl_->apply_device(); // before PLAYING, while the sink can still be swapped
    g_object_set(impl_->playbin, "volume", impl_->volume / 100.0, nullptr);
    if (gst_element_set_state(impl_->playbin, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        stop();
        return false;
    }
    return true;
}

void MediaPlayback::stop() {
    if (!impl_->playbin)
        return;
    gst_element_set_state(impl_->playbin, GST_STATE_NULL);
    gst_object_unref(impl_->playbin);
    impl_->playbin = nullptr;
}

bool MediaPlayback::active() const { return impl_->playbin != nullptr && !impl_->done; }

void MediaPlayback::seek(double delta_seconds) {
    if (!impl_->playbin)
        return;
    gint64 pos = 0;
    if (!gst_element_query_position(impl_->playbin, GST_FORMAT_TIME, &pos))
        return;
    gint64 target = pos + static_cast<gint64>(delta_seconds * GST_SECOND);
    if (target < 0)
        target = 0;
    gst_element_seek_simple(impl_->playbin, GST_FORMAT_TIME,
                            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                                      GST_SEEK_FLAG_KEY_UNIT),
                            target);
}

void MediaPlayback::set_volume(int pct) {
    impl_->volume = std::clamp(pct, 0, 100);
    if (impl_->playbin) // playbin's volume is a straight amplitude ratio
        g_object_set(impl_->playbin, "volume", impl_->volume / 100.0, nullptr);
}

int MediaPlayback::adjust_volume(int delta_pct) {
    set_volume(impl_->volume + delta_pct);
    return impl_->volume;
}

void MediaPlayback::set_output_device(std::string name) { impl_->device = std::move(name); }

bool MediaPlayback::toggle_pause() {
    if (!impl_->playbin)
        return false;
    GstState state = GST_STATE_NULL;
    gst_element_get_state(impl_->playbin, &state, nullptr, 0);
    const bool now_playing = state != GST_STATE_PLAYING;
    gst_element_set_state(impl_->playbin,
                          now_playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
    return now_playing;
}

bool MediaPlayback::completed() {
    impl_->poll_bus();
    return impl_->done;
}

int MediaPlayback::volume_pct() const { return impl_->volume; }

namespace {

struct PlayerCtx {
    MediaPlayback* player = nullptr;
    std::function<void(const std::string&)>* speak = nullptr;
    GtkWidget* dialog = nullptr;
    std::function<void(int)>* on_volume = nullptr; // remember the level chosen
};
PlayerCtx player_ctx;

} // namespace

bool show_media_player(GtkWindow* parent, const std::string& title, const std::string& url,
                       std::function<void(const std::string&)> speak,
                       MediaPlayerOptions options) {
    MediaPlayback player;
    player.set_output_device(std::move(options.device));
    player.set_volume(options.volume);
    if (!player.play(url))
        return false;

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        title.empty() ? "Media Player" : title.c_str(), parent,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Close",
        GTK_RESPONSE_CANCEL, nullptr);
    GtkWidget* label = gtk_label_new(
        "Playing. Space pauses, Left and Right seek, Up and Down change the volume, "
        "Page Up and Page Down change it by more, Home and End set it to full or "
        "silent, Escape closes.");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_container_set_border_width(
        GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), label, TRUE,
                       TRUE, 0);

    player_ctx = {&player, &speak, dialog, &options.on_volume};
    g_signal_connect(
        dialog, "key-press-event",
        G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer) -> gboolean {
            switch (event->keyval) {
            case GDK_KEY_space:
                (*player_ctx.speak)(player_ctx.player->toggle_pause() ? "Playing" : "Paused");
                return TRUE;
            case GDK_KEY_Left:
                player_ctx.player->seek(-5);
                return TRUE;
            case GDK_KEY_Right:
                player_ctx.player->seek(5);
                return TRUE;
            case GDK_KEY_Up:
            case GDK_KEY_Down:
            case GDK_KEY_Page_Up:
            case GDK_KEY_Page_Down:
            case GDK_KEY_Home:
            case GDK_KEY_End: {
                // This player is Linux's own (the core's needs FastPlay's engine),
                // so it moves the level itself -- but by the core's steps, so the
                // shared saved level moves the same way whichever app you set it
                // from. Page Up/Down take the bigger step; Home and End go to the
                // ends of the range.
                int level = 0;
                if (event->keyval == GDK_KEY_Home) {
                    player_ctx.player->set_volume(100);
                    level = player_ctx.player->volume_pct();
                } else if (event->keyval == GDK_KEY_End) {
                    player_ctx.player->set_volume(0);
                    level = player_ctx.player->volume_pct();
                } else {
                    const bool up =
                        event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Page_Up;
                    const bool coarse = event->keyval == GDK_KEY_Page_Up ||
                                        event->keyval == GDK_KEY_Page_Down;
                    const int step =
                        coarse ? fastsm::media::kVolumeStepCoarse : fastsm::media::kVolumeStep;
                    level = player_ctx.player->adjust_volume(up ? step : -step);
                }
                if (player_ctx.on_volume && *player_ctx.on_volume)
                    (*player_ctx.on_volume)(level); // persist for the next thing you play
                (*player_ctx.speak)("Volume " + std::to_string(level) + " percent");
                return TRUE;
            }
            default:
                return FALSE;
            }
        }),
        nullptr);
    // Close on our own when the stream finishes.
    const guint timer = g_timeout_add(1000, +[](gpointer) -> gboolean {
        if (player_ctx.player && player_ctx.player->completed())
            gtk_dialog_response(GTK_DIALOG(player_ctx.dialog), GTK_RESPONSE_CANCEL);
        return G_SOURCE_CONTINUE; // removed once, after the dialog closes
    }, nullptr);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    g_source_remove(timer);
    player.stop();
    gtk_widget_destroy(dialog);
    player_ctx = PlayerCtx{};
    return true;
}

} // namespace fastsmgtk
