#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio/miniaudio.h>

#include "fastsm/sound/sound_manager.hpp"

#include <algorithm>
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
};

SoundManager::SoundManager() : impl_(new Impl) {
    if (ma_engine_init(nullptr, &impl_->engine) == MA_SUCCESS)
        impl_->ok = true;
}

SoundManager::~SoundManager() {
    if (impl_->ok) {
        impl_->stop_all();
        ma_engine_uninit(&impl_->engine);
    }
    delete impl_;
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

std::vector<std::filesystem::path> SoundManager::search_dirs() const {
    std::vector<std::filesystem::path> dirs;
    const bool is_default = active_pack_.empty() || active_pack_ == "Default" ||
                            active_pack_ == "default";
    if (!is_default) {
        if (!user_packs_dir_.empty())
            dirs.push_back(user_packs_dir_ / active_pack_);
        if (!bundled_packs_dir_.empty())
            dirs.push_back(bundled_packs_dir_ / active_pack_);
    }
    // Default pack fallback (always last).
    if (!user_packs_dir_.empty())
        dirs.push_back(user_packs_dir_ / "default");
    if (!bundled_packs_dir_.empty())
        dirs.push_back(bundled_packs_dir_ / "default");
    return dirs;
}

std::filesystem::path SoundManager::resolve(const std::string& base) const {
    static const char* kExts[] = {".ogg", ".wav", ".mp3"};
    std::error_code ec;
    for (const auto& dir : search_dirs()) {
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

void SoundManager::play(Earcon e) {
    const char* base = earcon_file(e);
    if (base && *base)
        play_named(base);
}

void SoundManager::play_named(const std::string& base) {
    if (!enabled_ || !impl_->ok)
        return;
    const std::filesystem::path path = resolve(base);
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
