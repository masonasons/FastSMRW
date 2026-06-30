#pragma once

#include <filesystem>

namespace fastsm::sound {

// Plays earcons from a soundpack directory via miniaudio. Cross-platform and
// part of the shared core. Missing files are a no-op (the app ships without
// bundled sounds; drop a soundpack in to enable).
class SoundManager {
public:
    SoundManager();
    ~SoundManager();
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    void set_dir(std::filesystem::path dir) { dir_ = std::move(dir); }
    void set_enabled(bool enabled) { enabled_ = enabled; }

    // Plays "<name>.wav" from the soundpack dir if present.
    void play(const char* name);

private:
    struct Impl;
    Impl* impl_;
    std::filesystem::path dir_;
    bool enabled_ = true;
};

} // namespace fastsm::sound
