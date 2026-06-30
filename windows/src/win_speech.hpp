#pragma once

#include "fastsm/speech/speaker.hpp"

namespace fastsmui {

// Speaker backed by UniversalSpeech (samtupy/UniversalSpeechMSVCStatic) when
// the dependency is present at build time (HAVE_UNIVERSALSPEECH, set by
// build.bat after download-deps clones it); otherwise a no-op. Run
// download-deps.bat to enable it.
class WinSpeaker : public fastsm::speech::Speaker {
public:
    void speak(const std::string& utf8, bool interrupt) override;
    void stop() override;
};

} // namespace fastsmui
