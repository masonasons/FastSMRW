#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio/miniaudio.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "fastsm/sound/sound_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

// stb_vorbis is compiled separately as C; we just need this one entry point.
extern "C" int stb_vorbis_decode_filename(const char* filename, int* channels, int* sample_rate,
                                          short** output);

namespace fastsm::sound {

const char* earcon_file(Earcon e) {
    switch (e) {
    case Earcon::Navigate:
        return ""; // silent
    case Earcon::Boundary:
        return "boundary";
    case Earcon::PostSent:
        return "send_post";
    case Earcon::ReplySent:
        return "send_reply";
    case Earcon::Boost:
        return "send_repost";
    case Earcon::Favorite:
        return "like";
    case Earcon::Unfavorite:
        return "unlike";
    case Earcon::Close:
        return "close";
    case Earcon::Delete:
        return "delete";
    case Earcon::Refresh:
        return "ready";
    case Earcon::Error:
        return "error";
    }
    return "";
}

namespace {

struct DecodedPcm {
    std::vector<short> samples; // interleaved s16
    int channels = 0;
    int rate = 0;
    ma_uint64 frames = 0;
};

// One playing voice. Holds its own buffer ref (cursor) over shared, cached PCM.
struct Voice {
    ma_sound sound{};
    ma_audio_buffer_ref ref{};
    bool has_ref = false;
};

} // namespace

struct SoundManager::Impl {
    ma_engine engine{};
    bool ok = false;
    std::unordered_map<std::string, DecodedPcm> pcm_cache; // keyed by file path
    std::vector<std::unique_ptr<Voice>> voices;

    void cleanup_finished() {
        voices.erase(std::remove_if(voices.begin(), voices.end(),
                                    [](const std::unique_ptr<Voice>& v) {
                                        if (ma_sound_is_playing(&v->sound) == MA_TRUE)
                                            return false;
                                        ma_sound_uninit(&v->sound);
                                        if (v->has_ref)
                                            ma_audio_buffer_ref_uninit(&v->ref);
                                        return true;
                                    }),
                     voices.end());
    }

    void stop_all() {
        for (auto& v : voices) {
            ma_sound_uninit(&v->sound);
            if (v->has_ref)
                ma_audio_buffer_ref_uninit(&v->ref);
        }
        voices.clear();
    }

    // Whether the engine still has a running output device. Restarting the Windows
    // audio service (or pulling the output device) stops the device out from under
    // us: the engine keeps accepting sounds and reports success, but nothing is
    // audible ever again. There's no callback we get on the core loop, so check
    // before playing.
    bool device_running() {
        if (!ok)
            return false;
        ma_device* dev = ma_engine_get_device(&engine);
        return dev != nullptr && ma_device_get_state(dev) == ma_device_state_started;
    }

    // Don't hammer ma_engine_init when there's genuinely no output device (nothing
    // plugged in, service still coming back up) — each attempt is slow.
    std::chrono::steady_clock::time_point last_revive{};
    bool revive_due() {
        const auto now = std::chrono::steady_clock::now();
        if (last_revive.time_since_epoch().count() != 0 &&
            now - last_revive < std::chrono::seconds(2))
            return false;
        last_revive = now;
        return true;
    }

#if defined(__APPLE__) && TARGET_OS_IPHONE
    ma_context context{};
    bool context_ok = false;
#endif

