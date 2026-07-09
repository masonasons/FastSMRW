#include "main_window.hpp"

#include <cwctype>
#include <fstream>
#include <functional>
#include <optional>

#include <commctrl.h>
#include <shellapi.h>

#include "../resources/resource.h"
#include "add_account_dialog.hpp"
#include "app_messages.hpp"
#include "client_filters_dialog.hpp"
#include "account_settings_dialog.hpp"
#include "fastsm/util/log.hpp"
#include "hashtag_dialog.hpp"
#include "list_membership_dialog.hpp"
#include "lists_manager_dialog.hpp"
#include "new_timeline_dialog.hpp"
#include "server_filters_dialog.hpp"
#include "media_player_window.hpp"
#include "post_info_dialog.hpp"
#include "user_profile_dialog.hpp"
#include "settings_dialog.hpp"
#include "utf.hpp"

#include "fastsm/fastsm.hpp"
#include "fastsm/store/settings_json.hpp"

using namespace fastsm;
using nlohmann::json;

namespace fastsmui {
namespace {

constexpr wchar_t kClassName[] = L"FastSMRWMain";
constexpr int kTimelinesPaneWidth = 220;
constexpr int kMinWidth = 920;
constexpr UINT_PTR kMediaBgTimer = 1; // WM_TIMER id for background-audio end polling
constexpr int kMinHeight = 720;

enum {
    ID_ABOUT = 40010,
    ID_SETTINGS,
    ID_QUIT,
    ID_NEW_POST,
    ID_REFRESH,
    ID_REPLY,
    ID_BOOST,
    ID_FAVORITE,
    ID_QUOTE,
    ID_POST_INFO,
    ID_VIEW_THREAD,
    ID_USER_TIMELINE,
    ID_USER_PROFILE,
    ID_SPEAK_USER,
    ID_SPEAK_REPLY,
    ID_FOLLOW_TOGGLE,
    ID_OPEN_BROWSER,
    ID_OPEN_LINKS,
    ID_NEW_TIMELINE,
    ID_TOGGLE_PIN,
    ID_CLOSE_TIMELINE,
    ID_CLEAR_TIMELINE,
    ID_CLEAR_ALL,
    ID_LOAD_OLDER,
    ID_GO_BACK,
    ID_MOVE_UP,
    ID_MOVE_DOWN,
    ID_CYCLE_PREV,
    ID_CYCLE_NEXT,
    ID_PREV_ACCOUNT,
    ID_NEXT_ACCOUNT,
    ID_ADD_ACCOUNT,
    ID_ACCOUNT_SETTINGS,
    ID_KEYMAP_MANAGER,
    ID_FIND,
    ID_FIND_NEXT,
    ID_FIND_PREV,
    ID_CHECK_UPDATES,
    ID_HIDE_WINDOW,
    ID_CLIENT_FILTER,
    ID_SERVER_FILTER,
    ID_LIST_MANAGER,
    ID_VIEW_MUTES,
    ID_VIEW_BLOCKS,
    ID_FOLLOW_REQUESTS,
    ID_FOLLOWED_HASHTAGS,
    ID_STOP_MEDIA,
    ID_GOTO_TIMELINE_1 = 40100, // .. +8 for timelines 1-9
};

int window_dpi(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static GetDpiForWindowFn get_dpi = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (get_dpi)
        return static_cast<int>(get_dpi(hwnd));
    HDC dc = GetDC(hwnd);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc)
        ReleaseDC(hwnd, dc);
    return dpi > 0 ? dpi : 96;
}

int dpi_scale(HWND hwnd, int value) { return MulDiv(value, window_dpi(hwnd), 96); }

HWND make_listview(HWND parent, int id) {
    HWND list = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
            LVS_SHOWSELALWAYS,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (list)
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                    LVS_EX_LABELTIP);
    return list;
}

void add_single_column(HWND list, const wchar_t* title, int width) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(title);
    col.cx = width;
    ListView_InsertColumn(list, 0, &col);
}

bool confirm(HWND owner, const wchar_t* message, const wchar_t* title) {
    return MessageBoxW(owner, message, title, MB_YESNO | MB_ICONQUESTION) == IDYES;
}

// Modal "Layer keys" window: a read-only edit listing the layer keys (one per
// line), closed with the Close button or Escape.
INT_PTR CALLBACK layer_help_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextW(dlg, IDC_LAYERHELP_TEXT, reinterpret_cast<const wchar_t*>(lp));
        SetFocus(GetDlgItem(dlg, IDC_LAYERHELP_TEXT));
        return FALSE; // focus set explicitly
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) { // Close button or Escape
            EndDialog(dlg, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void show_layer_help(HWND parent, HINSTANCE inst, const std::string& text_utf8) {
    // Win32 multiline EDIT needs CRLF line breaks; the core composes with LF.
    std::wstring crlf;
    for (wchar_t ch : to_wide(text_utf8)) {
        if (ch == L'\n')
            crlf += L"\r\n";
        else
            crlf += ch;
    }
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_LAYER_HELP), parent, layer_help_proc,
                    reinterpret_cast<LPARAM>(crlf.c_str()));
}

json draft_to_json(const PostDraft& d) {
    json j;
    j["text"] = d.text;
    if (d.spoiler_text)
        j["spoiler_text"] = *d.spoiler_text;
    if (d.visibility)
        j["visibility"] = static_cast<int>(*d.visibility);
    if (d.language)
        j["language"] = *d.language;
    if (d.scheduled_at)
        j["scheduled_at"] = *d.scheduled_at;
    if (d.poll) {
        json p;
        p["options"] = json::array();
        for (const auto& o : d.poll->options)
            p["options"].push_back(o);
        p["multiple"] = d.poll->multiple;
        p["expires_in_seconds"] = d.poll->expires_in_seconds;
        j["poll"] = std::move(p);
    }
    return j;
}

// Mirrors the Mac main menu (MainMenu.swift); the App menu is "Application".
HMENU build_menu() {
    HMENU bar = CreateMenu();

    HMENU app = CreatePopupMenu();
    AppendMenuW(app, MF_STRING, ID_ABOUT, L"&About FastSMRW");
    AppendMenuW(app, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(app, MF_STRING, ID_SETTINGS, L"&Settings…\tCtrl+Comma");
    AppendMenuW(app, MF_STRING, ID_KEYMAP_MANAGER, L"&Keyboard Manager…");
    AppendMenuW(app, MF_STRING, ID_SERVER_FILTER, L"Ser&ver Filters…");
    AppendMenuW(app, MF_STRING, ID_CHECK_UPDATES, L"Check for &Updates…\tShift+F1");
    AppendMenuW(app, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(app, MF_STRING, ID_STOP_MEDIA, L"S&top Media Playback\tCtrl+S");
    AppendMenuW(app, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(app, MF_STRING, ID_HIDE_WINDOW, L"&Hide Window\tCtrl+H");
    AppendMenuW(app, MF_STRING, ID_QUIT, L"&Quit FastSMRW\tCtrl+Q");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(app), L"&Application");

    HMENU me = CreatePopupMenu();
    AppendMenuW(me, MF_STRING, ID_LIST_MANAGER, L"&Lists…");
    AppendMenuW(me, MF_STRING, ID_VIEW_MUTES, L"View &Muted Users");
    AppendMenuW(me, MF_STRING, ID_VIEW_BLOCKS, L"View &Blocked Users");
    AppendMenuW(me, MF_STRING, ID_FOLLOW_REQUESTS, L"View Follow &Requests");
    AppendMenuW(me, MF_STRING, ID_FOLLOWED_HASHTAGS, L"Followed Hasht&ags…");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(me), L"&Me");

    HMENU status = CreatePopupMenu();
    AppendMenuW(status, MF_STRING, ID_NEW_POST, L"&New Post\tCtrl+N");
    AppendMenuW(status, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(status, MF_STRING, ID_REPLY, L"&Reply\tR");
    AppendMenuW(status, MF_STRING, ID_BOOST, L"&Boost\tB");
    AppendMenuW(status, MF_STRING, ID_FAVORITE, L"&Favorite\tF");
    AppendMenuW(status, MF_STRING, ID_QUOTE, L"&Quote\tQ");
    AppendMenuW(status, MF_STRING, ID_POST_INFO, L"Post &Info…\tEnter");
    AppendMenuW(status, MF_STRING, ID_VIEW_THREAD, L"View &Thread");
    AppendMenuW(status, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(status, MF_STRING, ID_USER_TIMELINE, L"Open &User Timeline");
    AppendMenuW(status, MF_STRING, ID_USER_PROFILE, L"Open User &Profile\tCtrl+U");
    AppendMenuW(status, MF_STRING, ID_SPEAK_USER, L"&Speak User\tCtrl+;");
    AppendMenuW(status, MF_STRING, ID_SPEAK_REPLY, L"Speak &Referenced Reply\tCtrl+Shift+;");
    AppendMenuW(status, MF_STRING, ID_FOLLOW_TOGGLE, L"Fo&llow / Unfollow\tCtrl+L");
    AppendMenuW(status, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(status, MF_STRING, ID_OPEN_LINKS, L"Open &Links\tCtrl+O");
    AppendMenuW(status, MF_STRING, ID_OPEN_BROWSER, L"Open in Browser");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(status), L"&Status");

    HMENU timeline = CreatePopupMenu();
    AppendMenuW(timeline, MF_STRING, ID_NEW_TIMELINE, L"&New Timeline…\tCtrl+T");
    AppendMenuW(timeline, MF_STRING, ID_REFRESH, L"Re&fresh Timeline\tCtrl+R");
    AppendMenuW(timeline, MF_STRING, ID_TOGGLE_PIN, L"&Pin Timeline\tCtrl+P");
    AppendMenuW(timeline, MF_STRING, ID_CLOSE_TIMELINE, L"&Close Timeline\tCtrl+W");
    AppendMenuW(timeline, MF_STRING, ID_LOAD_OLDER, L"Load &Older Posts\tPeriod");
    AppendMenuW(timeline, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(timeline, MF_STRING, ID_FIND, L"&Find…\tCtrl+F");
    AppendMenuW(timeline, MF_STRING, ID_FIND_NEXT, L"Find &Next\tF3");
    AppendMenuW(timeline, MF_STRING, ID_FIND_PREV, L"Find &Previous\tShift+F3");
    AppendMenuW(timeline, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(timeline, MF_STRING, ID_CLIENT_FILTER, L"C&lient Filters…\tCtrl+Shift+F");
    AppendMenuW(timeline, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(timeline, MF_STRING, ID_CLEAR_TIMELINE, L"Clea&r Timeline\tCtrl+Delete");
    AppendMenuW(timeline, MF_STRING, ID_CLEAR_ALL, L"Clear &All Timelines\tCtrl+Shift+Delete");
    AppendMenuW(timeline, MF_STRING, ID_GO_BACK, L"Go &Back\tCtrl+Z");
    AppendMenuW(timeline, MF_SEPARATOR, 0, nullptr);
    for (int i = 1; i <= 9; ++i) {
        wchar_t label[40];
        wsprintfW(label, L"Go to Timeline &%d\tCtrl+%d", i, i);
        AppendMenuW(timeline, MF_STRING, ID_GOTO_TIMELINE_1 + (i - 1), label);
    }
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(timeline), L"&Timeline");

    HMENU account = CreatePopupMenu();
    AppendMenuW(account, MF_STRING, ID_ADD_ACCOUNT, L"&Add Account…\tCtrl+Shift+A");
    AppendMenuW(account, MF_STRING, ID_ACCOUNT_SETTINGS, L"Account &Settings…\tCtrl+Shift+Comma");
    AppendMenuW(account, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(account, MF_STRING, ID_PREV_ACCOUNT, L"&Previous Account\tCtrl+[");
    AppendMenuW(account, MF_STRING, ID_NEXT_ACCOUNT, L"&Next Account\tCtrl+]");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(account), L"A&ccount");

    return bar;
}

} // namespace

MainWindow::MainWindow(HINSTANCE inst) : inst_(inst) {}
MainWindow::~MainWindow() = default; // here, where MediaPlayback is a complete type

void MainWindow::event_sink(void* user, const char* event_json, size_t len) {
    auto* self = static_cast<MainWindow*>(user);
    auto* copy = new std::string(event_json, len);
    if (!self || !self->hwnd_ || !PostMessageW(self->hwnd_, WM_APP_EVENT, 0,
                                               reinterpret_cast<LPARAM>(copy)))
        delete copy;
}

bool MainWindow::create() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProcStatic;
    wc.hInstance = inst_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&wc))
        return false;

    hwnd_ = CreateWindowExW(0, kClassName, L"FastSMRW", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, kMinWidth, kMinHeight, nullptr, build_menu(), inst_,
                            this);
    if (!hwnd_)
        return false;
    hotkey_driver_.set_window(hwnd_);
    keyhook_driver_.set_window(hwnd_);

    std::vector<ACCEL> accels = {
        {FVIRTKEY | FCONTROL, 'N', ID_NEW_POST},
        {FVIRTKEY | FCONTROL, 'R', ID_REFRESH},
        {FVIRTKEY | FCONTROL, 'T', ID_NEW_TIMELINE},
        {FVIRTKEY | FCONTROL, 'P', ID_TOGGLE_PIN}, // Ctrl+P: pin/unpin the current tab
        {FVIRTKEY | FCONTROL, 'S', ID_STOP_MEDIA}, // Ctrl+S: stop background audio
        {FVIRTKEY | FCONTROL, VK_OEM_COMMA, ID_SETTINGS},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_OEM_COMMA, ID_ACCOUNT_SETTINGS}, // Ctrl+Shift+Comma
        {FVIRTKEY | FCONTROL, 'Q', ID_QUIT},
        {FVIRTKEY | FCONTROL, 'W', ID_CLOSE_TIMELINE}, // Ctrl+W: close the current timeline
        {FVIRTKEY | FCONTROL, 'H', ID_HIDE_WINDOW},    // Ctrl+H: hide the window
        {FVIRTKEY | FCONTROL | FSHIFT, 'A', ID_ADD_ACCOUNT},
        {FVIRTKEY | FCONTROL, VK_OEM_4, ID_PREV_ACCOUNT}, // Ctrl+[
        {FVIRTKEY | FCONTROL, VK_OEM_6, ID_NEXT_ACCOUNT}, // Ctrl+]
        {FVIRTKEY | FCONTROL, VK_DELETE, ID_CLEAR_TIMELINE},          // clear focused timeline
        {FVIRTKEY | FCONTROL | FSHIFT, VK_DELETE, ID_CLEAR_ALL}, // clear every timeline
        {FVIRTKEY | FCONTROL, 'Z', ID_GO_BACK},
        {FVIRTKEY | FCONTROL, 'F', ID_FIND},         // Ctrl+F: find in timeline
        {FVIRTKEY | FCONTROL | FSHIFT, 'F', ID_CLIENT_FILTER}, // Ctrl+Shift+F: client filters
        {FVIRTKEY, VK_F3, ID_FIND_NEXT},             // F3: find next
        {FVIRTKEY | FSHIFT, VK_F3, ID_FIND_PREV},    // Shift+F3: find previous
        {FVIRTKEY | FSHIFT, VK_F1, ID_CHECK_UPDATES}, // Shift+F1: check for updates
        {FVIRTKEY | FCONTROL, 'U', ID_USER_PROFILE}, // Ctrl+U: open user profile
        {FVIRTKEY | FCONTROL, VK_OEM_1, ID_SPEAK_USER},          // Ctrl+;: speak the post's user(s)
        {FVIRTKEY | FCONTROL | FSHIFT, VK_OEM_1, ID_SPEAK_REPLY}, // Ctrl+Shift+;: speak referenced reply
        {FVIRTKEY | FCONTROL, 'L', ID_FOLLOW_TOGGLE}, // Ctrl+L: follow/unfollow the author
        {FVIRTKEY | FCONTROL, 'O', ID_OPEN_LINKS},   // Ctrl+O: open links in the post
        {FVIRTKEY | FCONTROL, VK_UP, ID_MOVE_UP},     // jump up by movement unit
        {FVIRTKEY | FCONTROL, VK_DOWN, ID_MOVE_DOWN}, // jump down by movement unit
        {FVIRTKEY | FCONTROL, VK_LEFT, ID_CYCLE_PREV},  // pick previous movement unit
        {FVIRTKEY | FCONTROL, VK_RIGHT, ID_CYCLE_NEXT}, // pick next movement unit
    };
    for (int i = 0; i < 9; ++i)
        accels.push_back({FVIRTKEY | FCONTROL, static_cast<WORD>('1' + i),
                          static_cast<WORD>(ID_GOTO_TIMELINE_1 + i)});
    accel_ = CreateAcceleratorTableW(accels.data(), static_cast<int>(accels.size()));
    return true;
}

