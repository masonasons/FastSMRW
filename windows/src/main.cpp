// FastSMRW — Win32 front end entry point.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>

#include <filesystem>

#include "app_controller.hpp"
#include "app_messages.hpp"
#include "main_window.hpp"
#include "win_executor.hpp"

#include "fastsm/sound/sound_manager.hpp"

#pragma comment(lib, "comctl32.lib")

using namespace fastsmui;

namespace {
std::filesystem::path exe_dir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WinExecutor executor;
    fastsm::sound::SoundManager sound;
    sound.set_dir(exe_dir() / "sounds");

    MainWindow window(hInstance, &executor);
    if (!window.create())
        return 1;
    executor.bind(window.hwnd(), WM_APP_DISPATCH);

    AppController app(&executor, &sound);
    window.set_app(&app);
    app.set_view(&window);

    ShowWindow(window.hwnd(), nCmdShow);
    UpdateWindow(window.hwnd());

    app.bootstrap();

    const HACCEL accel = window.accel();
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (accel && TranslateAcceleratorW(window.hwnd(), accel, &msg))
            continue;
        // Handle Tab between panes ourselves (IsDialogMessage would swallow the
        // list views' arrow keys), but let the dialogs run their own loops.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && GetActiveWindow() == window.hwnd()) {
            window.cycle_focus();
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
