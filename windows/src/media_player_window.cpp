#include "media_player_window.hpp"

namespace fastsmui {

namespace {

constexpr wchar_t kClass[] = L"FastSMRWMediaPlayer";

struct WinState {
    std::function<void(const nlohmann::json&)> dispatch;
};

HWND g_player = nullptr; // the one player window, if open

void send(WinState* s, const nlohmann::json& cmd) {
    if (s && s->dispatch)
        s->dispatch(cmd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }
    auto* s = reinterpret_cast<WinState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_KEYDOWN:
        switch (wp) {
        case VK_SPACE:
            send(s, {{"cmd", "media_toggle"}});
            return 0;
        case VK_LEFT:
        case VK_RIGHT:
            send(s, {{"cmd", "media_seek"}, {"by", wp == VK_LEFT ? -5.0 : 5.0}});
            return 0;
        case VK_UP:
        case VK_DOWN:
            send(s, {{"cmd", "media_volume"}, {"by", wp == VK_UP ? 10 : -10}});
            return 0;
        case 'P':
            send(s, {{"cmd", "media_position"}});
            return 0;
        case VK_ESCAPE:
            DestroyWindow(hwnd); // stops it (WM_DESTROY)
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        // Closed here (Escape, the close box): playback stops with it. Closed by
        // the core (close_media_player) the dispatch is gone first, so nothing is sent.
        send(s, {{"cmd", "media_stop"}});
        if (g_player == hwnd)
            g_player = nullptr;
        return 0;
    case WM_NCDESTROY:
        delete s;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void show_media_player(HWND parent, HINSTANCE inst, const std::wstring& title,
                       std::function<void(const nlohmann::json&)> dispatch) {
    const std::wstring caption = L"Playing: " + title;
    if (g_player) {
        SetWindowTextW(g_player, caption.c_str());
        SetForegroundWindow(g_player);
        SetFocus(g_player);
        return;
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
    // ("Playing: <title>").
    const int w = 360, h = 90;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    auto* state = new WinState{std::move(dispatch)};
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, caption.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                x, y, w, h, parent, nullptr, inst, state);
    if (!hwnd) {
        delete state;
        return;
    }
    g_player = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
}

void retitle_media_player(const std::wstring& title) {
    if (g_player)
        SetWindowTextW(g_player, (L"Playing: " + title).c_str());
}

void close_media_player() {
    HWND hwnd = g_player;
    if (!hwnd)
        return;
    g_player = nullptr;
    // The core already knows: don't tell it to stop what it has stopped
    if (auto* s = reinterpret_cast<WinState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
        s->dispatch = nullptr;
    DestroyWindow(hwnd);
}

} // namespace fastsmui