LRESULT CALLBACK MainWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self)
        return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK MainWindow::ViewProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR id, DWORD_PTR ref) {
    auto* self = reinterpret_cast<MainWindow*>(ref);
    if (msg == WM_CHAR) {
        // Single letters (R/B/F/Q/E/U/Space...) are command shortcuts handled via
        // LVN_KEYDOWN. Swallow WM_CHAR so the ListView's built-in type-ahead
        // incremental search doesn't ALSO move the selection -- that search jumped
        // focus to the top, which is what flung the user there (e.g. pressing R to
        // reply moved the position before the dialog even opened).
        return 0;
    }
    if (msg == WM_SETFOCUS && self) {
        // When focus returns to the posts list (e.g. a modal dialog just closed),
        // make sure it lands on the remembered row rather than wherever the control
        // defaults to. Suppress the default proc's selection churn, then restore.
        Timeline* tc = self->current();
        const std::string keep = tc ? tc->selected_id : std::string{};
        self->updating_selection_ = true;
        const LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        self->updating_selection_ = false;
        self->restore_selection(keep);
        return r;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, &MainWindow::ViewProcStatic, id);
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK MainWindow::TimelinesListProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                                     UINT_PTR id, DWORD_PTR ref) {
    auto* self = reinterpret_cast<MainWindow*>(ref);
    if (msg == WM_KEYDOWN && self && (wp == VK_UP || wp == VK_DOWN) &&
        (GetKeyState(VK_SHIFT) & 0x8000)) {
        // Shift+Up/Down reorders the open timelines (Ctrl+arrow is movement-unit
        // navigation). Swallow the key so the ListView doesn't also move
        // focus/selection -- the core re-emits the list with the moved timeline
        // reselected.
        self->dispatch_cmd({{"cmd", "reorder_timeline"}, {"dir", wp == VK_UP ? "up" : "down"}});
        return 0;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, &MainWindow::TimelinesListProcStatic, id);
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT MainWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        create_children();
        return 0;

    case WM_SIZE:
        layout();
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = kMinWidth;
        mmi->ptMinTrackSize.y = kMinHeight;
        return 0;
    }

    case WM_APP_EVENT: {
        std::unique_ptr<std::string> s(reinterpret_cast<std::string*>(lp));
        if (s)
            on_event(*s);
        return 0;
    }

    case WM_APP_INV_ACTION: { // keyhook fired a bound action (or a layer enter/exit)
        std::unique_ptr<std::string> action(reinterpret_cast<std::string*>(lp));
        if (!action)
            return 0;
        if (*action == KeyhookDriver::kLayerEnter) {
            dispatch_cmd({{"cmd", "play_earcon"}, {"name", "navigate"}});
            announce(layer_enter_message_);
            return 0;
        }
        if (*action == KeyhookDriver::kLayerHelp) {
            leave_layer(); // a window is opening; drop out of the layer
            show_layer_help(hwnd_, inst_, layer_help_message_);
            return 0;
        }
        if (*action == KeyhookDriver::kLayerExit) {
            dispatch_cmd({{"cmd", "play_earcon"}, {"name", "close"}});
            if (overlay_layer_) { // an on-demand overlay closed: restore the base driver
                overlay_layer_ = false;
                install_active_driver();
            }
            return 0;
        }
        fastsm::log::write("invisible: perform action '" + *action + "'");
        dispatch_cmd({{"cmd", "perform_action"}, {"action", *action}});
        // The keyhook swallowed the combo's non-modifier key, so the foreground
        // app saw Alt/Win pressed with no real key -> it drops into menu / Start
        // mode ("stuck Alt"). Inject an inert Ctrl tap to mask that, matching
        // AutoHotkey's menu-mask. Injected -> our hook ignores it.
        INPUT mask[2] = {};
        mask[0].type = INPUT_KEYBOARD;
        mask[0].ki.wVk = VK_LCONTROL;
        mask[1].type = INPUT_KEYBOARD;
        mask[1].ki.wVk = VK_LCONTROL;
        mask[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, mask, sizeof(INPUT));
        return 0;
    }

    case WM_SETFOCUS:
        SetFocus(timeline_view_);
        return 0;

    case WM_HOTKEY: {
        // A global invisible-interface hotkey fired: run its core action.
        const std::string action = hotkey_driver_.action_for(static_cast<int>(wp));
        if (!action.empty()) {
            fastsm::log::write("hotkeys: fired -> " + action);
            dispatch_cmd({{"cmd", "perform_action"}, {"action", action}});
        }
        return 0;
    }

    case WM_INITMENUPOPUP:
        // Reflect the focused post's state on the Status menu (Boost / Favorite
        // show a check mark, which screen readers announce, when already done).
        update_menu_checks(reinterpret_cast<HMENU>(wp));
        break;

    case WM_COMMAND:
        if (HIWORD(wp) == 0 || HIWORD(wp) == 1) { // menu or accelerator
            handle_command(LOWORD(wp));
            return 0;
        }
        break;

    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr->hwndFrom == timeline_view_) {
            if (hdr->code == LVN_GETDISPINFOW) {
                auto* di = reinterpret_cast<NMLVDISPINFOW*>(lp);
                Timeline* tc = current();
                if (tc && (di->item.mask & LVIF_TEXT)) {
                    const int idx = di->item.iItem;
                    if (idx >= 0 && idx < static_cast<int>(tc->rows.size())) {
                        scratch_ = tc->rows[static_cast<size_t>(idx)].text;
                        di->item.pszText = scratch_.data();
                        maybe_load_older(idx); // a near-bottom row was rendered
                    }
                }
            } else if (hdr->code == LVN_KEYDOWN) {
                on_view_keydown(reinterpret_cast<NMLVKEYDOWN*>(lp)->wVKey);
            } else if (hdr->code == LVN_ITEMCHANGED || hdr->code == LVN_ODSTATECHANGED) {
                if (!updating_selection_) {
                    Timeline* tc = current();
                    const int focused = ListView_GetNextItem(timeline_view_, -1, LVNI_FOCUSED);
                    if (tc && focused >= 0 && focused < static_cast<int>(tc->rows.size())) {
                        tc->selected_id = tc->rows[static_cast<size_t>(focused)].id;
                        dispatch_cmd({{"cmd", "note_selection"}, {"id", tc->selected_id}});
                        maybe_load_older(focused); // near the bottom -> pull next page
                    }
                }
            }
        } else if (hdr->hwndFrom == timelines_list_) {
            if (hdr->code == LVN_ITEMCHANGED && !updating_selection_) {
                auto* nm = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((nm->uChanged & LVIF_STATE) && (nm->uNewState & LVIS_SELECTED) &&
                    !(nm->uOldState & LVIS_SELECTED))
                    dispatch_cmd({{"cmd", "select_timeline"}, {"index", nm->iItem}});
            }
        }
        return 0;
    }

    case WM_TIMER:
        if (wp == kMediaBgTimer && media_bg_ && media_bg_->completed()) {
            media_bg_->stop(); // background audio finished; clear it
            KillTimer(hwnd_, kMediaBgTimer);
        }
        return 0;

    case WM_DESTROY:
        // Remove the global input hooks while we're still alive and pumping, so a
        // modifier held during close (e.g. Alt+F4) isn't left stuck by the hook
        // being torn down mid-keypress at process exit.
        keyhook_driver_.disable();
        hotkey_driver_.clear();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void MainWindow::create_children() {
    timelines_list_ = make_listview(hwnd_, IDC_TIMELINES_LIST);
    SetWindowSubclass(timelines_list_, &MainWindow::TimelinesListProcStatic, 0,
                      reinterpret_cast<DWORD_PTR>(this));
    timeline_view_ = CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
            LVS_SHOWSELALWAYS | LVS_OWNERDATA,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TIMELINE_VIEW)), inst_,
        nullptr);
    ListView_SetExtendedListViewStyle(timeline_view_,
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SetWindowSubclass(timeline_view_, &MainWindow::ViewProcStatic, 0,
                      reinterpret_cast<DWORD_PTR>(this));

    add_single_column(timelines_list_, L"Timelines", dpi_scale(hwnd_, kTimelinesPaneWidth) - 24);
    add_single_column(timeline_view_, L"Timeline", dpi_scale(hwnd_, 640));
    layout();
}

void MainWindow::layout() {
    if (!timelines_list_ || !timeline_view_)
        return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int pane = dpi_scale(hwnd_, kTimelinesPaneWidth);
    const int h = rc.bottom - rc.top;
    const int total = rc.right - rc.left;
    MoveWindow(timelines_list_, 0, 0, pane, h, TRUE);
    MoveWindow(timeline_view_, pane, 0, total - pane, h, TRUE);
    ListView_SetColumnWidth(timelines_list_, 0, pane - dpi_scale(hwnd_, 24));
    ListView_SetColumnWidth(timeline_view_, 0, total - pane - dpi_scale(hwnd_, 24));
}

MainWindow::Timeline* MainWindow::current() {
    if (current_ < 0 || current_ >= static_cast<int>(timelines_.size()))
        return nullptr;
    return &timelines_[static_cast<size_t>(current_)];
}

