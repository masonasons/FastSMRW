#include "check.hpp"

#include <filesystem>

#include "fastsm/presentation/speech_settings.hpp"
#include "fastsm/store/app_config.hpp"

using namespace fastsm;
using namespace fastsm::present;

void test_speech_defaults() {
    const auto d = SpeechSettings::defaults();
    CHECK_EQ(d.status.size(), size_t(15));
    CHECK_EQ(d.user.size(), size_t(8));
    for (const auto& it : d.status) {
        if (it.field == StatusSpeechField::Handle)
            CHECK(!it.enabled); // handle off by default (Mac parity)
        if (it.field == StatusSpeechField::Author)
            CHECK(it.enabled);
        if (it.field == StatusSpeechField::Source)
            CHECK(!it.enabled);
    }
}

void test_speech_normalized() {
    // A saved config with only two fields, reordered (text before author).
    SpeechSettings partial;
    partial.status = {{StatusSpeechField::Text, true}, {StatusSpeechField::Author, false}};
    const auto norm = partial.normalized();

    CHECK_EQ(norm.status.size(), size_t(15)); // every field present exactly once
    CHECK(norm.status[0].field == StatusSpeechField::Text);   // saved order kept
    CHECK(norm.status[1].field == StatusSpeechField::Author);
    CHECK(!norm.status[1].enabled);                           // saved toggle kept
    CHECK_EQ(norm.user.size(), size_t(8));                    // user list filled from defaults
}

void test_settings_roundtrip() {
    // Settings persist inside config.json (alongside accounts).
    const auto dir = std::filesystem::temp_directory_path() / "fastsmrw_settings_test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto path = dir / "config.json";

    store::AppConfigStore st(path);
    store::AppConfig cfg;
    cfg.settings.sounds_enabled = false;
    cfg.settings.soundpack = "MyPack";
    cfg.settings.text.cw = CwMode::Ignore;
    cfg.settings.text.post_emoji = EmojiRemoval::Both;
    cfg.settings.text.name_emoji = EmojiRemoval::Unicode;
    cfg.settings.text.max_mentions = 3;
    cfg.settings.text.absolute_time = true;
    cfg.settings.reverse_timelines = true;
    cfg.settings.speech = SpeechSettings::defaults();
    cfg.settings.speech.separator = " | ";
    for (auto& it : cfg.settings.speech.status) {
        if (it.field == StatusSpeechField::Handle) {
            it.enabled = true; // user turned handle on
            it.before = "(";   // and wrapped it in parentheses
            it.after = ")";
        }
    }

    CHECK(st.save(cfg));
    const store::AppConfig loaded = st.load();

    CHECK(!loaded.settings.sounds_enabled);
    CHECK_EQ(loaded.settings.soundpack, std::string("MyPack"));
    CHECK(loaded.settings.text.cw == CwMode::Ignore);
    CHECK(loaded.settings.text.post_emoji == EmojiRemoval::Both);
    CHECK(loaded.settings.text.name_emoji == EmojiRemoval::Unicode);
    CHECK_EQ(loaded.settings.text.max_mentions, 3);
    CHECK(loaded.settings.text.absolute_time);
    CHECK(loaded.settings.reverse_timelines);
    CHECK_EQ(loaded.settings.speech.separator, std::string(" | "));
    bool handle_on = false;
    for (const auto& it : loaded.settings.speech.status)
        if (it.field == StatusSpeechField::Handle) {
            handle_on = it.enabled;
            CHECK_EQ(it.before, std::string("("));
            CHECK_EQ(it.after, std::string(")"));
        }
    CHECK(handle_on);
    CHECK(loaded.settings.speech == cfg.settings.speech.normalized());

    std::filesystem::remove_all(dir, ec);
}
