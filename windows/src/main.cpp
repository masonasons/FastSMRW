// FastSMRW — Win32 front end entry point (M0 shell).
//
// For now this stands up the main window structure that the M1 UI will fill in:
// a horizontal split with a narrow "Timelines" list on the left and the posts
// "Timeline" list on the right, both as report-mode ListViews (natively
// accessible to screen readers). Bootstrap, account management, and live data
// arrive in M1.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commctrl.h>

#include <string>

#include "fastsm/fastsm.hpp"
#include "../resources/resource.h"

#pragma comment(lib, "comctl32.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"FastSMRWMain";
constexpr int kTimelinesPaneWidth = 220; // device-independent at 96 DPI
constexpr int kMinWindowWidth = 920;
constexpr int kMinWindowHeight = 720;

HWND g_timelinesList = nullptr;
HWND g_timelineView = nullptr;

// Scale a 96-DPI value for the window's current DPI.
int Scale(HWND hwnd, int value) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return MulDiv(value, static_cast<int>(dpi), 96);
}

HWND CreateListView(HWND parent, int id) {
    HWND list = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LVS_REPORT |
            LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (list) {
        ListView_SetExtendedListViewStyle(
            list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    }
    return list;
}

void AddColumn(HWND list, const wchar_t* title, int width) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(title);
    col.cx = width;
    ListView_InsertColumn(list, 0, &col);
}

void AddRow(HWND list, int index, const wchar_t* text) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    item.pszText = const_cast<wchar_t*>(text);
    ListView_InsertItem(list, &item);
}

void LayoutChildren(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int paneWidth = Scale(hwnd, kTimelinesPaneWidth);
    const int height = rc.bottom - rc.top;
    const int total = rc.right - rc.left;

    MoveWindow(g_timelinesList, 0, 0, paneWidth, height, TRUE);
    MoveWindow(g_timelineView, paneWidth, 0, total - paneWidth, height, TRUE);

    // Keep the single column filling each list.
    ListView_SetColumnWidth(g_timelinesList, 0, paneWidth - Scale(hwnd, 24));
    ListView_SetColumnWidth(g_timelineView, 0, LVSCW_AUTOSIZE_USEHEADER);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_timelinesList = CreateListView(hwnd, IDC_TIMELINES_LIST);
        g_timelineView = CreateListView(hwnd, IDC_TIMELINE_VIEW);

        AddColumn(g_timelinesList, L"Timelines", Scale(hwnd, kTimelinesPaneWidth) - 24);
        AddColumn(g_timelineView, L"Timeline", Scale(hwnd, 600));

        // Placeholder content until M1 wires in real accounts/timelines.
        AddRow(g_timelinesList, 0, L"Home");
        AddRow(g_timelineView, 0, L"(no account yet — Add Account arrives in M1)");
        return 0;
    }

    case WM_SIZE:
        LayoutChildren(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = kMinWindowWidth;
        mmi->ptMinTrackSize.y = kMinWindowHeight;
        return 0;
    }

    case WM_SETFOCUS:
        SetFocus(g_timelineView);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&wc))
        return 1;

    HWND hwnd = CreateWindowExW(
        0, kWindowClass, L"FastSMRW",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, kMinWindowWidth, kMinWindowHeight,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd)
        return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Touch the core so the link dependency is real even in the M0 shell.
    std::wstring title = L"FastSMRW v";
    for (const char* p = fastsm::version(); *p; ++p)
        title.push_back(static_cast<wchar_t>(*p));
    SetWindowTextW(hwnd, title.c_str());

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // IsDialogMessage gives us Tab traversal between the panes for free.
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}