int MainWindow::index_of_id(const Timeline& tl, const std::string& id) const {
    if (id.empty())
        return -1;
    for (size_t i = 0; i < tl.rows.size(); ++i)
        if (tl.rows[i].id == id)
            return static_cast<int>(i);
    return -1;
}

void MainWindow::populate_timelines_list() {
    updating_selection_ = true;
    ListView_DeleteAllItems(timelines_list_);
    for (size_t i = 0; i < timelines_.size(); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(timelines_[i].title.c_str());
        ListView_InsertItem(timelines_list_, &item);
    }
    if (current_ >= 0 && current_ < static_cast<int>(timelines_.size()))
        ListView_SetItemState(timelines_list_, current_, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    updating_selection_ = false;
}

void MainWindow::update_pin_menu() {
    // Check the "Pin Timeline" item and flip its label to "Unpin" when the current
    // tab is pinned, so the toggle state is announced by the screen reader.
    HMENU bar = GetMenu(hwnd_);
    if (!bar)
        return;
    const Timeline* tl = current();
    const bool pinned = tl && tl->pinned;
    MENUITEMINFOW mii{};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STATE | MIIM_STRING;
    mii.fState = pinned ? MFS_CHECKED : MFS_UNCHECKED;
    std::wstring label = pinned ? L"Un&pin Timeline\tCtrl+P" : L"&Pin Timeline\tCtrl+P";
    mii.dwTypeData = label.data();
    SetMenuItemInfoW(bar, ID_TOGGLE_PIN, FALSE, &mii);
}

void MainWindow::maybe_load_older(int row) {
    if (load_pending_)
        return;
    Timeline* tc = current();
    if (!tc)
        return;
    const int count = static_cast<int>(tc->rows.size());
    // A tracked middle gap within a few rows -> fill it transparently. In reversed
    // mode older posts sit above, so scan both directions around the cursor.
    for (int d = 0; d <= 5; ++d) {
        for (int g : {row + d, row - d}) {
            if (g < 0 || g >= count)
                continue;
            if (tc->rows[static_cast<size_t>(g)].gap_after) {
                load_pending_ = true;
                dispatch_cmd({{"cmd", "load_gap"}, {"id", tc->rows[static_cast<size_t>(g)].id}});
                return;
            }
        }
    }
    // Older posts are at the bottom normally, but at the top when reversed.
    const bool near_edge = tc->reversed ? (row <= 9) : (row >= count - 10);
    if (count > 0 && near_edge && settings_.value("auto_load_older", true)) {
        load_pending_ = true;
        dispatch_cmd({{"cmd", "load_older"}});
    }
}

void MainWindow::do_load_older() {
    if (load_pending_)
        return;
    Timeline* tc = current();
    if (!tc || tc->rows.empty())
        return;
    load_pending_ = true;
    dispatch_cmd({{"cmd", "load_older"}});
}

void MainWindow::bind_current_to_view(bool force) {
    Timeline* tc = current();
    // User lists allow multi-select (for batch follow/mute/block); post timelines
    // stay single-select. Flip the list style only when it needs to change.
    const bool want_multi = tc && tc->user_list;
    const LONG_PTR style = GetWindowLongPtrW(timeline_view_, GWL_STYLE);
    if (want_multi == static_cast<bool>(style & LVS_SINGLESEL))
        SetWindowLongPtrW(timeline_view_, GWL_STYLE,
                          want_multi ? (style & ~LVS_SINGLESEL) : (style | LVS_SINGLESEL));
    std::vector<std::string> ids;
    if (tc) {
        ids.reserve(tc->rows.size());
        for (const auto& r : tc->rows)
            ids.push_back(r.id);
    }
    // The guard avoids disturbing selection on no-op data updates, but a timeline
    // *switch* must always rebind — otherwise switching to an empty timeline (ids
    // == the cleared rendered_ids_) would leave the previous timeline's row count
    // in place, showing blank rows.
    if (!force && ids == rendered_ids_)
        return;
    rendered_ids_ = ids;

    const int count = static_cast<int>(ids.size());
    updating_selection_ = true;
    ListView_SetItemCountEx(timeline_view_, count, LVSICF_NOSCROLL);
    if (tc && count > 0) {
        int idx = index_of_id(*tc, tc->selected_id);
        if (idx < 0) {
            // No remembered row: land on the newest post — the top normally, or the
            // bottom when the timeline is reversed (newest-at-bottom).
            idx = tc->reversed ? count - 1 : 0;
            tc->selected_id = tc->rows[static_cast<size_t>(idx)].id;
        }
        const UINT want = LVIS_SELECTED | LVIS_FOCUSED;
        if ((ListView_GetItemState(timeline_view_, idx, want) & want) != want)
            ListView_SetItemState(timeline_view_, idx, want, want);
        ListView_EnsureVisible(timeline_view_, idx, FALSE);
    }
    updating_selection_ = false;
    InvalidateRect(timeline_view_, nullptr, FALSE);
}

int MainWindow::selected_row() const {
    return ListView_GetNextItem(timeline_view_, -1, LVNI_FOCUSED);
}

std::string MainWindow::selected_id() {
    Timeline* tc = current();
    const int row = selected_row();
    if (!tc || row < 0 || row >= static_cast<int>(tc->rows.size()))
        return {};
    return tc->rows[static_cast<size_t>(row)].id;
}

void MainWindow::restore_selection(const std::string& id) {
    Timeline* tc = current();
    if (!tc || id.empty())
        return;
    const int idx = index_of_id(*tc, id);
    if (idx < 0)
        return;
    tc->selected_id = id;
    const UINT want = LVIS_SELECTED | LVIS_FOCUSED;
    updating_selection_ = true;
    ListView_SetItemState(timeline_view_, idx, want, want);
    ListView_EnsureVisible(timeline_view_, idx, FALSE);
    updating_selection_ = false;
}

void MainWindow::cycle_focus() {
    HWND focus = GetFocus();
    SetFocus(focus == timeline_view_ ? timelines_list_ : timeline_view_);
}

void MainWindow::dispatch_cmd(const json& cmd) {
    if (!core_)
        return;
    const std::string s = cmd.dump();
    fastsm_core_dispatch(core_, s.c_str(), s.size());
}

void MainWindow::on_view_keydown(int vk) {
    // Shift+letter: first-letter navigation to the next post whose spoken text
    // starts with that letter. Plain letters stay post actions (below), so this
    // only claims the Shift chord (no Ctrl/Alt).
    if (vk >= 'A' && vk <= 'Z') {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (shift && !ctrl && !alt) {
            first_letter_nav(static_cast<wchar_t>(vk));
            return;
        }
    }
    switch (vk) {
    case VK_UP:
    case VK_DOWN: {
        Timeline* tc = current();
        const int count = tc ? static_cast<int>(tc->rows.size()) : 0;
        const int row = selected_row();
        const bool at_edge = (vk == VK_UP && row <= 0) || (vk == VK_DOWN && row >= count - 1);
        if (at_edge && count > 0 && settings_.value("boundary_sound", true))
            dispatch_cmd({{"cmd", "play_earcon"}, {"name", "boundary"}});
        break;
    }
    case VK_LEFT:
        dispatch_cmd({{"cmd", "select_timeline"}, {"dir", "prev"}});
        break;
    case VK_RIGHT:
        dispatch_cmd({{"cmd", "select_timeline"}, {"dir", "next"}});
        break;
    case 'B':
        do_boost();
        break;
    case 'F':
        do_favorite();
        break;
    case 'P': { // pin/unpin your own post to your profile (core rejects others' posts)
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "toggle_pin_post"}, {"id", id}});
        break;
    }
    case 'R':
        compose("reply");
        break;
    case 'Q':
        compose("quote");
        break;
    case 'E':
        compose("edit");
        break;
    case VK_SPACE: { // open the post's thread (Mac parity)
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_thread"}, {"id", id}});
        break;
    }
    case 'U': { // open the author's posts (Ctrl+U is the accelerator for profile)
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_user_timeline"}, {"id", id}, {"pick", true}});
        break;
    }
    case 'H': // follow a hashtag; prompt pre-fills with this post's hashtags
        dispatch_cmd({{"cmd", "follow_hashtag_prompt"}, {"id", selected_id()}});
        break;
    case VK_DELETE: // delete your own post (only offered on your own rows)
        do_delete_post();
        break;
    case VK_OEM_PERIOD: // manually load older posts
        do_load_older();
        break;
    case VK_RETURN: {
        Timeline* t = current();
        const int fr = selected_row();
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool follow_req = t && fr >= 0 && fr < static_cast<int>(t->rows.size()) &&
                                t->rows[static_cast<size_t>(fr)].follow_request;
        if (shift && t && !t->user_list && !follow_req) {
            do_secondary_post_action(); // Shift+Enter: the secondary interact (Behavior tab)
        } else if (follow_req) {
            do_follow_request_action(t->rows[static_cast<size_t>(fr)]); // accept/reject
        } else if (t && t->user_list) {
            do_enter_user_action(); // configurable (Behavior tab)
        } else {
            do_enter_post_action(); // configurable (Behavior tab)
        }
        break;
    }
    default:
        break;
    }
}

void MainWindow::do_enter_post_action() {
    const std::string a = settings_.value("enter_post_action", std::string("post_info"));
    const std::string id = selected_id();
    if (a == "thread") {
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_thread"}, {"id", id}});
    } else if (a == "reply") {
        compose("reply");
    } else if (a == "links") {
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_post_links"}, {"id", id}});
    } else {
        do_post_info();
    }
}

void MainWindow::do_secondary_post_action() {
    const std::string a = settings_.value("secondary_post_action", std::string("play_media"));
    const std::string id = selected_id();
    if (id.empty())
        return;
    if (a == "post_info")
        do_post_info();
    else if (a == "thread")
        dispatch_cmd({{"cmd", "open_thread"}, {"id", id}});
    else if (a == "reply")
        compose("reply");
    else if (a == "links")
        dispatch_cmd({{"cmd", "open_post_links"}, {"id", id}});
    else
        dispatch_cmd({{"cmd", "play_media"}, {"id", id}});
}

void MainWindow::do_enter_user_action() {
    const std::string a = settings_.value("enter_user_action", std::string("actions"));
    const std::string id = selected_id();
    if (a == "profile") {
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_user_profile"}, {"id", id}});
    } else if (a == "timeline") {
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_user_timeline"}, {"id", id}});
    } else {
        show_user_actions();
    }
}

void MainWindow::do_follow_request_action(const Row& r) {
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    AppendMenuW(menu, MF_STRING, 1, L"&Accept");
    AppendMenuW(menu, MF_STRING, 2, L"&Reject");
    POINT pt{0, 0};
    RECT rc;
    const int frow = selected_row();
    if (frow >= 0 && ListView_GetItemRect(timeline_view_, frow, &rc, LVIR_BOUNDS)) {
        pt.x = rc.left;
        pt.y = rc.bottom;
        ClientToScreen(timeline_view_, &pt);
    } else {
        GetCursorPos(&pt);
    }
    const int chosen = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd_,
        nullptr));
    DestroyMenu(menu);
    if (chosen != 1 && chosen != 2)
        return;
    const char* action = chosen == 1 ? "authorize_request" : "reject_request";
    dispatch_cmd({{"cmd", "set_relationship"},
                  {"account_id", r.account_id},
                  {"acct", r.acct},
                  {"action", action}});
}

