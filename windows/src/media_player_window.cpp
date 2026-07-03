#include "media_player_window.hpp"

#include <algorithm>
#include <string>

#include <dshow.h>

namespace fastsmui {

// One streaming DirectShow graph rendering an HTTP audio URL.
struct MediaPlayback::Impl {
    IGraphBuilder* graph = nullptr;
    IMediaControl* control = nullptr;
    IMediaSeeking* seek = nullptr;
    IBasicAudio* audio = nullptr;
    IMediaEventEx* event = nullptr;
    bool own_com = false;
    int volume = 100;

    void release() {
        if (control)
            control->Stop();
        auto rel = [](auto*& p) {
            if (p) {
                p->Release();
                p = nullptr;
            }
        };
        rel(event);
        rel(audio);
        rel(seek);
        rel(control);
        rel(graph);
    }
};

MediaPlayback::MediaPlayback() : impl_(new Impl) {}
MediaPlayback::~MediaPlayback() {
    stop();
    delete impl_;
}

bool MediaPlayback::active() const { return impl_->graph != nullptr; }
int MediaPlayback::volume_pct() const { return impl_->volume; }

bool MediaPlayback::play(const std::wstring& url) {
    stop();
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    impl_->own_com = (hr == S_OK || hr == S_FALSE);
    if (CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_IGraphBuilder,
                         reinterpret_cast<void**>(&impl_->graph)) != S_OK ||
        !impl_->graph) {
        stop();
        return false;
    }
    impl_->graph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&impl_->control));
    impl_->graph->QueryInterface(IID_IMediaSeeking, reinterpret_cast<void**>(&impl_->seek));
    impl_->graph->QueryInterface(IID_IBasicAudio, reinterpret_cast<void**>(&impl_->audio));
    impl_->graph->QueryInterface(IID_IMediaEventEx, reinterpret_cast<void**>(&impl_->event));
    // RenderFile on an http(s) URL builds a streaming graph (progressive read).
    if (FAILED(impl_->graph->RenderFile(url.c_str(), nullptr)) || !impl_->control) {
        stop();
        return false;
    }
    if (impl_->seek)
        impl_->seek->SetTimeFormat(&TIME_FORMAT_MEDIA_TIME);
    adjust_volume(0); // apply current volume
    if (FAILED(impl_->control->Run())) {
        stop();
        return false;
    }
    return true;
}

void MediaPlayback::stop() {
    impl_->release();
    if (impl_->own_com) {
        CoUninitialize();
        impl_->own_com = false;
    }
}

static LONGLONG to_units(double s) { return static_cast<LONGLONG>(s * 10000000.0); }
static double to_seconds(LONGLONG u) { return static_cast<double>(u) / 10000000.0; }

double MediaPlayback::position() const {
    LONGLONG cur = 0;
    if (impl_->seek)
        impl_->seek->GetCurrentPosition(&cur);
    return to_seconds(cur);
}
double MediaPlayback::duration() const {
    LONGLONG dur = 0;
    if (impl_->seek)
        impl_->seek->GetDuration(&dur);
    return to_seconds(dur);
}
void MediaPlayback::seek(double delta) {
    if (!impl_->seek)
        return;
    const double dur = duration();
    double target = position() + delta;
    target = std::clamp(target, 0.0, dur > 0 ? dur : target);
    LONGLONG pos = to_units(target);
    impl_->seek->SetPositions(&pos, AM_SEEKING_AbsolutePositioning, nullptr,
                              AM_SEEKING_NoPositioning);
}
bool MediaPlayback::toggle_pause() {
    if (!impl_->control)
        return false;
    long st = 0; // OAFILTERSTATE (a LONG): State_Stopped/Paused/Running
    impl_->control->GetState(0, &st);
    if (st == State_Running) {
        impl_->control->Pause();
        return false;
    }
    impl_->control->Run();
    return true;
}
void MediaPlayback::adjust_volume(int delta) {
    impl_->volume = std::clamp(impl_->volume + delta, 0, 100);
    if (impl_->audio) // put_Volume is -10000 (silence) .. 0 (full)
        impl_->audio->put_Volume(static_cast<long>(-10000 + impl_->volume * 100));
}
bool MediaPlayback::completed() {
    if (!impl_->event)
        return false;
    long code = 0;
    LONG_PTR p1 = 0, p2 = 0;
    bool done = false;
    while (impl_->event->GetEvent(&code, &p1, &p2, 0) == S_OK) {
        if (code == EC_COMPLETE || code == EC_ERRORABORT)
            done = true;
        impl_->event->FreeEventParams(code, p1, p2);
    }
    return done;
}

namespace {

// Heap state carried by the modeless player window for its whole lifetime.
struct WinState {
    MediaPlayback player;
    std::function<void(const std::wstring&)> speak;
};

constexpr wchar_t kClass[] = L"FastSMRWMediaPlayer";

std::wstring fmt_time(double seconds) {
    if (seconds < 0)
        seconds = 0;
    const int total = static_cast<int>(seconds + 0.5);
    wchar_t buf[32];
    wsprintfW(buf, L"%d:%02d", total / 60, total % 60);
    return buf;
}

void say(WinState* s, const std::wstring& msg) {
    if (s && s->speak)
        s->speak(msg);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        SetTimer(hwnd, 1, 500, nullptr); // poll for end-of-stream
        return 0;
    }
    auto* s = reinterpret_cast<WinState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_KEYDOWN:
        if (s) {
            switch (wp) {
            case VK_SPACE:
                say(s, s->player.toggle_pause() ? L"Playing" : L"Paused");
                return 0;
            case VK_LEFT:
                s->player.seek(-5);
                say(s, fmt_time(s->player.position()) + L" of " + fmt_time(s->player.duration()));
                return 0;
            case VK_RIGHT:
                s->player.seek(5);
                say(s, fmt_time(s->player.position()) + L" of " + fmt_time(s->player.duration()));
                return 0;
            case VK_UP:
                s->player.adjust_volume(10);
                say(s, L"Volume " + std::to_wstring(s->player.volume_pct()) + L" percent");
                return 0;
            case VK_DOWN:
                s->player.adjust_volume(-10);
                say(s, L"Volume " + std::to_wstring(s->player.volume_pct()) + L" percent");
                return 0;
            case VK_ESCAPE:
                DestroyWindow(hwnd);
                return 0;
            }
        }
        break;
    case WM_TIMER:
        if (s && s->player.completed()) {
            say(s, L"Finished");
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        KillTimer(hwnd, 1);
        delete s; // stops playback + releases COM via ~MediaPlayback
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

bool show_media_player(HWND parent, HINSTANCE inst, const std::wstring& title,
                       const std::wstring& url, std::function<void(const std::wstring&)> speak) {
    auto* state = new WinState{};
    state->speak = std::move(speak);
    if (!state->player.play(url)) {
        delete state;
        return false; // caller falls back to the system player
    }

    static ATOM registered = 0;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc);
    }

    // Keys-only window: no controls, just a caption the screen reader announces
    // ("Playing: <title>"). Escape (or the close box) stops and closes it.
    const int w = 360, h = 90;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, (L"Playing: " + title).c_str(),
                                WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, parent, nullptr,
                                inst, state);
    if (!hwnd) {
        delete state;
        return false;
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    return true; // modeless; the window owns `state` and cleans up on close
}

} // namespace fastsmui
