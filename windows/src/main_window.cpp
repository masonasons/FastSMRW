#include "main_window.hpp"

#include <cwctype>
#include <fstream>

#include <commctrl.h>
#include <shellapi.h>

#include "../resources/resource.h"
#include "add_account_dialog.hpp"
#include "app_messages.hpp"
#include "new_timeline_dialog.hpp"
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
    ID_OPEN_BROWSER,
    ID_NEW_TIMELINE,
    ID_CLOSE_TIMELINE,
    ID_CLEAR_TIMELINE,
    ID_CLEAR_ALL,
    ID_GO_BACK,
    ID_MOVE_UP,
    ID_MOVE_DOWN,
    ID_CYCLE_PREV,
    ID_CYCLE_NEXT,
    ID_PREV_ACCOUNT,
    ID_NEXT_ACCOUNT,
    ID_ADD_ACCOUNT,
    ID_KEYMAP_MANAGER,
    ID_FIND,
    ID_FIND_NEXT,
    ID_FIND_PREV,
    ID_CHECK_UPDATES,
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
    AppendMenuW(app, MF_STRING, ID_SETTINGS, L"&Settings…\tCtrl+,");
    AppendMenuW(app, MF_STRING, ID_KEYMAP_MANAGER, L"&Keyboard Manager…");
    AppendMenuW(app, MF_STRING, ID_CHECK_UPDATES, L"Check for &Updates…");
    AppendMenuW(app, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(app, MF_STRING, ID_NEW_POST, L"&New Post\tCtrl+N");
    AppendMenuW(app, MF_STRING, ID_REFRESH, L"&Refresh Timeline\tCtrl+R");
    AppendMenuW(app, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(app, MF_STRING, ID_QUIT, L"&Quit FastSMRW\tCtrl+Q");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(app), L"&Application");

    HMENU status = CreatePopupMenu();
    AppendMenuW(status, MF_STRING, ID_REPLY, L"&Reply\tR");
    AppendMenuW(status, MF_STRING, ID_BOOST, L"&Boost\tCtrl+Shift+B");
    AppendMenuW(status, MF_STRING, ID_FAVORITE, L"&Favorite\tCtrl+Shift+D");
    AppendMenuW(status, MF_STRING, ID_QUOTE, L"&Quote\tCtrl+Shift+Q");
    AppendMenuW(status, MF_STRING, ID_POST_INFO, L"Post &Info…\tEnter");
    AppendMenuW(status, MF_STRING, ID_VIEW_THREAD, L"View &Thread");
    AppendMenuW(status, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(status, MF_STRING, ID_USER_TIMELINE, L"Open &User Timeline");
    AppendMenuW(status, MF_STRING, ID_USER_PROFILE, L"Open User &Profile\tCtrl+U");
    AppendMenuW(status, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(status, MF_STRING, ID_OPEN_BROWSER, L"Open in Browser");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(status), L"&Status");

    HMENU timeline = CreatePopupMenu();
    AppendMenuW(timeline, MF_STRING, ID_NEW_TIMELINE, L"&New Timeline…\tCtrl+T");
    AppendMenuW(timeline, MF_STRING, ID_CLOSE_TIMELINE, L"&Close Timeline\tBackspace");
    AppendMenuW(timeline, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(timeline, MF_STRING, ID_FIND, L"&Find…\tCtrl+F");
    AppendMenuW(timeline, MF_STRING, ID_FIND_NEXT, L"Find &Next\tF3");
    AppendMenuW(timeline, MF_STRING, ID_FIND_PREV, L"Find &Previous\tShift+F3");
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
    AppendMenuW(account, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(account, MF_STRING, ID_PREV_ACCOUNT, L"&Previous Account\tCtrl+[");
    AppendMenuW(account, MF_STRING, ID_NEXT_ACCOUNT, L"&Next Account\tCtrl+]");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(account), L"A&ccount");

    return bar;
}

} // namespace

MainWindow::MainWindow(HINSTANCE inst) : inst_(inst) {}

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
        {FVIRTKEY | FCONTROL, VK_OEM_COMMA, ID_SETTINGS},
        {FVIRTKEY | FCONTROL, 'Q', ID_QUIT},
        {FVIRTKEY | FCONTROL | FSHIFT, 'A', ID_ADD_ACCOUNT},
        {FVIRTKEY | FCONTROL | FSHIFT, 'B', ID_BOOST},
        {FVIRTKEY | FCONTROL | FSHIFT, 'D', ID_FAVORITE},
        {FVIRTKEY | FCONTROL | FSHIFT, 'Q', ID_QUOTE},
        {FVIRTKEY | FCONTROL, VK_OEM_4, ID_PREV_ACCOUNT}, // Ctrl+[
        {FVIRTKEY | FCONTROL, VK_OEM_6, ID_NEXT_ACCOUNT}, // Ctrl+]
        {FVIRTKEY, VK_BACK, ID_CLOSE_TIMELINE},          // Backspace: close timeline (anywhere)
        {FVIRTKEY | FCONTROL, VK_DELETE, ID_CLEAR_TIMELINE},          // clear focused timeline
        {FVIRTKEY | FCONTROL | FSHIFT, VK_DELETE, ID_CLEAR_ALL}, // clear every timeline
        {FVIRTKEY | FCONTROL, 'Z', ID_GO_BACK},
        {FVIRTKEY | FCONTROL, 'F', ID_FIND},         // Ctrl+F: find in timeline
        {FVIRTKEY, VK_F3, ID_FIND_NEXT},             // F3: find next
        {FVIRTKEY | FSHIFT, VK_F3, ID_FIND_PREV},    // Shift+F3: find previous
        {FVIRTKEY | FCONTROL, 'U', ID_USER_PROFILE}, // Ctrl+U: open user profile
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
            announce(layer_help_message_);
            return 0;
        }
        if (*action == KeyhookDriver::kLayerExit) {
            dispatch_cmd({{"cmd", "play_earcon"}, {"name", "close"}});
            return 0;
        }
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
        if (!action.empty())
            dispatch_cmd({{"cmd", "perform_action"}, {"action", action}});
        return 0;
    }

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

