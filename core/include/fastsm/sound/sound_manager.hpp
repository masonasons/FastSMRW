#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fastsm::sound {

// Semantic earcons. Each maps to a base filename looked up in the active
// soundpack (falling back to the default pack). `Navigate` is intentionally
// SILENT — like the Mac app, row movement is conveyed by the screen reader, not
// an earcon.
// Mirrors the Mac Earcon enum exactly. Per-timeline "new items" chimes are NOT
// here — they use SoundManager::play_named(source.new_items_sound_name()).
enum class Earcon {
    Navigate, // silent — row movement is conveyed by the screen reader
    Boundary, // hit the top/bottom of a list
    PostSent, // a new post was sent
    ReplySent, // a reply was sent (distinct chime, like FastSM for Windows)
    Boost,    // a boost (repost) succeeded; not played on un-boost
    Favorite,
    Unfavorite,
    Close,   // a timeline was closed/dismissed
    Delete,  // a timeline was cleared / content deleted
    Refresh, // a timeline finished loading ("ready")
    Error,
};

// Returns the soundpack base filename for an earcon, or "" if silent.
const char* earcon_file(Earcon e);

// Plays earcons from soundpacks: folders of .ogg (also .wav/.mp3) files named by
// event (boundary, like, send_post, ...). A bundled "default" pack ships with
// the app; users drop additional packs into the user packs dir and select one.
// Missing sounds fall back to the default pack, then silently no-op.
//
// OGG Vorbis is decoded with stb_vorbis; playback/mixing is miniaudio. Multiple
// earcons may overlap. Part of the shared core.
class SoundManager {
public:
    SoundManager();
    ~SoundManager();
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    void set_user_packs_dir(std::filesystem::path dir) { user_packs_dir_ = std::move(dir); }
    void set_bundled_packs_dir(std::filesystem::path dir) { bundled_packs_dir_ = std::move(dir); }

    // "" or "Default"/"default" selects the bundled default pack.
    void set_soundpack(const std::string& name);
    void set_enabled(bool enabled) { enabled_ = enabled; }
    void set_volume(float volume) { volume_ = volume; } // 0.0 .. 1.0

    // ["Default", <user packs sorted>].
    std::vector<std::string> list_soundpacks() const;

    // Play an earcon / named sound. `pack` overrides which soundpack to draw from
    // (empty = the active pack set by set_soundpack); used so a background
    // account's chime can sound in that account's pack.
    void play(Earcon e, const std::string& pack = {});
    void play_named(const std::string& base, const std::string& pack = {});

private:
    struct Impl;
    Impl* impl_;

    // Ordered pack directories to search for a sound (the named pack, then default).
    std::vector<std::filesystem::path> search_dirs(const std::string& pack) const;
    std::filesystem::path resolve(const std::string& base, const std::string& pack) const;

    std::filesystem::path user_packs_dir_;
    std::filesystem::path bundled_packs_dir_;
    std::string active_pack_;
    bool enabled_ = true;
    float volume_ = 1.0f;
};

} // namespace fastsm::sound