void MainWindow::update_menu_checks(HMENU menu) {
    // No-op unless this popup is the Status menu (which owns these items).
    if (GetMenuState(menu, ID_BOOST, MF_BYCOMMAND) == static_cast<UINT>(-1))
        return;
    bool boosted = false, favorited = false;
    Timeline* tc = current();
    const int row = selected_row();
    if (tc && row >= 0 && row < static_cast<int>(tc->rows.size())) {
        boosted = tc->rows[static_cast<size_t>(row)].boosted;
        favorited = tc->rows[static_cast<size_t>(row)].favorited;
    }
    CheckMenuItem(menu, ID_BOOST, MF_BYCOMMAND | (boosted ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_FAVORITE, MF_BYCOMMAND | (favorited ? MF_CHECKED : MF_UNCHECKED));
}

void MainWindow::do_boost() {
    Timeline* tc = current();
    const int row = selected_row();
    if (!tc || row < 0 || row >= static_cast<int>(tc->rows.size()))
        return;
    const Row& r = tc->rows[static_cast<size_t>(row)];
    const bool boosting = !r.boosted;
    if (boosting && settings_.value("confirm_boost", false) &&
        !confirm(hwnd_, L"Boost this post?", L"Boost"))
        return;
    if (!boosting && settings_.value("confirm_unboost", false) &&
        !confirm(hwnd_, L"Unboost this post?", L"Unboost"))
        return;
    dispatch_cmd({{"cmd", "toggle_boost"}, {"id", r.id}});
}

void MainWindow::do_favorite() {
    Timeline* tc = current();
    const int row = selected_row();
    if (!tc || row < 0 || row >= static_cast<int>(tc->rows.size()))
        return;
    const Row& r = tc->rows[static_cast<size_t>(row)];
    const bool favoriting = !r.favorited;
    if (favoriting && settings_.value("confirm_favorite", false) &&
        !confirm(hwnd_, L"Favorite this post?", L"Favorite"))
        return;
    if (!favoriting && settings_.value("confirm_unfavorite", false) &&
        !confirm(hwnd_, L"Unfavorite this post?", L"Unfavorite"))
        return;
    dispatch_cmd({{"cmd", "toggle_favorite"}, {"id", r.id}});
}

void MainWindow::do_delete_post() {
    Timeline* tc = current();
    const int row = selected_row();
    if (!tc || row < 0 || row >= static_cast<int>(tc->rows.size()))
        return;
    const Row& r = tc->rows[static_cast<size_t>(row)];
    if (!r.is_mine) // Delete only acts on your own posts
        return;
    if (!settings_.value("confirm_delete_post", true) ||
        confirm(hwnd_, L"Delete this post? This can't be undone.", L"Delete Post"))
        dispatch_cmd({{"cmd", "delete_post"}, {"id", r.id}});
}

void MainWindow::compose(const char* mode) {
    json cmd = {{"cmd", "compose_context"}, {"mode", mode}};
    const std::string id = selected_id();
    if (!id.empty())
        cmd["id"] = id;
    dispatch_cmd(cmd);
}

void MainWindow::do_post_info() {
    const std::string id = selected_id();
    if (!id.empty())
        dispatch_cmd({{"cmd", "post_info"}, {"id", id}});
}

void MainWindow::ev_post_info(const json& e) {
    leave_layer(); // a modal dialog is opening; leave the layer (restores an overlay)
    const std::string id = e.value("id", std::string{});
    const std::wstring text = to_wide(e.value("text", std::string{}));
    const bool quote_ok = e.contains("features") && e["features"].value("quote_posts", false);
    const bool browser_ok = e.value("has_url", false);
    const bool is_mine = e.value("is_mine", false);
    PollInfo poll;
    if (e.contains("poll") && e["poll"].is_object()) {
        poll.present = true;
        poll.multiple = e["poll"].value("multiple", false);
        for (const auto& o : e["poll"].value("options", json::array()))
            poll.options.push_back(to_wide(o.get<std::string>()));
    }
    const std::string keep_id = selected_id();
    auto guard = enter_modal();
    PostInfoResult res =
        show_post_info_dialog(hwnd_, inst_, text, quote_ok, browser_ok, is_mine, poll);
    restore_selection(keep_id);
    leave_modal(guard);
    if (!res.action)
        return;
    if (*res.action == PostInfoAction::Vote) {
        json choices = json::array();
        for (int c : res.choices)
            choices.push_back(c);
        dispatch_cmd({{"cmd", "vote_poll"}, {"id", id}, {"choices", std::move(choices)}});
        return;
    }
    switch (*res.action) {
    case PostInfoAction::Reply:
        dispatch_cmd({{"cmd", "compose_context"}, {"mode", "reply"}, {"id", id}});
        break;
    case PostInfoAction::Boost:
        dispatch_cmd({{"cmd", "toggle_boost"}, {"id", id}});
        break;
    case PostInfoAction::Favorite:
        dispatch_cmd({{"cmd", "toggle_favorite"}, {"id", id}});
        break;
    case PostInfoAction::Quote:
        dispatch_cmd({{"cmd", "compose_context"}, {"mode", "quote"}, {"id", id}});
        break;
    case PostInfoAction::OpenBrowser:
        dispatch_cmd({{"cmd", "open_status"}, {"id", id}});
        break;
    case PostInfoAction::OpenLinks:
        dispatch_cmd({{"cmd", "open_post_links"}, {"id", id}});
        break;
    case PostInfoAction::ViewThread:
        dispatch_cmd({{"cmd", "open_thread"}, {"id", id}});
        break;
    case PostInfoAction::ViewAuthor:
        dispatch_cmd({{"cmd", "open_user_timeline"}, {"id", id}});
        break;
    case PostInfoAction::Delete:
        if (!settings_.value("confirm_delete_post", true) ||
            confirm(hwnd_, L"Delete this post? This can't be undone.", L"Delete Post"))
            dispatch_cmd({{"cmd", "delete_post"}, {"id", id}});
        break;
    case PostInfoAction::Vote:
        break; // handled above
    }
}

void MainWindow::ev_user_profile(const json& e) {
    leave_layer(); // a modal dialog is opening; leave the layer (restores an overlay)
    const std::wstring text = to_wide(e.value("text", std::string{}));
    const std::string account_id = e.value("account_id", std::string{});
    const std::string acct = e.value("acct", std::string{});
    const std::string url = e.value("url", std::string{});
    UserProfileRelationship rel;
    rel.known = e.value("has_relationship", false);
    rel.following = e.value("following", false);
    rel.muting = e.value("muting", false);
    rel.blocking = e.value("blocking", false);
    rel.requested = e.value("requested", false);
    rel.showing_reblogs = e.value("showing_reblogs", true);
    rel.can_hide_boosts = e.value("can_hide_boosts", false);
    rel.can_use_lists = e.value("can_use_lists", false);
    const std::string keep_id = selected_id();
    auto guard = enter_modal();
    auto action = show_user_profile_dialog(hwnd_, inst_, text, rel);
    restore_selection(keep_id);
    leave_modal(guard);
    if (!action)
        return;
    auto set_rel = [&](const char* a) {
        dispatch_cmd(
            {{"cmd", "set_relationship"}, {"account_id", account_id}, {"acct", acct}, {"action", a}});
    };
    switch (*action) {
    case UserProfileAction::ViewPosts:
        dispatch_cmd({{"cmd", "open_user_timeline"}, {"account_id", account_id}, {"acct", acct}});
        break;
    case UserProfileAction::Followers:
        dispatch_cmd({{"cmd", "open_followers"}, {"account_id", account_id}, {"acct", acct}});
        break;
    case UserProfileAction::Following:
        dispatch_cmd({{"cmd", "open_following"}, {"account_id", account_id}, {"acct", acct}});
        break;
    case UserProfileAction::OpenBrowser:
        if (!url.empty())
            ShellExecuteW(nullptr, L"open", to_wide(url).c_str(), nullptr, nullptr, SW_SHOW);
        break;
    case UserProfileAction::ToggleFollow:
        set_rel((rel.following || rel.requested) ? "unfollow" : "follow");
        break;
    case UserProfileAction::ToggleMute:
        set_rel(rel.muting ? "unmute" : "mute");
        break;
    case UserProfileAction::ToggleBlock:
        if (rel.blocking) {
            if (!settings_.value("confirm_unblock", false) ||
                confirm(hwnd_, (L"Unblock @" + to_wide(acct) + L"?").c_str(), L"Unblock"))
                set_rel("unblock");
        } else if (!settings_.value("confirm_block", true) ||
                   confirm(hwnd_, (L"Block @" + to_wide(acct) + L"?").c_str(), L"Block")) {
            set_rel("block");
        }
        break;
    case UserProfileAction::ToggleBoosts:
        set_rel(rel.showing_reblogs ? "hide_boosts" : "show_boosts");
        break;
    case UserProfileAction::Lists:
        // Fetch the account's lists + this user's membership; ev_user_lists opens
        // the checklist when they arrive.
        dispatch_cmd({{"cmd", "get_user_lists"}, {"account_id", account_id}, {"acct", acct}});
        break;
    }
}

void MainWindow::ev_user_lists(const json& e) {
    if (!e.value("supported", false))
        return;
    const std::string account_id = e.value("account_id", std::string{});
    const std::string acct = e.value("acct", std::string{});
    std::vector<fastsmui::ListMembershipItem> items;
    for (const auto& l : e.value("lists", json::array()))
        items.push_back({l.value("id", std::string{}), to_wide(l.value("title", std::string{})),
                         l.value("member", false)});
    if (items.empty()) {
        announce("You have no lists. Create one from Application, Lists.");
        return;
    }
    const std::vector<fastsmui::ListMembershipItem> before = items; // to diff after editing
    const std::wstring heading = L"Lists for @" + to_wide(acct) + L" (check to add):";
    auto guard = enter_modal();
    auto result = show_list_membership_dialog(hwnd_, inst_, heading, items);
    leave_modal(guard);
    if (!result)
        return;
    // Apply only the changes.
    for (size_t i = 0; i < result->size(); ++i) {
        if ((*result)[i].member == before[i].member)
            continue;
        dispatch_cmd({{"cmd", "set_user_list"},
                      {"list_id", (*result)[i].id},
                      {"account_id", account_id},
                      {"add", (*result)[i].member}});
    }
}

void MainWindow::ev_user_picker(const json& e) {
    if (!e.contains("users") || !e["users"].is_array() || e["users"].empty())
        return;
    leave_layer(); // a modal dialog is opening; leave the layer (restores an overlay)
    const std::string purpose = e.value("purpose", std::string{});
    const std::string row_id = e.value("id", std::string{});
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    std::vector<std::pair<std::string, std::string>> list; // (account_id, acct)
    UINT cmd_id = 1;
    for (const auto& u : e["users"]) {
        const std::string acct = u.value("acct", std::string{});
        AppendMenuW(menu, MF_STRING, cmd_id++, to_wide("@" + acct).c_str());
        list.push_back({u.value("id", std::string{}), acct});
    }
    // Always offer a manual entry so you can act on someone by handle even if
    // they aren't in this post.
    const UINT manual_id = cmd_id;
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, manual_id, L"&Type a handle…");
    // Pop up at the selected row (matches the Mac's popUpAtSelectedRow).
    POINT pt{0, 0};
    const int row = selected_row();
    RECT rc;
    if (row >= 0 && ListView_GetItemRect(timeline_view_, row, &rc, LVIR_BOUNDS)) {
        pt.x = rc.left;
        pt.y = rc.bottom;
        ClientToScreen(timeline_view_, &pt);
    } else {
        GetCursorPos(&pt);
    }
    const int chosen = track_popup(menu, pt);
    DestroyMenu(menu);
    if (chosen <= 0)
        return;
    // "Type a handle…" chosen: prompt for a handle and act on that instead.
    if (chosen == static_cast<int>(manual_id)) {
        const std::string handle = prompt_handle();
        if (handle.empty())
            return;
        if (purpose == "timeline")
            dispatch_cmd({{"cmd", "open_user_timeline"}, {"handle", handle}});
        else if (purpose == "follow_toggle")
            dispatch_cmd({{"cmd", "follow_toggle"}, {"handle", handle}});
        else
            dispatch_cmd({{"cmd", "open_user_profile"}, {"handle", handle}});
        return;
    }
    if (chosen > static_cast<int>(list.size()))
        return;
    const auto& [account_id, acct] = list[static_cast<size_t>(chosen - 1)];
    if (purpose == "timeline")
        dispatch_cmd({{"cmd", "open_user_timeline"}, {"account_id", account_id}, {"acct", acct}});
    else if (purpose == "follow_toggle")
        dispatch_cmd({{"cmd", "follow_toggle"}, {"account_id", account_id}, {"acct", acct}});
    else
        dispatch_cmd({{"cmd", "open_user_profile"}, {"id", row_id}, {"account_id", account_id}});
}

void MainWindow::ev_url_picker(const json& e) {
    if (!e.contains("links") || !e["links"].is_array() || e["links"].empty())
        return;
    leave_layer(); // a menu is opening; leave the layer (restores an overlay)
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    std::vector<std::wstring> urls; // parallels the menu items
    UINT cmd_id = 1;
    for (const auto& l : e["links"]) {
        const std::string title = l.value("title", std::string{});
        const std::string url = l.value("url", std::string{});
        // Show the title with the actual URL in parentheses (unless they're equal).
        std::string label = (title.empty() || title == url) ? url : title + " (" + url + ")";
        AppendMenuW(menu, MF_STRING, cmd_id++, to_wide(label).c_str());
        urls.push_back(to_wide(url));
    }
    POINT pt{0, 0};
    const int row = selected_row();
    RECT rc;
    if (row >= 0 && ListView_GetItemRect(timeline_view_, row, &rc, LVIR_BOUNDS)) {
        pt.x = rc.left;
        pt.y = rc.bottom;
        ClientToScreen(timeline_view_, &pt);
    } else {
        GetCursorPos(&pt);
    }
    const int chosen = track_popup(menu, pt);
    DestroyMenu(menu);
    if (chosen <= 0 || chosen > static_cast<int>(urls.size()))
        return;
    ShellExecuteW(nullptr, L"open", urls[static_cast<size_t>(chosen - 1)].c_str(), nullptr, nullptr,
                  SW_SHOW);
}