void MainWindow::maybe_load_older(int row) {
    if (load_pending_)
        return;
    Timeline* tc = current();
    if (!tc)
        return;
    const int count = static_cast<int>(tc->rows.size());
    // A tracked middle gap within the next few rows -> fill it transparently.
    for (int g = row; g < count && g <= row + 5; ++g) {
        if (tc->rows[static_cast<size_t>(g)].gap_after) {
            load_pending_ = true;
            dispatch_cmd({{"cmd", "load_gap"}, {"id", tc->rows[static_cast<size_t>(g)].id}});
            return;
        }
    }
    if (count > 0 && row >= count - 10) { // within 10 rows of the bottom
        load_pending_ = true;
        dispatch_cmd({{"cmd", "load_older"}});
    }
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
            idx = 0;
            tc->selected_id = tc->rows[0].id; // adopt the top as the position
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
    switch (vk) {
    case VK_UP:
    case VK_DOWN: {
        Timeline* tc = current();
        const int count = tc ? static_cast<int>(tc->rows.size()) : 0;
        const int row = selected_row();
        const bool at_edge = (vk == VK_UP && row <= 0) || (vk == VK_DOWN && row >= count - 1);
        if (at_edge && count > 0)
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
            dispatch_cmd({{"cmd", "open_user_timeline"}, {"id", id}});
        break;
    }
    case VK_RETURN:
        if (Timeline* t = current(); t && t->user_list)
            show_user_actions(); // batch follow/mute/block on selected users
        else
            do_post_info();
        break;
    default:
        break;
    }
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
    dispatch_cmd({{"cmd", "toggle_favorite"}, {"id", r.id}});
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
    keyhook_driver_.exit_layer(); // a modal dialog is opening; leave the layer
    const std::string id = e.value("id", std::string{});
    const std::wstring text = to_wide(e.value("text", std::string{}));
    const bool quote_ok = e.contains("features") && e["features"].value("quote_posts", false);
    const bool browser_ok = e.value("has_url", false);
    const std::string keep_id = selected_id();
    auto action = show_post_info_dialog(hwnd_, inst_, text, quote_ok, browser_ok);
    restore_selection(keep_id);
    if (!action)
        return;
    switch (*action) {
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
    case PostInfoAction::ViewThread:
        dispatch_cmd({{"cmd", "open_thread"}, {"id", id}});
        break;
    case PostInfoAction::ViewAuthor:
        dispatch_cmd({{"cmd", "open_user_timeline"}, {"id", id}});
        break;
    }
}

