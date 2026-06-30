// FastSMRW — Win32 front end entry point. A pure client of the core's C ABI.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "fastsm/capi/fastsm_core.h"
#include "fastsm/store/paths.hpp"

#include "main_window.hpp"
#include "utf.hpp"
#include "win_speech.hpp"

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
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES |
                                              ICC_DATE_CLASSES | ICC_TAB_CLASSES |
                                              ICC_UPDOWN_CLASS};
    InitCommonControlsEx(&icc);

    WinSpeaker speaker; // UniversalSpeech-backed when the dep is present, else no-op

    MainWindow window(hInstance);
    if (!window.create())
        return 1;
    window.set_speaker(&speaker);

    // Build the core: it gets the data folder (config.json + cache + user
    // soundpacks) and where the bundled soundpacks ship (next to the exe).
    nlohmann::json cfg;
    cfg["config_dir"] = to_utf8(fastsm::store::config_dir().wstring());
    cfg["soundpacks_dir"] = to_utf8((exe_dir() / L"soundpacks").wstring());
    cfg["user_agent"] = "FastSMRW/0.0.1";
    const std::string cfg_str = cfg.dump();

    fastsm_core* core = fastsm_core_create(cfg_str.c_str());
    if (!core)
        return 1;
    window.set_core(core);
    fastsm_core_set_event_sink(core, &MainWindow::event_sink, &window);

    ShowWindow(window.hwnd(), nCmdShow);
    UpdateWindow(window.hwnd());

    const std::string start = R"({"cmd":"start"})";
    fastsm_core_dispatch(core, start.c_str(), start.size());

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

    fastsm_core_destroy(core); // stops the core threads
    return static_cast<int>(msg.wParam);
}