    // Bring up the engine. On iOS, miniaudio's default takes over the app's
    // audio session with PlayAndRecord — which grabs the microphone and routes
    // output to the quiet earpiece speaker. The app owns the session there
    // (playback, mixed with VoiceOver/music), so tell miniaudio to leave it
    // alone; the context carrying that choice is reused across engine revives.
    bool init_engine() {
#if defined(__APPLE__) && TARGET_OS_IPHONE
        if (!context_ok) {
            ma_context_config cfg = ma_context_config_init();
            cfg.coreaudio.sessionCategory = ma_ios_session_category_none;
            if (ma_context_init(nullptr, 0, &cfg, &context) != MA_SUCCESS)
                return false;
            context_ok = true;
        }
        ma_engine_config cfg = ma_engine_config_init();
        cfg.pContext = &context;
        return ma_engine_init(&cfg, &engine) == MA_SUCCESS;
#else
        return ma_engine_init(nullptr, &engine) == MA_SUCCESS;
#endif
    }
};

SoundManager::SoundManager() : impl_(new Impl) {
    if (impl_->init_engine())
        impl_->ok = true;
}

SoundManager::~SoundManager() {
    if (impl_->ok) {
        impl_->stop_all();
        ma_engine_uninit(&impl_->engine);
    }
#if defined(__APPLE__) && TARGET_OS_IPHONE
    if (impl_->context_ok)
        ma_context_uninit(&impl_->context);
#endif
    delete impl_;
}

void SoundManager::reinitialize() {
    // Stop live voices and drop the old engine (which may be bound to a device the
    // OS invalidated across a sleep/hibernation cycle), then bring a fresh one up.
    // The decoded-PCM cache is engine-independent, so it survives untouched.
    impl_->stop_all();
    if (impl_->ok) {
        ma_engine_uninit(&impl_->engine);
        impl_->ok = false;
    }
    impl_->engine = ma_engine{};
    if (impl_->init_engine())
        impl_->ok = true;
}

void SoundManager::set_soundpack(const std::string& name) {
    if (name == active_pack_)
        return;
    active_pack_ = name;
    // The cache is keyed by path so it stays valid, but a different pack may
    // shadow files; clear it (and stop voices that reference it) to be safe.
    if (impl_->ok) {
        impl_->stop_all();
        impl_->pcm_cache.clear();
    }
}

std::vector<std::filesystem::path> SoundManager::search_dirs(const std::string& pack) const {
    std::vector<std::filesystem::path> dirs;
    const bool is_default = pack.empty() || pack == "Default" || pack == "default";
    if (!is_default) {
        if (!user_packs_dir_.empty())
            dirs.push_back(user_packs_dir_ / pack);
        if (!bundled_packs_dir_.empty())
            dirs.push_back(bundled_packs_dir_ / pack);
    }
    // Default pack fallback (always last).
    if (!user_packs_dir_.empty())
        dirs.push_back(user_packs_dir_ / "default");
    if (!bundled_packs_dir_.empty())
        dirs.push_back(bundled_packs_dir_ / "default");
    return dirs;
}

std::filesystem::path SoundManager::resolve(const std::string& base, const std::string& pack) const {
    static const char* kExts[] = {".ogg", ".wav", ".mp3"};
    std::error_code ec;
    for (const auto& dir : search_dirs(pack)) {
        for (const char* ext : kExts) {
            std::filesystem::path candidate = dir / (base + ext);
            if (std::filesystem::exists(candidate, ec))
                return candidate;
        }
    }
    return {};
}

std::vector<std::string> SoundManager::list_soundpacks() const {
    std::vector<std::string> packs;
    packs.push_back("Default");
    std::error_code ec;
    for (const auto& root : {user_packs_dir_, bundled_packs_dir_}) {
        if (root.empty() || !std::filesystem::is_directory(root, ec))
            continue;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (!entry.is_directory(ec))
                continue;
            const std::string name = entry.path().filename().string();
            if (name == "default" || name == "Default")
                continue;
            if (std::find(packs.begin(), packs.end(), name) == packs.end())
                packs.push_back(name);
        }
    }
    std::sort(packs.begin() + 1, packs.end());
    return packs;
}

void SoundManager::play(Earcon e, const std::string& pack) {
    const char* base = earcon_file(e);
    if (base && *base)
        play_named(base, pack);
}

void SoundManager::play_named(const std::string& base, const std::string& pack) {
    if (!enabled_)
        return;
    // Self-heal a dead output device (audio service restarted, device unplugged or
    // re-routed) rather than going silent until the app is restarted.
    if (!impl_->device_running()) {
        if (!impl_->revive_due())
            return;
        reinitialize();
        if (!impl_->ok)
            return;
    }
    // Empty pack means "use whatever set_soundpack selected".
    const std::filesystem::path path = resolve(base, pack.empty() ? active_pack_ : pack);
    if (path.empty())
        return;

    impl_->cleanup_finished();
    auto voice = std::make_unique<Voice>();

    const std::string path_str = path.string();
    const bool is_ogg = path.extension() == ".ogg" || path.extension() == ".OGG";
    if (is_ogg) {
        auto it = impl_->pcm_cache.find(path_str);
        if (it == impl_->pcm_cache.end()) {
            short* out = nullptr;
            int ch = 0, rate = 0;
            const int frames = stb_vorbis_decode_filename(path_str.c_str(), &ch, &rate, &out);
            if (frames < 0 || out == nullptr)
                return;
            DecodedPcm pcm;
            pcm.channels = ch;
            pcm.rate = rate;
            pcm.frames = static_cast<ma_uint64>(frames);
            pcm.samples.assign(out, out + static_cast<size_t>(frames) * static_cast<size_t>(ch));
            std::free(out);
            it = impl_->pcm_cache.emplace(path_str, std::move(pcm)).first;
        }
        const DecodedPcm& pcm = it->second;
        if (ma_audio_buffer_ref_init(ma_format_s16, static_cast<ma_uint32>(pcm.channels),
                                     pcm.samples.data(), pcm.frames, &voice->ref) != MA_SUCCESS)
            return;
        voice->ref.sampleRate = static_cast<ma_uint32>(pcm.rate);
        voice->has_ref = true;
        if (ma_sound_init_from_data_source(&impl_->engine, &voice->ref,
                                           MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                           &voice->sound) != MA_SUCCESS) {
            ma_audio_buffer_ref_uninit(&voice->ref);
            return;
        }
    } else {
        if (ma_sound_init_from_file(&impl_->engine, path_str.c_str(),
                                    MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr,
                                    &voice->sound) != MA_SUCCESS)
            return;
    }

    ma_sound_set_volume(&voice->sound, volume_);
    ma_sound_start(&voice->sound);
    impl_->voices.push_back(std::move(voice));
}

} // namespace fastsm::sound
