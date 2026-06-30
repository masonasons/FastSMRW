#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio/miniaudio.h>

#include "fastsm/sound/sound_manager.hpp"

#include <string>
#include <system_error>

namespace fastsm::sound {

struct SoundManager::Impl {
    ma_engine engine{};
    bool ok = false;
};

SoundManager::SoundManager() : impl_(new Impl) {
    if (ma_engine_init(nullptr, &impl_->engine) == MA_SUCCESS)
        impl_->ok = true;
}

SoundManager::~SoundManager() {
    if (impl_->ok)
        ma_engine_uninit(&impl_->engine);
    delete impl_;
}

void SoundManager::play(const char* name) {
    if (!enabled_ || !impl_->ok || dir_.empty())
        return;
    const std::filesystem::path path = dir_ / (std::string(name) + ".wav");
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return;
    ma_engine_play_sound(&impl_->engine, path.string().c_str(), nullptr);
}

} // namespace fastsm::sound