void MainWindow::ev_user_profile(const json& e) {
    keyhook_driver_.exit_layer(); // a modal dialog is opening; leave the layer
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
    const std::string keep_id = selected_id();
    auto action = show_user_profile_dialog(hwnd_, inst_, text, rel);
    restore_selection(keep_id);
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
            set_rel("unblock");
        } else if (!settings_.value("confirm_block", true) ||
                   confirm(hwnd_, (L"Block @" + to_wide(acct) + L"?").c_str(), L"Block")) {
            set_rel("block");
        }
        break;
    case UserProfileAction::ToggleBoosts:
        set_rel(rel.showing_reblogs ? "hide_boosts" : "show_boosts");
        break;
    }
}

void MainWindow::ev_user_picker(const json& e) {
    if (!e.contains("users") || !e["users"].is_array() || e["users"].empty())
        return;
    keyhook_driver_.exit_layer(); // a modal dialog is opening; leave the layer
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
    const int chosen = static_cast<int>(TrackPopupMenu(menu,
                                                       TPM_RETURNCMD | TPM_NONOTIFY |
                                                           TPM_LEFTALIGN | TPM_TOPALIGN,
                                                       pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (chosen <= 0 || chosen > static_cast<int>(list.size()))
        return;
    const auto& [account_id, acct] = list[static_cast<size_t>(chosen - 1)];
    if (purpose == "timeline")
        dispatch_cmd({{"cmd", "open_user_timeline"}, {"account_id", account_id}, {"acct", acct}});
    else
        dispatch_cmd({{"cmd", "open_user_profile"}, {"id", row_id}, {"account_id", account_id}});
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
    static const Act kActs[] = {
        {L"&Follow", "follow"}, {L"&Unfollow", "unfollow"}, {L"&Mute", "mute"},
        {L"Un&mute", "unmute"}, {L"&Block", "block"},       {L"Un&block", "unblock"},
    };
    const int kActCount = static_cast<int>(sizeof(kActs) / sizeof(kActs[0]));
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
    if (std::string(action) == "block" && settings_.value("confirm_block", true)) {
        const std::wstring msg =
            count > 1 ? L"Block " + std::to_wstring(count) + L" users?" : L"Block this user?";
        if (!confirm(hwnd_, msg.c_str(), L"Block"))
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
} // namespace

void MainWindow::do_find() {
    FindCtx ctx;
    ctx.text = find_query_;
    if (DialogBoxParamW(inst_, MAKEINTRESOURCEW(IDD_FIND), hwnd_, &find_proc,
                        reinterpret_cast<LPARAM>(&ctx)) != IDOK ||
        !ctx.ok || ctx.text.empty())
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
    case ID_VIEW_THREAD: {
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_thread"}, {"id", id}});
        break;
    }
    case ID_USER_TIMELINE: {
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_user_timeline"}, {"id", id}});
        break;
    }
    case ID_USER_PROFILE: {
        const std::string id = selected_id();
        if (!id.empty())
            dispatch_cmd({{"cmd", "open_user_profile"}, {"id", id}});
        break;
    }
    case ID_NEW_TIMELINE:
        do_new_timeline();
        break;
    case ID_CLOSE_TIMELINE:
        dispatch_cmd({{"cmd", "close_timeline"}});
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
    tl.rows.clear();
    for (const auto& r : e.value("rows", json::array())) {
        Row row;
        row.id = r.value("id", std::string{});
        row.text = to_wide(r.value("text", std::string{}));
        row.favorited = r.value("favorited", false);
        row.boosted = r.value("boosted", false);
        row.gap_after = r.value("gap_after", false);
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

void MainWindow::ev_update_status(const json& e) {
    const bool silent = e.value("silent", false);
    const std::string error = e.value("error", std::string{});
    const bool available = e.value("available", false);
    const std::string branch = e.value("branch", std::string{});
    const std::string version = e.value("version", std::string{});
    const std::string notes = e.value("notes", std::string{});
    pending_update_url_ = e.value("download_url", std::string{});

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
    if (pending_update_url_.empty())
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
        dispatch_cmd({{"cmd", "download_update"}, {"url", pending_update_url_}});
    }
}

void MainWindow::ev_update_ready(const json& e) {
    const std::string zip = e.value("path", std::string{});
    if (zip.empty()) {
        announce("Update download failed.");
        return;
    }
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
    if (invisible_mode_ == "hotkey" || invisible_mode_ == "keyhook") {
        dispatch_cmd({{"cmd", "get_keymap"}}); // ev_keymap installs the active driver
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
    // Install whichever driver the active mode calls for; keep the other idle.
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

void MainWindow::ev_layer_keymap(const json& e) {
    if (invisible_mode_ != "layer")
        return;
    std::map<std::string, std::string> layer;
    const json bindings = e.value("bindings", json::object());
    for (const auto& [key, action] : bindings.items())
        layer[key] = action.get<std::string>();
    layer_enter_message_ = e.value("enter_message", std::string("FastSM layer"));
    layer_help_message_ = e.value("help_message", std::string{});
    hotkey_driver_.clear();
    keyhook_driver_.set_layer(e.value("activation", std::string("control+win+space")), layer);
    keyhook_driver_.enable();
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

void MainWindow::ev_invisible_ui_action(const json& e) {
    const std::string a = e.value("action", std::string{});
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
    } else if (a == "StopAudio") {
        if (speaker_)
            speaker_->stop();
    } else if (a == "Find") {
        do_find();
    } else if (a == "FindNext") {
        do_find_next();
    } else if (a == "FindPrev") {
        do_find_prev();
    }
}

void MainWindow::ev_compose_context(const json& e) {
    keyhook_driver_.exit_layer(); // a modal dialog is opening; leave the layer
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
    }

    auto result = show_compose_dialog(hwnd_, inst_, req);
    restore_selection(keep_id);
    if (!result)
        return;

    json draft = draft_to_json(result->draft);
    if (e.contains("reply_to_id"))
        draft["reply_to_id"] = e["reply_to_id"];
    if (e.contains("reply_to_url")) // remote reply target: resolved core-side
        draft["reply_to_url"] = e["reply_to_url"];
    if (e.contains("quoted_status_id"))
        draft["quoted_status_id"] = e["quoted_status_id"];
    json cmd = {{"cmd", "post"}, {"draft", draft}};
    if (e.contains("edit_id"))
        cmd["edit_id"] = e["edit_id"];
    dispatch_cmd(cmd);
}

void MainWindow::ev_spawnable(const json& e) {
    spawnable_kinds_.clear();
    std::vector<fastsmui::NewTimelineEntry> entries;
    for (const auto& t : e.value("timelines", json::array())) {
        spawnable_kinds_.push_back(t.value("kind", std::string{}));
        entries.push_back({to_wide(t.value("title", std::string{})),
                           to_wide(t.value("input", std::string{}))});
    }
    if (entries.empty()) {
        announce("No more timelines to add for this account.");
        return;
    }
    auto choice = show_new_timeline_dialog(hwnd_, inst_, entries);
    if (!choice || choice->index < 0 || choice->index >= static_cast<int>(spawnable_kinds_.size()))
        return;
    json cmd = {{"cmd", "spawn_timeline"},
                {"kind", spawnable_kinds_[static_cast<size_t>(choice->index)]}};
    if (!choice->value.empty())
        cmd["value"] = to_utf8(choice->value);
    dispatch_cmd(cmd);
}

void MainWindow::announce(const std::string& message) {
    // Speak via the screen reader / TTS only; the window title stays "FastSMRW"
    // (it used to mirror the last announcement, which just cluttered the title bar).
    if (speaker_ && !message.empty())
        speaker_->speak(message, true);
}

} // namespace fastsmui
