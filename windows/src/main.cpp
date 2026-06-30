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

#include "fastsm/presentation/speech_settings.hpp"
#include "fastsm/sound/sound_manager.hpp"
#include "fastsm/store/app_settings.hpp"
#include "fastsm/store/paths.hpp"

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

    // Load preferences; the core's presenters read SpeechConfig for field order.
    const fastsm::store::AppSettings settings = fastsm::store::SettingsStore::default_store().load();
    fastsm::present::SpeechConfig::set_current(settings.speech);

    fastsm::sound::SoundManager sound;
    // Bundled packs ship next to the exe (dist/soundpacks); user packs live in
    // %APPDATA%\FastSMRW\soundpacks.
    sound.set_bundled_packs_dir(exe_dir() / "soundpacks");
    sound.set_user_packs_dir(fastsm::store::config_dir() / "soundpacks");
    sound.set_enabled(settings.sounds_enabled);
    sound.set_soundpack(settings.soundpack);

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
