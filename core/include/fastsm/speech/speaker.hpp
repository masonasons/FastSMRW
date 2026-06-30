#pragma once

#include <memory>
#include <string>

// Speech output abstraction. The core composes all the text (StatusPresenter +
// SpeechConfig); a Speaker just sends a finished UTF-8 string to the screen
// reader / TTS. The Windows front end backs this with UniversalSpeech
// (samtupy/UniversalSpeechMSVCStatic). Used for the "invisible interface"
// announcements; full per-event speech lands in M3.
namespace fastsm::speech {

class Speaker {
public:
    virtual ~Speaker() = default;

    // Speak a UTF-8 string. interrupt=true cuts off any current speech.
    virtual void speak(const std::string& utf8, bool interrupt) = 0;
    virtual void stop() = 0;
};

// Does nothing; used when no speech backend is available.
class NoopSpeaker : public Speaker {
public:
    void speak(const std::string&, bool) override {}
    void stop() override {}
};

} // namespace fastsm::speech