void MainWindow::show_user_actions() {
    Timeline* tc = current();
    if (!tc)
        return;
    // Collect the selected user rows (fall back to the focused row).
    std::vector<std::string> ids;
    int row = -1;
    while ((row = ListView_GetNextItem(timeline_view_, row, LVNI_SELECTED)) >= 0)
        if (row < static_cast<int>(tc->rows.size()))
            ids.push_back(tc->rows[static_cast<size_t>(row)].id);
    if (ids.empty()) {
        const int f = selected_row();
        if (f >= 0 && f < static_cast<int>(tc->rows.size()))
            ids.push_back(tc->rows[static_cast<size_t>(f)].id);
    }
    if (ids.empty())
        return;
    const int count = static_cast<int>(ids.size());
    struct Act {
        const wchar_t* label;
        const char* action;
    };
    static const Act kUserActs[] = {
        {L"&Follow", "follow"}, {L"&Unfollow", "unfollow"}, {L"&Mute", "mute"},
        {L"Un&mute", "unmute"}, {L"&Block", "block"},       {L"Un&block", "unblock"},
    };
    // The Follow Requests buffer offers Accept/Reject instead.
    static const Act kReqActs[] = {
        {L"&Accept", "authorize_request"},
        {L"&Reject", "reject_request"},
    };
    const bool requests = tc->kind == "followRequests";
    const Act* kActs = requests ? kReqActs : kUserActs;
    const int kActCount =
        requests ? static_cast<int>(sizeof(kReqActs) / sizeof(kReqActs[0]))
                 : static_cast<int>(sizeof(kUserActs) / sizeof(kUserActs[0]));
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    for (int i = 0; i < kActCount; ++i) {
        std::wstring label = kActs[i].label;
        if (count > 1)
            label += L" (" + std::to_wstring(count) + L")";
        AppendMenuW(menu, MF_STRING, static_cast<UINT>(i + 1), label.c_str());
    }
    POINT pt{0, 0};
    const int frow = selected_row();
    RECT rc;
    if (frow >= 0 && ListView_GetItemRect(timeline_view_, frow, &rc, LVIR_BOUNDS)) {
        pt.x = rc.left;
        pt.y = rc.bottom;
        ClientToScreen(timeline_view_, &pt);
    } else {
        GetCursorPos(&pt);
    }
    const int chosen = static_cast<int>(TrackPopupMenu(menu,
                                                       TPM_RETURNCMD | TPM_NONOTIFY |
                                                           TPM_LEFTALIGN | TPM_TOPALIGN,
                                                       pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (chosen <= 0 || chosen > kActCount)
        return;
    const char* action = kActs[chosen - 1].action;
    const std::string act(action);
    if (act == "block" && settings_.value("confirm_block", true)) {
        const std::wstring msg =
            count > 1 ? L"Block " + std::to_wstring(count) + L" users?" : L"Block this user?";
        if (!confirm(hwnd_, msg.c_str(), L"Block"))
            return;
    } else if (act == "unblock" && settings_.value("confirm_unblock", false)) {
        const std::wstring msg =
            count > 1 ? L"Unblock " + std::to_wstring(count) + L" users?" : L"Unblock this user?";
        if (!confirm(hwnd_, msg.c_str(), L"Unblock"))
            return;
    }
    json jids = json::array();
    for (const auto& s : ids)
        jids.push_back(s);
    dispatch_cmd({{"cmd", "user_action"}, {"action", action}, {"ids", std::move(jids)}});
}

void MainWindow::do_new_timeline() { dispatch_cmd({{"cmd", "get_spawnable"}}); }

void MainWindow::do_add_account() {
    auto data = show_add_account_dialog(hwnd_, inst_);
    if (!data)
        return;
    json cmd = {{"cmd", "add_account"}};
    if (data->platform == 0) {
        cmd["platform"] = "mastodon";
        cmd["instance"] = data->service;
    } else {
        cmd["platform"] = "bluesky";
        cmd["service"] = data->service;
        cmd["handle"] = data->handle;
        cmd["app_password"] = data->app_password;
    }
    dispatch_cmd(cmd);
}

void MainWindow::do_settings() {
    store::AppSettings s = store::settings_from_json(settings_);
    std::vector<std::string> packs = soundpacks_.empty()
                                         ? std::vector<std::string>{"Default"}
                                         : soundpacks_;
    auto open_mgr = [this](HWND parent) { open_keymap_manager(parent); };
    if (auto result = show_settings_dialog(hwnd_, inst_, s, packs, open_mgr))
        dispatch_cmd({{"cmd", "update_settings"}, {"settings", store::settings_to_json(*result)}});
}

void MainWindow::ev_account_settings(const json& e) {
    std::vector<std::string> packs;
    for (const auto& p : e.value("soundpacks", json::array()))
        packs.push_back(p.get<std::string>());
    if (packs.empty())
        packs.push_back("Default");
    const std::string acct = e.value("acct", std::string{});
    const std::string current = e.value("soundpack", std::string("Default"));
    std::wstring title = acct.empty() ? L"Account Settings"
                                      : L"Account Settings for @" + to_wide(acct);
    auto guard = enter_modal();
    auto chosen = show_account_settings_dialog(hwnd_, inst_, title, packs, current);
    leave_modal(guard);
    if (chosen)
        dispatch_cmd({{"cmd", "set_account_settings"}, {"soundpack", *chosen}});
}

namespace {
struct FindCtx {
    std::wstring text;
    bool ok = false;
};
INT_PTR CALLBACK find_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        auto* ctx = reinterpret_cast<FindCtx*>(lp);
        SetDlgItemTextW(dlg, IDC_FIND_TEXT, ctx->text.c_str());
        SendDlgItemMessageW(dlg, IDC_FIND_TEXT, EM_SETSEL, 0, -1); // select all to retype
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDOK) {
            auto* ctx = reinterpret_cast<FindCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
            wchar_t buf[256];
            GetDlgItemTextW(dlg, IDC_FIND_TEXT, buf, 256);
            ctx->text = buf;
            ctx->ok = true;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}
std::wstring lowered(std::wstring s) {
    for (wchar_t& c : s)
        c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

// The first alphanumeric character of a row's spoken label, lowercased (0 if
// none). Leading punctuation/whitespace (e.g. a "@" before a handle) is skipped
// so first-letter nav matches the first thing a listener actually hears.
wchar_t first_letter_of(const std::wstring& s) {
    for (wchar_t c : s)
        if (std::iswalnum(c))
            return static_cast<wchar_t>(std::towlower(c));
    return 0;
}

// "Type a handle…" prompt: a single edit box that returns the typed handle
// (leading '@' and surrounding spaces trimmed), or nullopt if cancelled/empty.
struct HandleCtx {
    std::optional<std::string> result;
};
INT_PTR CALLBACK handle_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        SetFocus(GetDlgItem(dlg, IDC_EH_EDIT));
        return FALSE; // focus set explicitly
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDOK) {
            auto* ctx = reinterpret_cast<HandleCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
            wchar_t buf[256] = {0};
            GetDlgItemTextW(dlg, IDC_EH_EDIT, buf, 256);
            std::wstring h = buf;
            const size_t b = h.find_first_not_of(L" \t@");
            const size_t e = h.find_last_not_of(L" \t");
            h = (b == std::wstring::npos) ? std::wstring{} : h.substr(b, e - b + 1);
            if (!h.empty() && ctx)
                ctx->result = to_utf8(h);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

// @-mention autocomplete picker. Live-searches accounts as the user types; the
// core's replies fill the list via MainWindow::ev_user_suggestions. Enter/Insert
// (or a double-click) returns the highlighted handle to drop into the post.
struct MentionCtx {
    std::function<void(const nlohmann::json&)> dispatch;
    HWND* active = nullptr;           // &MainWindow::mention_dlg_, tracked while open
    std::wstring query;               // seed: the partial already typed in the composer
    std::vector<std::string> handles; // acct per list row (parallel to the listbox)
    std::string result;               // chosen handle, empty if cancelled
    bool ok = false;
    bool initializing = false;        // suppress the seed's EN_CHANGE
};

void mention_search(HWND dlg, MentionCtx* ctx) {
    wchar_t buf[256] = {0};
    GetDlgItemTextW(dlg, IDC_MENTION_QUERY, buf, 256);
    ctx->dispatch({{"cmd", "autocomplete_users"}, {"query", to_utf8(std::wstring(buf))}});
}

INT_PTR CALLBACK mention_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        auto* ctx = reinterpret_cast<MentionCtx*>(lp);
        if (ctx->active)
            *ctx->active = dlg;
        ctx->initializing = true;
        SetDlgItemTextW(dlg, IDC_MENTION_QUERY, ctx->query.c_str());
        SendDlgItemMessageW(dlg, IDC_MENTION_QUERY, EM_SETSEL, 0, -1);
        ctx->initializing = false;
        // Land on the results list (matches are already coming for the seeded
        // handle) so a screen reader arrows straight through them; with nothing
        // seeded there's nothing to land on, so start in the search box instead.
        SetFocus(GetDlgItem(dlg, ctx->query.empty() ? IDC_MENTION_QUERY : IDC_MENTION_LIST));
        mention_search(dlg, ctx); // seed the initial matches
        return FALSE;             // focus set explicitly
    }
    if (msg == WM_COMMAND) {
        auto* ctx = reinterpret_cast<MentionCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
        const int id = LOWORD(wp);
        if (id == IDC_MENTION_QUERY && HIWORD(wp) == EN_CHANGE) {
            if (ctx && !ctx->initializing)
                mention_search(dlg, ctx);
            return TRUE;
        }
        if (id == IDC_MENTION_LIST && HIWORD(wp) == LBN_DBLCLK) {
            SendMessageW(dlg, WM_COMMAND, IDOK, 0); // double-click inserts
            return TRUE;
        }
        if (id == IDOK) {
            if (ctx) {
                int sel = static_cast<int>(
                    SendDlgItemMessageW(dlg, IDC_MENTION_LIST, LB_GETCURSEL, 0, 0));
                if (sel < 0 && !ctx->handles.empty())
                    sel = 0; // no explicit pick -> take the top match
                if (sel >= 0 && sel < static_cast<int>(ctx->handles.size())) {
                    ctx->result = ctx->handles[static_cast<size_t>(sel)];
                    ctx->ok = true;
                }
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}
} // namespace

void MainWindow::ev_user_suggestions(const json& e) {
    if (!mention_dlg_)
        return;
    auto* ctx = reinterpret_cast<MentionCtx*>(GetWindowLongPtrW(mention_dlg_, DWLP_USER));
    if (!ctx)
        return;
    // Drop a stale reply: apply only if it still matches what's typed right now.
    wchar_t buf[256] = {0};
    GetDlgItemTextW(mention_dlg_, IDC_MENTION_QUERY, buf, 256);
    if (to_utf8(std::wstring(buf)) != e.value("query", std::string{}))
        return;
    HWND lb = GetDlgItem(mention_dlg_, IDC_MENTION_LIST);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    ctx->handles.clear();
    for (const auto& u : e.value("users", json::array())) {
        const std::string handle = u.value("acct", std::string{});
        if (handle.empty())
            continue;
        std::string label = u.value("label", std::string{});
        if (label.empty())
            label = "@" + handle;
        SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(to_wide(label).c_str()));
        ctx->handles.push_back(handle);
    }
    if (!ctx->handles.empty())
        SendMessageW(lb, LB_SETCURSEL, 0, 0); // highlight the top match
}

std::optional<std::string> MainWindow::pick_mention(HWND owner, const std::string& partial) {
    MentionCtx ctx;
    ctx.dispatch = [this](const json& c) { dispatch_cmd(c); };
    ctx.active = &mention_dlg_;
    ctx.query = to_wide(partial);
    const INT_PTR r = DialogBoxParamW(inst_, MAKEINTRESOURCEW(IDD_MENTION), owner, &mention_proc,
                                      reinterpret_cast<LPARAM>(&ctx));
    mention_dlg_ = nullptr;
    if (r == IDOK && ctx.ok && !ctx.result.empty())
        return ctx.result;
    return std::nullopt;
}

std::string MainWindow::prompt_handle() {
    HandleCtx ctx;
    auto guard = enter_modal();
    DialogBoxParamW(inst_, MAKEINTRESOURCEW(IDD_ENTER_HANDLE), hwnd_, &handle_proc,
                    reinterpret_cast<LPARAM>(&ctx));
    leave_modal(guard);
    return ctx.result.value_or(std::string{});
}

void MainWindow::do_find() {
    FindCtx ctx;
    ctx.text = find_query_;
    auto guard = enter_modal();
    const INT_PTR fr = DialogBoxParamW(inst_, MAKEINTRESOURCEW(IDD_FIND), hwnd_, &find_proc,
                                       reinterpret_cast<LPARAM>(&ctx));
    leave_modal(guard);
    if (fr != IDOK || !ctx.ok || ctx.text.empty())
        return;
    find_query_ = ctx.text;
    const int row = selected_row();
    find_from(row < 0 ? 0 : row, 1); // from the current row (inclusive), forward
}

void MainWindow::do_find_next() {
    if (find_query_.empty()) {
        do_find();
        return;
    }
    find_from(selected_row() + 1, 1); // from the row after the current
}

void MainWindow::do_find_prev() {
    if (find_query_.empty()) {
        do_find();
        return;
    }
    find_from(selected_row() - 1, -1); // from the row before the current, backward
}

void MainWindow::find_from(int start_row, int dir) {
    Timeline* tc = current();
    if (!tc || tc->rows.empty() || find_query_.empty())
        return;
    const int n = static_cast<int>(tc->rows.size());
    const std::wstring q = lowered(find_query_);
    for (int off = 0; off < n; ++off) { // scan in `dir`, wrapping around once
        const int i = ((start_row + dir * off) % n + n) % n;
        if (lowered(tc->rows[static_cast<size_t>(i)].text).find(q) != std::wstring::npos) {
            // Select normally (no updating_selection_ guard) so the move is noted
            // to the core and the screen reader reads the matched post.
            const UINT want = LVIS_SELECTED | LVIS_FOCUSED;
            ListView_SetItemState(timeline_view_, i, want, want);
            ListView_EnsureVisible(timeline_view_, i, FALSE);
            SetFocus(timeline_view_);
            return;
        }
    }
    announce("Not found");
}

void MainWindow::first_letter_nav(wchar_t letter) {
    Timeline* tc = current();
    if (!tc || tc->rows.empty())
        return;
    const wchar_t want = static_cast<wchar_t>(std::towlower(letter));
    const int n = static_cast<int>(tc->rows.size());
    const int start = selected_row() + 1; // begin after the current row
    for (int off = 0; off < n; ++off) { // scan forward, wrapping around once
        const int i = ((start + off) % n + n) % n;
        if (first_letter_of(tc->rows[static_cast<size_t>(i)].text) == want) {
            const UINT flags = LVIS_SELECTED | LVIS_FOCUSED;
            ListView_SetItemState(timeline_view_, i, flags, flags);
            ListView_EnsureVisible(timeline_view_, i, FALSE);
            SetFocus(timeline_view_);
            return;
        }
    }
    // No post starts with that letter: a brief boundary earcon, no chatter.
    if (settings_.value("boundary_sound", true))
        dispatch_cmd({{"cmd", "play_earcon"}, {"name", "boundary"}});
}

void MainWindow::about() {
    std::wstring text = L"FastSMRW\nVersion ";
    text += to_wide(fastsm::version());
    text += L"\n\nA fast, accessible Mastodon/Bluesky client.";
    MessageBoxW(hwnd_, text.c_str(), L"About FastSMRW", MB_OK | MB_ICONINFORMATION);
}

void MainWindow::handle_command(int id) {
    if (id >= ID_GOTO_TIMELINE_1 && id <= ID_GOTO_TIMELINE_1 + 8) {
        dispatch_cmd({{"cmd", "select_timeline"}, {"number", id - ID_GOTO_TIMELINE_1 + 1}});
        return;
    }
    switch (id) {
    case ID_ABOUT:
        about();
        break;
    case ID_SETTINGS:
        do_settings();
        break;
    case ID_KEYMAP_MANAGER:
        open_keymap_manager(hwnd_);
        break;
    case ID_CLIENT_FILTER:
        dispatch_cmd({{"cmd", "get_client_filter"}}); // core replies with client_filter -> dialog
        break;
    case ID_SERVER_FILTER:
        dispatch_cmd({{"cmd", "list_server_filters"}}); // core replies with server_filters -> dialog
        break;
    case ID_LIST_MANAGER:
        dispatch_cmd({{"cmd", "list_lists"}}); // core replies with lists -> manager dialog
        break;
    case ID_VIEW_MUTES:
        dispatch_cmd({{"cmd", "spawn_timeline"}, {"kind", "mutes"}});
        break;
    case ID_VIEW_BLOCKS:
        dispatch_cmd({{"cmd", "spawn_timeline"}, {"kind", "blocks"}});
        break;
    case ID_FOLLOW_REQUESTS:
        dispatch_cmd({{"cmd", "spawn_timeline"}, {"kind", "follow_requests"}});
        break;
    case ID_FOLLOWED_HASHTAGS:
        dispatch_cmd({{"cmd", "list_followed_hashtags"}}); // core replies -> manager dialog
        break;
    case ID_STOP_MEDIA:
        stop_media();
        break;
    case ID_CHECK_UPDATES:
        announce("Checking for updates…");
        dispatch_cmd({{"cmd", "check_for_update"}, {"silent", false}});
        break;
    case ID_FIND:
        do_find();
        break;
    case ID_FIND_NEXT:
        do_find_next();
        break;
    case ID_FIND_PREV:
        do_find_prev();
        break;
    case ID_HIDE_WINDOW:
        // Hide to the background; global hotkeys / the layer keep working, and
        // Ctrl+Win+W (or the layer's W) brings it back. Remembered across restarts.
        ShowWindow(hwnd_, SW_HIDE);
        dispatch_cmd({{"cmd", "set_window_shown"}, {"shown", false}});
        break;
    case ID_QUIT:
        DestroyWindow(hwnd_);
        break;
    case ID_NEW_POST:
        compose("new");
        break;
    case ID_REFRESH:
        dispatch_cmd({{"cmd", "refresh"}});
        break;
    case ID_REPLY:
        compose("reply");
        break;
    case ID_BOOST:
        do_boost();
        break;
    case ID_FAVORITE:
        do_favorite();
        break;
    case ID_QUOTE:
        compose("quote");
        break;
    case ID_POST_INFO:
        do_post_info();
        break;
    case ID_OPEN_BROWSER: {
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_status"}, {"id", id}});
        break;
    }
    case ID_OPEN_LINKS: {
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_post_links"}, {"id", id}});
        break;
    }
    case ID_VIEW_THREAD: {
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_thread"}, {"id", id}});
        break;
    }
    case ID_USER_TIMELINE: {
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_user_timeline"}, {"id", id}, {"pick", true}});
        break;
    }
    case ID_USER_PROFILE: {
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_user_profile"}, {"id", id}, {"pick", true}});
        break;
    }
    case ID_SPEAK_USER: {
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "speak_user"}, {"id", id}});
        break;
    }
    case ID_SPEAK_REPLY: {
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "speak_reply"}, {"id", id}});
        break;
    }
    case ID_FOLLOW_TOGGLE: {
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "follow_toggle"}, {"id", id}, {"pick", true}});
        break;
    }
    case ID_NEW_TIMELINE:
        do_new_timeline();
        break;
    case ID_TOGGLE_PIN:
        dispatch_cmd({{"cmd", "toggle_pin"}});
        break;
    case ID_CLOSE_TIMELINE:
        dispatch_cmd({{"cmd", "close_timeline"}});
        break;
    case ID_LOAD_OLDER:
        do_load_older();
        break;
    case ID_CLEAR_TIMELINE:
        if (!settings_.value("confirm_clear_timeline", true) ||
            confirm(hwnd_, L"Clear this timeline? This removes the loaded posts and its cache.",
                    L"Clear Timeline"))
            dispatch_cmd({{"cmd", "clear_timeline"}});
        break;
    case ID_CLEAR_ALL:
        if (!settings_.value("confirm_clear_timeline", true) ||
            confirm(hwnd_,
                    L"Clear all timelines? This removes the loaded posts and caches for every "
                    L"open timeline.",
                    L"Clear All Timelines"))
            dispatch_cmd({{"cmd", "clear_all_timelines"}});
        break;
    case ID_PREV_ACCOUNT:
        dispatch_cmd({{"cmd", "select_account"}, {"dir", "prev"}});
        break;
    case ID_NEXT_ACCOUNT:
        dispatch_cmd({{"cmd", "select_account"}, {"dir", "next"}});
        break;
    case ID_ADD_ACCOUNT:
        do_add_account();
        break;
    case ID_ACCOUNT_SETTINGS:
        dispatch_cmd({{"cmd", "get_account_settings"}}); // core replies -> ev_account_settings
        break;
    case ID_GO_BACK:
        dispatch_cmd({{"cmd", "go_back"}});
        break;
    case ID_MOVE_UP:
    case ID_MOVE_DOWN: {
        const std::string from = selected_id();
        if (!from.empty())
            dispatch_cmd({{"cmd", "move"},
                          {"from_id", from},
                          {"dir", id == ID_MOVE_UP ? "prev" : "next"}});
        break;
    }
    case ID_CYCLE_PREV:
        dispatch_cmd({{"cmd", "cycle_movement"}, {"dir", "prev"}});
        break;
    case ID_CYCLE_NEXT:
        dispatch_cmd({{"cmd", "cycle_movement"}, {"dir", "next"}});
        break;
    default:
        break;
    }
}

