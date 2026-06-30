#include "check.hpp"

#include <filesystem>

#include "fastsm/presentation/speech_settings.hpp"
#include "fastsm/store/app_settings.hpp"

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
    const auto dir = std::filesystem::temp_directory_path() / "fastsmrw_settings_test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto path = dir / "settings.json";

    store::SettingsStore st(path);
    store::AppSettings s;
    s.sounds_enabled = false;
    s.soundpack = "MyPack";
    s.speech = SpeechSettings::defaults();
    for (auto& it : s.speech.status)
        if (it.field == StatusSpeechField::Handle)
            it.enabled = true; // user turned handle on

    CHECK(st.save(s));
    const store::AppSettings loaded = st.load();

    CHECK(!loaded.sounds_enabled);
    CHECK_EQ(loaded.soundpack, std::string("MyPack"));
    bool handle_on = false;
    for (const auto& it : loaded.speech.status)
        if (it.field == StatusSpeechField::Handle)
            handle_on = it.enabled;
    CHECK(handle_on);
    CHECK(loaded.speech == s.speech.normalized());

    std::filesystem::remove_all(dir, ec);
}
