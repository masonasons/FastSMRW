#include "fastsm/media/media_player.hpp"

#include <algorithm>
#include <mutex>

#ifdef FASTSM_FASTPLAY_ENGINE
#include <fastplay_engine/fastplay_engine.h>
#endif

namespace fastsm::media {

#ifdef FASTSM_FASTPLAY_ENGINE

namespace {

// The engine is set up once for the process and left up: it costs nothing
// until a player opens something.
std::mutex g_engine_mutex;
bool g_engine_ready = false;

bool ensure_engine(const std::filesystem::path& data_dir) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine_ready)
        return true;
    std::error_code ec;
    std::filesystem::create_directories(data_dir, ec);
    const auto u8 = data_dir.u8string();
    const std::string dir(reinterpret_cast<const char*>(u8.c_str()), u8.size());
    fpe_config config{};
    config.api_version = FPE_API_VERSION;
    config.data_dir = dir.c_str();
    // iOS: the media player is for listening, ring switch off and screen locked
    config.ios_session = FPE_IOS_SESSION_PLAYBACK;
    g_engine_ready = fpe_init(&config) != 0;
    return g_engine_ready;
}

} // namespace

struct MediaPlayer::Impl {
    std::filesystem::path data_dir;
    EventSink sink;
    fpe_player* player = nullptr;
    std::string device;
    int volume = 100;

    static void on_event(void* user, fpe_player*, int request, int event, const char* text) {
        auto* self = static_cast<Impl*>(user);
        Event e;
        switch (event) {
        case FPE_EVENT_STATUS: e = Event::Status; break;
        case FPE_EVENT_OPENED: e = Event::Opened; break;
        case FPE_EVENT_FAILED: e = Event::Failed; break;
        case FPE_EVENT_ENDED: e = Event::Ended; break;
        case FPE_EVENT_TITLE: e = Event::Title; break;
        default: return;
        }
        if (self->sink)
            self->sink(e, request, text ? text : "");
    }

    // The player, made for the device chosen. Null if the engine is unavailable.
    fpe_player* ensure_player() {
        if (player)
            return player;
        if (!ensure_engine(data_dir))
            return nullptr;
        fpe_player_config config{};
        config.device = device.c_str();
        config.on_event = &Impl::on_event;
        config.user = this;
        player = fpe_player_create(&config);
        if (player)
            fpe_set_volume(player, static_cast<float>(volume) / 100.0f);
        return player;
    }
};

MediaPlayer::MediaPlayer(const std::filesystem::path& data_dir, EventSink sink)
    : impl_(std::make_unique<Impl>()) {
    impl_->data_dir = data_dir;
    impl_->sink = std::move(sink);
}

MediaPlayer::~MediaPlayer() {
    if (impl_->player)
        fpe_player_destroy(impl_->player); // no callbacks after this
}

bool MediaPlayer::available() { return true; }
bool MediaPlayer::youtube_supported() { return fpe_youtube_supported() != 0; }
bool MediaPlayer::is_youtube_url(const std::string& url) {
    return youtube_supported() && fpe_is_youtube_url(url.c_str()) != 0;
}

std::vector<std::string> MediaPlayer::output_devices() {
    std::vector<std::string> names;
    const int count = fpe_device_count();
    for (int i = 0; i < count; ++i) {
        char buffer[512];
        if (fpe_device_name(i, buffer, sizeof buffer) >= 0 && buffer[0])
            names.emplace_back(buffer);
    }
    return names;
}

void MediaPlayer::set_device(const std::string& name) {
    if (name == impl_->device)
        return;
    impl_->device = name;
    if (impl_->player && fpe_state(impl_->player) == FPE_STATE_EMPTY)
        fpe_set_device(impl_->player, name.c_str());
}

void MediaPlayer::set_volume(int percent) {
    impl_->volume = std::clamp(percent, 0, 100);
    if (impl_->player)
        fpe_set_volume(impl_->player, static_cast<float>(impl_->volume) / 100.0f);
}

int MediaPlayer::open(const std::string& url) {
    fpe_player* p = impl_->ensure_player();
    if (!p)
        return 0;
    // The device as it is now set (a stream already playing keeps its own)
    fpe_close(p);
    fpe_set_device(p, impl_->device.c_str());
    return fpe_open(p, url.c_str(), 1);
}

void MediaPlayer::close() {
    if (impl_->player)
        fpe_close(impl_->player);
}

bool MediaPlayer::active() const {
    return impl_->player && fpe_state(impl_->player) != FPE_STATE_EMPTY;
}

bool MediaPlayer::playing() const {
    return impl_->player && fpe_state(impl_->player) == FPE_STATE_PLAYING;
}

bool MediaPlayer::toggle_pause() { return impl_->player && fpe_toggle_pause(impl_->player) != 0; }

bool MediaPlayer::seek_by(double seconds) {
    return impl_->player && fpe_seek_by(impl_->player, seconds) != 0;
}

double MediaPlayer::position() const { return impl_->player ? fpe_position(impl_->player) : 0.0; }
double MediaPlayer::length() const { return impl_->player ? fpe_length(impl_->player) : 0.0; }
bool MediaPlayer::live() const { return impl_->player && fpe_is_live(impl_->player) != 0; }


// The engine names its effects by key only, so the display names live here -- the
// core composes every string the apps show.
static std::string effect_display_name(const std::string& key) {
    if (key == "reverb")
        return "Reverb";
    if (key == "echo")
        return "Echo";
    if (key == "eq")
        return "EQ";
    if (key == "compressor")
        return "Compressor";
    if (key == "stereo_width")
        return "Stereo Width";
    if (key == "center_cancel")
        return "Center Cancel";
    if (key == "convolution")
        return "Convolution Reverb";
    if (key == "3d_audio")
        return "3D Audio";
    if (key == "normalizer")
        return "Normalizer";
    return key; // a new effect the engine grew: better its key than nothing
}