// --- events ---

void MainWindow::on_event(const std::string& js) {
    json e;
    try {
        e = json::parse(js);
    } catch (...) {
        return;
    }
    const std::string ev = e.value("event", std::string{});
    if (ev == "timelines_changed")
        ev_timelines_changed(e);
    else if (ev == "timeline_updated")
        ev_timeline_updated(e);
    else if (ev == "announce")
        announce(e.value("message", std::string{}));
    else if (ev == "settings")
        ev_settings(e);
    else if (ev == "account_settings")
        ev_account_settings(e);
    else if (ev == "compose_context")
        ev_compose_context(e);
    else if (ev == "spawnable_timelines")
        ev_spawnable(e);
    else if (ev == "post_info")
        ev_post_info(e);
    else if (ev == "user_profile")
        ev_user_profile(e);
    else if (ev == "user_picker")
        ev_user_picker(e);
    else if (ev == "user_suggestions")
        ev_user_suggestions(e);
    else if (ev == "url_picker")
        ev_url_picker(e);
    else if (ev == "select_row")
        restore_selection(e.value("id", std::string{}));
    else if (ev == "keymap")
        ev_keymap(e);
    else if (ev == "layer_keymap")
        ev_layer_keymap(e);
    else if (ev == "action_catalog")
        ev_action_catalog(e);
    else if (ev == "invisible_ui_action")
        ev_invisible_ui_action(e);
    else if (ev == "media_open")
        ev_media_open(e);
    else if (ev == "media_picker")
        ev_media_picker(e);
    else if (ev == "client_filter")
        ev_client_filter(e);
    else if (ev == "server_filters")
        ev_server_filters(e);
    else if (ev == "user_lists")
        ev_user_lists(e);
    else if (ev == "lists")
        ev_lists(e);
    else if (ev == "hashtag_prompt")
        ev_hashtag_prompt(e);
    else if (ev == "followed_hashtags")
        ev_followed_hashtags(e);
    else if (ev == "update_status")
        ev_update_status(e);
    else if (ev == "update_ready")
        ev_update_ready(e);
    else if (ev == "update_error")
        announce(e.value("error", std::string("Update failed.")));
    else if (ev == "open_url")
        ShellExecuteW(nullptr, L"open", to_wide(e.value("url", std::string{})).c_str(), nullptr,
                      nullptr, SW_SHOW);
    // accounts_changed / auth_result / post_result: nothing extra (sounds + any
    // announce come from the core).
}

void MainWindow::ev_timelines_changed(const json& e) {
    const size_t prev_count = timelines_.size();
    // Timeline kinds ("home", ...) repeat across accounts, so only carry over
    // rows/position within the SAME account (a spawn/close) -- never across an
    // account switch, or one account's position would leak onto another's.
    const std::string account = e.value("account", std::string{});
    const bool same_account = account == current_account_;
    current_account_ = account;
    std::vector<Timeline> next;
    for (const auto& t : e.value("timelines", json::array())) {
        Timeline tl;
        tl.title = to_wide(t.value("title", std::string{}));
        tl.kind = t.value("kind", std::string{});
        tl.dismissable = t.value("dismissable", false);
        tl.pinned = t.value("pinned", false);
        tl.user_list = t.value("user_list", false);
        if (same_account)
            for (const auto& old : timelines_) // carry rows/position for the same timeline
                if (old.kind == tl.kind) {
                    tl.rows = old.rows;
                    tl.selected_id = old.selected_id;
                    break;
                }
        next.push_back(std::move(tl));
    }
    timelines_ = std::move(next);
    current_ = e.value("current", 0);
    if (current_ < 0 || current_ >= static_cast<int>(timelines_.size()))
        current_ = 0;
    load_pending_ = false; // switching timelines resets the paging guard
    populate_timelines_list();
    bind_current_to_view(/*force=*/true); // a switch always rebinds (even to an empty timeline)
    update_pin_menu();
    // Match the Mac's focusTable(): when a NEW timeline appears (e.g. opening a
    // thread), put focus on the posts so the user lands on the content. Only on a
    // spawn (the count grew) — not a plain switch, or arrowing the timelines list
    // would yank focus to the posts. Guarded so we never steal focus from a dialog.
    if (timelines_.size() > prev_count && GetForegroundWindow() == hwnd_)
        SetFocus(timeline_view_);
}

