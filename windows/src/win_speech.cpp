#include "win_speech.hpp"

#ifdef HAVE_UNIVERSALSPEECH
#include "utf.hpp"
#include <UniversalSpeech.h> // from deps/UniversalSpeechMSVCStatic/include
#endif

namespace fastsmui {

void WinSpeaker::speak(const std::string& utf8, bool interrupt) {
#ifdef HAVE_UNIVERSALSPEECH
    const std::wstring w = to_wide(utf8);
    speechSay(w.c_str(), interrupt ? 1 : 0);
#else
    (void)utf8;
    (void)interrupt;
#endif
}

void WinSpeaker::stop() {
#ifdef HAVE_UNIVERSALSPEECH
    speechStop();
#endif
}

} // namespace fastsmui