std::vector<MediaPlayer::Effect> MediaPlayer::effects() const {
#ifdef FASTSM_FASTPLAY_ENGINE
    std::vector<Effect> out;
    fpe_player* p = impl_->ensure_player();
    const int n = fpe_effect_count();
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const char* key = fpe_effect_key(i);
        if (!key || !*key)
            continue;
        Effect e;
        e.key = key;
        e.name = effect_display_name(e.key);
        e.enabled = p && fpe_effect_enabled(p, key) != 0;
        out.push_back(std::move(e));
    }
    return out;
#else
    return {};
#endif
}

std::vector<MediaPlayer::Param> MediaPlayer::params() const {
#ifdef FASTSM_FASTPLAY_ENGINE
    std::vector<Param> out;
    fpe_player* p = impl_->ensure_player();
    const int n = fpe_param_count();
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        fpe_param_info info{};
        if (!fpe_param_at(i, &info) || !info.key || !*info.key)
            continue;
        // Volume is deliberately left out: FastSMRW has its own media volume
        // setting, and two controls for one value would fight each other.
        if (std::string(info.key) == "volume")
            continue;
        Param q;
        q.key = info.key;
        q.name = info.name ? info.name : info.key;
        q.unit = info.unit ? info.unit : "";
        q.effect = info.effect ? info.effect : "";
        q.min_value = info.min_value;
        q.max_value = info.max_value;
        q.step = info.step;
        q.default_value = info.default_value;
        q.value = p ? fpe_get_param(p, info.key) : info.default_value;
        for (int c = 0; c < info.choices; ++c) {
            char name[128] = {};
            if (fpe_param_choice(info.key, c, name, static_cast<int>(sizeof(name))))
                q.choices.emplace_back(name);
            else
                q.choices.emplace_back(std::to_string(c));
        }
        out.push_back(std::move(q));
    }
    return out;
#else
    return {};
#endif
}

bool MediaPlayer::set_effect(const std::string& key, bool on) {
#ifdef FASTSM_FASTPLAY_ENGINE
    fpe_player* p = impl_->ensure_player();
    return p && fpe_set_effect(p, key.c_str(), on ? 1 : 0) != 0;
#else
    (void)key;
    (void)on;
    return false;
#endif
}

bool MediaPlayer::set_param(const std::string& key, float value) {
#ifdef FASTSM_FASTPLAY_ENGINE
    fpe_player* p = impl_->ensure_player();
    return p && fpe_set_param(p, key.c_str(), value) != 0;
#else
    (void)key;
    (void)value;
    return false;
#endif
}

void MediaPlayer::set_reverb_type(int type) {
#ifdef FASTSM_FASTPLAY_ENGINE
    if (fpe_player* p = impl_->ensure_player())
        fpe_set_reverb_type(p, type);
#else
    (void)type;
#endif
}

int MediaPlayer::import_settings(const std::string& ini) {
    fpe_player* p = impl_->ensure_player();
    return p ? fpe_settings_import(p, ini.c_str()) : 0;
}

std::string MediaPlayer::export_settings() {
    fpe_player* p = impl_->ensure_player();
    if (!p)
        return {};
    const int n = fpe_settings_export(p, nullptr, 0);
    std::string text(static_cast<size_t>(n) + 1, '\0');
    fpe_settings_export(p, text.data(), n + 1);
    text.resize(static_cast<size_t>(n));
    return text;
}

void MediaPlayer::reset_settings() {
    if (fpe_player* p = impl_->ensure_player())
        fpe_settings_reset(p);
}

std::string MediaPlayer::title() const {
    if (!impl_->player)
        return {};
    char buffer[512];
    const int n = fpe_title(impl_->player, buffer, sizeof buffer);
    if (n < static_cast<int>(sizeof buffer))
        return buffer;
    std::string whole(static_cast<size_t>(n) + 1, '\0');
    fpe_title(impl_->player, whole.data(), static_cast<int>(whole.size()));
    whole.resize(static_cast<size_t>(n));
    return whole;
}

#else // no engine in this build: the apps' own players play media

struct MediaPlayer::Impl {};

MediaPlayer::MediaPlayer(const std::filesystem::path&, EventSink) : impl_(std::make_unique<Impl>()) {}
MediaPlayer::~MediaPlayer() = default;
bool MediaPlayer::available() { return false; }
bool MediaPlayer::youtube_supported() { return false; }
bool MediaPlayer::is_youtube_url(const std::string&) { return false; }
std::vector<std::string> MediaPlayer::output_devices() { return {}; }
void MediaPlayer::set_device(const std::string&) {}
void MediaPlayer::set_volume(int) {}
int MediaPlayer::open(const std::string&) { return 0; }
void MediaPlayer::close() {}
bool MediaPlayer::active() const { return false; }
bool MediaPlayer::playing() const { return false; }
bool MediaPlayer::toggle_pause() { return false; }
bool MediaPlayer::seek_by(double) { return false; }
double MediaPlayer::position() const { return 0.0; }
double MediaPlayer::length() const { return 0.0; }
bool MediaPlayer::live() const { return false; }
std::string MediaPlayer::title() const { return {}; }
int MediaPlayer::import_settings(const std::string&) { return 0; }
std::string MediaPlayer::export_settings() { return {}; }
void MediaPlayer::reset_settings() {}

#endif

} // namespace fastsm::media