void MainWindow::ev_timeline_updated(const json& e) {
    const int index = e.value("index", -1);
    if (index < 0 || index >= static_cast<int>(timelines_.size()))
        return;
    Timeline& tl = timelines_[static_cast<size_t>(index)];
    tl.reversed = e.value("reversed", false);
    tl.rows.clear();
    for (const auto& r : e.value("rows", json::array())) {
        Row row;
        row.id = r.value("id", std::string{});
        row.text = to_wide(r.value("text", std::string{}));
        row.favorited = r.value("favorited", false);
        row.boosted = r.value("boosted", false);
        row.is_mine = r.value("is_mine", false);
        row.gap_after = r.value("gap_after", false);
        row.follow_request = r.value("follow_request", false);
        row.account_id = r.value("account_id", std::string{});
        row.acct = r.value("acct", std::string{});
        tl.rows.push_back(std::move(row));
    }
    if (tl.selected_id.empty()) // first load: adopt the core's remembered position
        tl.selected_id = e.value("selected_id", std::string{});
    if (index == current_) {
        load_pending_ = false; // rows changed -> a queued page can load next
        bind_current_to_view();
    }
}

void MainWindow::ev_settings(const json& e) {
    settings_ = e.value("settings", json::object());
    soundpacks_.clear();
    for (const auto& p : e.value("soundpacks", json::array()))
        soundpacks_.push_back(p.get<std::string>());
    if (action_catalog_.empty()) // load once so the Keyboard Manager has its actions
        dispatch_cmd({{"cmd", "get_action_catalog"}});
    apply_invisible();
    // Quietly check for updates once on startup, if enabled.
    if (!startup_update_checked_) {
        startup_update_checked_ = true;
        if (settings_.value("check_updates_on_startup", true))
            dispatch_cmd({{"cmd", "check_for_update"}, {"silent", true}});
    }
}

bool MainWindow::installed_mode() const {
    // The installer drops installed.txt next to the exe; a portable zip never has it.
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p = exe;
    const size_t slash = p.find_last_of(L"\\/");
    const std::wstring marker = p.substr(0, slash) + L"\\installed.txt";
    return GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void MainWindow::ev_update_status(const json& e) {
    const bool silent = e.value("silent", false);
    const std::string error = e.value("error", std::string{});
    const bool available = e.value("available", false);
    const std::string branch = e.value("branch", std::string{});
    const std::string version = e.value("version", std::string{});
    const std::string notes = e.value("notes", std::string{});
    pending_update_url_ = e.value("download_url", std::string{});
    pending_installer_url_ = e.value("installer_url", std::string{});

    if (!error.empty()) {
        if (!silent)
            MessageBoxW(hwnd_, to_wide(error).c_str(), L"Check for Updates",
                        MB_OK | MB_ICONWARNING);
        return;
    }
    if (!available) {
        if (!silent)
            MessageBoxW(hwnd_, L"You're running the latest version.", L"Check for Updates",
                        MB_OK | MB_ICONINFORMATION);
        return;
    }
    // An installed copy (installed.txt marker) updates via the setup exe when the
    // release ships one; a portable copy uses the zip.
    const bool use_installer = installed_mode() && !pending_installer_url_.empty();
    const std::string& url = use_installer ? pending_installer_url_ : pending_update_url_;
    if (url.empty())
        return; // nothing to download

    std::wstring msg;
    if (branch == "latest")
        msg = L"A newer build is available.";
    else
        msg = L"Version " + to_wide(version) + L" is available (you have " +
              to_wide(fastsm::version()) + L").";
    msg += L"\n\nDownload and install it now? FastSMRW will restart to finish.";
    if (!notes.empty()) {
        std::string trimmed = notes.substr(0, 700);
        msg += L"\n\nRelease notes:\n" + to_wide(trimmed);
    }
    if (MessageBoxW(hwnd_, msg.c_str(), L"Update Available", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        announce("Downloading update…");
        dispatch_cmd({{"cmd", "download_update"}, {"url", url}, {"installer", use_installer}});
    }
}

void MainWindow::ev_update_ready(const json& e) {
    const std::string path = e.value("path", std::string{});
    if (path.empty()) {
        announce("Update download failed.");
        return;
    }
    // Installed copy: run the downloaded setup silently, then quit so it can
    // replace the running files. It writes the installed.txt marker again itself.
    if (e.value("installer", false)) {
        announce("Installing update and restarting…");
        ShellExecuteW(hwnd_, L"open", to_wide(path).c_str(), L"/SILENT", nullptr, SW_SHOWNORMAL);
        DestroyWindow(hwnd_);
        return;
    }
    const std::string zip = path; // portable copy: extract the zip over the run folder
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring exe_path = exe;
    const size_t slash = exe_path.find_last_of(L"\\/");
    const std::wstring app_dir = exe_path.substr(0, slash);
    const std::wstring exe_name = exe_path.substr(slash + 1);
    const std::wstring extract_dir = app_dir + L"\\update_temp";
    const std::wstring bat_path = app_dir + L"\\fastsmrw-update.bat";
    const unsigned long pid = GetCurrentProcessId();

    // Batch: wait for this process to exit, extract the zip, overlay the run
    // folder, relaunch, and delete itself. Quote every path for spaces.
    std::string bat;
    bat += "@echo off\r\nchcp 65001 >nul\r\n";
    bat += "powershell -NoProfile -ExecutionPolicy Bypass -Command \"Wait-Process -Id " +
           std::to_string(pid) + " -Timeout 20 -ErrorAction SilentlyContinue; " +
           "Expand-Archive -LiteralPath '" + zip + "' -DestinationPath '" + to_utf8(extract_dir) +
           "' -Force\"\r\n";
    bat += "xcopy /s /e /y /q \"" + to_utf8(extract_dir) + "\\*\" \"" + to_utf8(app_dir) +
           "\\\" >nul\r\n";
    bat += "rmdir /s /q \"" + to_utf8(extract_dir) + "\"\r\n";
    bat += "del /q \"" + zip + "\"\r\n";
    bat += "start \"\" \"" + to_utf8(app_dir) + "\\" + to_utf8(exe_name) + "\"\r\n";
    bat += "del \"%~f0\"\r\n";

    {
        std::ofstream out(bat_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            announce("Couldn't write the updater script.");
            return;
        }
        out.write(bat.data(), static_cast<std::streamsize>(bat.size()));
    }
    // Launch the updater hidden, then quit so it can replace the running files.
    ShellExecuteW(nullptr, L"open", bat_path.c_str(), nullptr, app_dir.c_str(), SW_HIDE);
    announce("Installing update and restarting…");
    DestroyWindow(hwnd_);
}

void MainWindow::apply_invisible() {
    invisible_mode_ = settings_.value("invisible_mode", std::string("off"));
    overlay_layer_ = false;
    if (invisible_mode_ == "hotkey" || invisible_mode_ == "keyhook") {
        dispatch_cmd({{"cmd", "get_keymap"}}); // ev_keymap installs the active driver
        dispatch_cmd({{"cmd", "get_layer_keymap"}}); // cache it for on-demand EnterLayer
    } else if (invisible_mode_ == "layer") {
        dispatch_cmd({{"cmd", "get_layer_keymap"}}); // ev_layer_keymap installs the layer hook
    } else {
        hotkey_driver_.clear();
        keyhook_driver_.disable();
    }
}

void MainWindow::ev_keymap(const json& e) {
    // Forward to the Keyboard Manager if it's open (it may have asked for any keymap).
    if (keymap_mgr_)
        keymap_mgr_->on_keymap(e);
    // Only (re)bind the global hotkeys for the ACTIVE keymap — ignore events for
    // other keymaps the manager is merely browsing.
    const std::string active = settings_.value("invisible_keymap", std::string("default"));
    if (e.value("name", std::string{}) != active)
        return;
    invisible_bindings_.clear();
    // Bind to a named object first: iterating .items() on the temporary returned
    // by value() would dangle (the proxy outlives the temporary).
    const json bindings = e.value("bindings", json::object());
    for (const auto& [key, action] : bindings.items())
        invisible_bindings_[key] = action.get<std::string>();
    install_active_driver();
}

// Install whichever driver the active mode calls for; keep the other idle.
void MainWindow::install_active_driver() {
    fastsm::log::write("invisible: install driver for mode '" + invisible_mode_ + "', " +
                       std::to_string(invisible_bindings_.size()) + " binding(s)");
    if (invisible_mode_ == "hotkey") {
        keyhook_driver_.disable();
        hotkey_driver_.set_bindings(invisible_bindings_);
    } else if (invisible_mode_ == "keyhook") {
        hotkey_driver_.clear();
        keyhook_driver_.set_hotkeys(invisible_bindings_);
        keyhook_driver_.enable();
    } else {
        hotkey_driver_.clear();
        keyhook_driver_.disable();
    }
}

void MainWindow::leave_layer() {
    keyhook_driver_.exit_layer();
    if (overlay_layer_) { // the layer was an on-demand overlay: restore the base driver
        overlay_layer_ = false;
        install_active_driver();
    }
}

void MainWindow::ev_layer_keymap(const json& e) {
    // Cache the layer map in every mode: hotkey/keyhook mode needs it ready so the
    // EnterLayer action can call the layer up on demand.
    layer_bindings_.clear();
    const json bindings = e.value("bindings", json::object());
    for (const auto& [key, action] : bindings.items())
        layer_bindings_[key] = action.get<std::string>();
    layer_activation_ = e.value("activation", std::string("control+win+space"));
    layer_enter_message_ = e.value("enter_message", std::string("FastSM layer"));
    layer_help_message_ = e.value("help_message", std::string{});
    if (invisible_mode_ == "layer") {
        hotkey_driver_.clear();
        keyhook_driver_.set_layer(layer_activation_, layer_bindings_);
        keyhook_driver_.enable();
    }
}

void MainWindow::ev_action_catalog(const json& e) {
    action_catalog_.clear();
    const json actions = e.value("actions", json::array());
    for (const auto& a : actions)
        action_catalog_.push_back({a.value("id", std::string{}), a.value("label", std::string{}),
                                   a.value("default_key", std::string{})});
}

void MainWindow::open_keymap_manager(HWND parent) {
    if (action_catalog_.empty()) {
        announce("Keyboard actions are still loading; try again in a moment.");
        dispatch_cmd({{"cmd", "get_action_catalog"}});
        return;
    }
    const std::string active = settings_.value("invisible_keymap", std::string("default"));
    KeymapManagerDialog dlg(inst_, action_catalog_, active,
                            [this](const json& cmd) { dispatch_cmd(cmd); });
    keymap_mgr_ = &dlg;
    dlg.run(parent);
    keymap_mgr_ = nullptr;
}

void MainWindow::ev_client_filter(const json& e) {
    if (!e.value("available", false)) {
        announce("Open a timeline first to filter it.");
        return;
    }
    const json f = e.value("filter", json::object());
    ClientFilterValues v;
    v.original = f.value("original", true);
    v.replies = f.value("replies", true);
    v.replies_to_me = f.value("replies_to_me", true);
    v.threads = f.value("threads", true);
    v.boosts = f.value("boosts", true);
    v.quotes = f.value("quotes", true);
    v.media = f.value("media", true);
    v.no_media = f.value("no_media", true);
    v.my_posts = f.value("my_posts", true);
    v.my_replies = f.value("my_replies", true);
    v.text = to_wide(f.value("text", std::string{}));
    switch (show_client_filter_dialog(hwnd_, inst_, v)) {
    case ClientFilterAction::Apply: {
        json nf = {{"original", v.original},         {"replies", v.replies},
                   {"replies_to_me", v.replies_to_me}, {"threads", v.threads},
                   {"boosts", v.boosts},             {"quotes", v.quotes},
                   {"media", v.media},               {"no_media", v.no_media},
                   {"my_posts", v.my_posts},         {"my_replies", v.my_replies},
                   {"text", to_utf8(v.text)}};
        dispatch_cmd({{"cmd", "set_client_filter"}, {"filter", nf}});
        announce("Filter applied.");
        break;
    }
    case ClientFilterAction::Clear:
        dispatch_cmd({{"cmd", "clear_client_filter"}});
        announce("Filter cleared.");
        break;
    case ClientFilterAction::Cancel:
        break;
    }
}

void MainWindow::ev_server_filters(const json& e) {
    if (server_filters_mgr_) { // manager is open: live-refresh its list
        server_filters_mgr_->on_server_filters(e);
        return;
    }
    if (!e.value("supported", false)) {
        announce("Server filters are only available for Mastodon accounts.");
        return;
    }
    ServerFiltersDialog dlg(inst_, [this](const json& cmd) { dispatch_cmd(cmd); });
    server_filters_mgr_ = &dlg;
    dlg.run(hwnd_, e);
    server_filters_mgr_ = nullptr;
}

void MainWindow::ev_lists(const json& e) {
    if (lists_mgr_) { // manager is open: live-refresh its list
        lists_mgr_->on_lists(e);
        return;
    }
    if (!e.value("supported", false)) {
        announce("Lists are only available for Mastodon accounts.");
        return;
    }
    ListsManagerDialog dlg(inst_, [this](const json& cmd) { dispatch_cmd(cmd); });
    lists_mgr_ = &dlg;
    auto guard = enter_modal();
    dlg.run(hwnd_, e);
    leave_modal(guard);
    lists_mgr_ = nullptr;
}

void MainWindow::ev_hashtag_prompt(const json& e) {
    std::vector<std::wstring> prefill;
    for (const auto& t : e.value("tags", json::array()))
        if (t.is_string())
            prefill.push_back(to_wide(t.get<std::string>()));
    auto guard = enter_modal();
    std::optional<std::string> name = show_follow_hashtag_dialog(hwnd_, inst_, prefill);
    leave_modal(guard);
    if (name && !name->empty())
        dispatch_cmd({{"cmd", "follow_hashtag"}, {"name", *name}});
}

void MainWindow::ev_followed_hashtags(const json& e) {
    if (followed_tags_mgr_) { // manager is open: live-refresh its list
        followed_tags_mgr_->on_followed(e);
        return;
    }
    if (!e.value("supported", false)) {
        announce("Following hashtags is only available for Mastodon accounts.");
        return;
    }
    FollowedHashtagsDialog dlg(inst_, [this](const json& cmd) { dispatch_cmd(cmd); });
    followed_tags_mgr_ = &dlg;
    auto guard = enter_modal();
    dlg.run(hwnd_, e);
    leave_modal(guard);
    followed_tags_mgr_ = nullptr;
}

void MainWindow::ev_invisible_ui_action(const json& e) {
    const std::string a = e.value("action", std::string{});
    if (a == "EnterLayer") {
        // Call the layer up on demand from hotkey/keyhook mode. No-op if the layer
        // is already the active mode, we're already in an overlay, or the layer map
        // hasn't loaded yet.
        if (invisible_mode_ == "layer" || overlay_layer_ || layer_bindings_.empty())
            return;
        overlay_layer_ = true;
        hotkey_driver_.clear(); // avoid modified-key collisions while the layer is up
        keyhook_driver_.open_layer(layer_activation_, layer_bindings_);
        dispatch_cmd({{"cmd", "play_earcon"}, {"name", "navigate"}});
        announce(layer_enter_message_);
        return;
    }
    if (a == "ToggleWindow") {
        // Pure visibility toggle, matching the Python client: shown -> hide,
        // hidden -> show + raise. Global hotkeys keep working while hidden. The
        // shown/hidden state is remembered across restarts.
        const bool show = !IsWindowVisible(hwnd_);
        ShowWindow(hwnd_, show ? SW_SHOW : SW_HIDE);
        if (show)
            SetForegroundWindow(hwnd_);
        dispatch_cmd({{"cmd", "set_window_shown"}, {"shown", show}});
    } else if (a == "Options") {
        do_settings();
    } else if (a == "KeymapManager") {
        if (!IsWindowVisible(hwnd_)) {
            ShowWindow(hwnd_, SW_SHOW);
            SetForegroundWindow(hwnd_);
            dispatch_cmd({{"cmd", "set_window_shown"}, {"shown", true}});
        }
        open_keymap_manager(hwnd_);
    } else if (a == "Find") {
        do_find();
    } else if (a == "FindNext") {
        do_find_next();
    } else if (a == "FindPrev") {
        do_find_prev();
    } else if (a == "NewTimeline") {
        do_new_timeline(); // requests spawnable list; ev_spawnable opens the dialog
    } else if (a == "UserActions") {
        show_user_actions(); // the Enter default on a user, from the invisible interface
    } else if (a == "StopMedia") {
        stop_media();
    } else if (a == "Exit") {
        DestroyWindow(hwnd_); // real quit (same as the Quit menu item), not just hide
    }
}

void MainWindow::ev_media_open(const json& e) {
    const std::string kind = e.value("kind", std::string{});
    const std::wstring url = to_wide(e.value("url", std::string{}));
    const std::wstring title = to_wide(e.value("title", std::string{}));
    if (url.empty())
        return;
    // Only audio streams in the in-app player; images/video/gifv open in the
    // system app (unchanged from when the core emitted open_url for them).
    if (!kind.empty() && kind != "audio") {
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOW);
        return;
    }
    if (settings_.value("media_background", false)) { // no window; stop with Ctrl+S
        play_media_background(url, title);
        return;
    }
    leave_layer(); // a window is opening; leave the layer (restores an overlay)
    const bool played = show_media_player(hwnd_, inst_, title, url,
                                          [this](const std::wstring& m) { announce(to_utf8(m)); });
    if (!played) // couldn't stream it (e.g. an unsupported codec) -> system player
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOW);
}

void MainWindow::play_media_background(const std::wstring& url, const std::wstring& title) {
    if (!media_bg_)
        media_bg_ = std::make_unique<MediaPlayback>();
    if (media_bg_->play(url)) {
        announce("Playing " + to_utf8(title));
        SetTimer(hwnd_, kMediaBgTimer, 1000, nullptr); // auto-clear when it ends
    } else {
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOW);
    }
}

void MainWindow::stop_media() {
    if (media_bg_ && media_bg_->active()) {
        media_bg_->stop();
        KillTimer(hwnd_, kMediaBgTimer);
        announce("Stopped");
    }
}

void MainWindow::ev_media_picker(const json& e) {
    const std::string id = e.value("id", std::string{});
    const auto items = e.value("items", json::array());
    if (items.empty())
        return;
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    for (size_t i = 0; i < items.size(); ++i)
        AppendMenuW(menu, MF_STRING, static_cast<UINT>(i + 1),
                    to_wide(items[i].value("title", std::string{})).c_str());
    POINT pt{0, 0};
    RECT rc;
    const int frow = selected_row();
    if (frow >= 0 && ListView_GetItemRect(timeline_view_, frow, &rc, LVIR_BOUNDS)) {
        pt.x = rc.left;
        pt.y = rc.bottom;
        ClientToScreen(timeline_view_, &pt);
    } else {
        GetCursorPos(&pt);
    }
    auto guard = enter_modal(); // works even when the window is hidden (invisible interface)
    const int chosen = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd_,
        nullptr));
    leave_modal(guard);
    DestroyMenu(menu);
    if (chosen <= 0 || chosen > static_cast<int>(items.size()))
        return;
    const auto& it = items[static_cast<size_t>(chosen - 1)];
    dispatch_cmd({{"cmd", "play_media"},
                  {"id", id},
                  {"url", it.value("url", std::string{})},
                  {"kind", it.value("kind", std::string{})},
                  {"title", it.value("title", std::string{})}});
}

void MainWindow::ev_compose_context(const json& e) {
    leave_layer(); // a modal dialog is opening; leave the layer (restores an overlay)
    const std::string keep_id = selected_id();
    ComposeRequest req;
    const std::string mode = e.value("mode", std::string("new"));
    req.mode = mode == "reply"  ? ComposeMode::Reply
               : mode == "quote" ? ComposeMode::Quote
               : mode == "edit"  ? ComposeMode::Edit
                                 : ComposeMode::New;
    req.title = to_wide(e.value("title", std::string("New Post")));
    req.context_label = e.value("context_label", std::string{});
    req.prefill_text = e.value("prefill_text", std::string{});
    req.prefill_cw = e.value("prefill_cw", std::string{});
    for (const auto& p : e.value("reply_participants", json::array()))
        req.recipients.push_back({p.value("acct", std::string{}),
                                  to_wide(p.value("display_name", p.value("acct", std::string{}))),
                                  p.value("checked", true)});
    req.max_chars = e.value("max_chars", 500);
    req.enter_to_send = e.value("enter_to_send", false);
    if (e.contains("default_visibility"))
        req.default_visibility = static_cast<Visibility>(e["default_visibility"].get<int>());
    if (auto f = e.find("features"); f != e.end() && f->is_object()) {
        req.features.visibility = f->value("visibility", false);
        req.features.content_warning = f->value("content_warning", false);
        req.features.quote_posts = f->value("quote_posts", false);
        req.features.polls = f->value("polls", false);
        req.features.editing = f->value("editing", false);
        req.features.scheduling = f->value("scheduling", false);
        req.features.media = f->value("media", false);
    }

    auto guard = enter_modal();
    auto result = show_compose_dialog(
        hwnd_, inst_, req,
        [this](HWND owner, const std::string& partial) { return pick_mention(owner, partial); });
    restore_selection(keep_id);
    leave_modal(guard);
    if (!result)
        return;

    json draft = draft_to_json(result->draft);
    if (!result->mentions.empty())
        draft["mentions"] = result->mentions; // checked reply recipients; core prepends them
    if (!result->attachments.empty()) {
        json atts = json::array();
        for (const auto& a : result->attachments)
            atts.push_back({{"filename", to_utf8(a.filename)},
                            {"mime", a.mime},
                            {"data", a.data_base64},
                            {"alt", to_utf8(a.alt)}});
        draft["attachments"] = std::move(atts);
    }
    if (e.contains("reply_to_id"))
        draft["reply_to_id"] = e["reply_to_id"];
    if (e.contains("reply_to_url")) // remote reply target: resolved core-side
        draft["reply_to_url"] = e["reply_to_url"];
    if (e.contains("quoted_status_id"))
        draft["quoted_status_id"] = e["quoted_status_id"];
    if (e.contains("quoted_status_cid"))
        draft["quoted_status_cid"] = e["quoted_status_cid"];
    if (e.contains("quoted_status_url"))
        draft["quoted_status_url"] = e["quoted_status_url"];
    json cmd = {{"cmd", "post"}, {"draft", draft}};
    if (e.contains("edit_id"))
        cmd["edit_id"] = e["edit_id"];
    dispatch_cmd(cmd);
}

void MainWindow::ev_spawnable(const json& e) {
    leave_layer(); // a modal dialog is opening; leave the layer (restores an overlay)
    spawnable_kinds_.clear();
    spawnable_params_.clear();
    std::vector<fastsmui::NewTimelineEntry> entries;
    for (const auto& t : e.value("timelines", json::array())) {
        spawnable_kinds_.push_back(t.value("kind", std::string{}));
        spawnable_params_.push_back(t.value("param", std::string{}));
        entries.push_back({to_wide(t.value("title", std::string{})),
                           to_wide(t.value("input", std::string{}))});
    }
    if (entries.empty()) {
        announce("No more timelines to add for this account.");
        return;
    }
    auto guard = enter_modal();
    auto choice = show_new_timeline_dialog(hwnd_, inst_, entries);
    leave_modal(guard);
    if (!choice || choice->index < 0 || choice->index >= static_cast<int>(spawnable_kinds_.size()))
        return;
    const size_t ci = static_cast<size_t>(choice->index);
    json cmd = {{"cmd", "spawn_timeline"}, {"kind", spawnable_kinds_[ci]}};
    if (!choice->value.empty())
        cmd["value"] = to_utf8(choice->value);
    if (ci < spawnable_params_.size() && !spawnable_params_[ci].empty())
        cmd["param"] = spawnable_params_[ci];
    dispatch_cmd(cmd);
}

int MainWindow::track_popup(HMENU menu, POINT pt) {
    // A popup needs a visible foreground window or focus never lands on it; when
    // the app is hidden (invisible interface), briefly show it and restore after
    // (mirrors FastPlay's hidden-window menu handling).
    const bool was_hidden = !IsWindowVisible(hwnd_);
    if (was_hidden)
        ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    const int chosen = static_cast<int>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd_,
        nullptr));
    PostMessageW(hwnd_, WM_NULL, 0, 0); // let the menu dismiss cleanly
    if (was_hidden)
        ShowWindow(hwnd_, SW_HIDE);
    return chosen;
}

MainWindow::ModalGuard MainWindow::enter_modal() {
    ModalGuard g;
    g.prior = GetForegroundWindow();
    g.was_hidden = !IsWindowVisible(hwnd_);
    if (g.was_hidden)
        ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_); // ensure the dialog we own can take focus
    return g;
}

void MainWindow::leave_modal(const ModalGuard& g) {
    if (g.was_hidden)
        ShowWindow(hwnd_, SW_HIDE);
    // Return focus to wherever the user was (another app) instead of letting the
    // now-closed dialog reactivate our (possibly hidden) main window. A no-op when
    // we were already the foreground window (normal in-app use).
    if (g.prior && g.prior != hwnd_ && IsWindow(g.prior) && IsWindowVisible(g.prior))
        SetForegroundWindow(g.prior);
}

void MainWindow::announce(const std::string& message) {
    // Speak via the screen reader / TTS only; the window title stays "FastSMRW"
    // (it used to mirror the last announcement, which just cluttered the title bar).
    if (speaker_ && !message.empty())
        speaker_->speak(message, true);
}

} // namespace fastsmui
