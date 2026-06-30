#include "main_window.hpp"

#include <commctrl.h>
#include <shellapi.h>

#include "../resources/resource.h"
#include "add_account_dialog.hpp"
#include "app_messages.hpp"
#include "new_timeline_dialog.hpp"
#include "post_info_dialog.hpp"
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
    AppendMenuW(status, MF_STRING | MF_GRAYED, ID_USER_TIMELINE, L"Open &User Timeline");
    AppendMenuW(status, MF_STRING | MF_GRAYED, ID_USER_PROFILE, L"Open User &Profile\tCtrl+U");
    AppendMenuW(status, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(status, MF_STRING, ID_OPEN_BROWSER, L"Open in Browser");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(status), L"&Status");

    HMENU timeline = CreatePopupMenu();
    AppendMenuW(timeline, MF_STRING, ID_NEW_TIMELINE, L"&New Timeline…\tCtrl+T");
    AppendMenuW(timeline, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(timeline, MF_STRING, ID_CLEAR_TIMELINE, L"&Clear Timeline\tCtrl+Backspace");
    AppendMenuW(timeline, MF_STRING | MF_GRAYED, ID_CLEAR_ALL, L"Clear &All Timelines");
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
        {FVIRTKEY | FCONTROL, VK_BACK, ID_CLEAR_TIMELINE},
        {FVIRTKEY | FCONTROL, 'Z', ID_GO_BACK},
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

    case WM_SETFOCUS:
        SetFocus(timeline_view_);
        return 0;

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
                    }
                }
            }
        } else if (hdr->hwndFrom == timelines_list_) {
            if (hdr->code == LVN_ITEMCHANGED && !updating_selection_) {
                auto* nm = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((nm->uChanged & LVIF_STATE) && (nm->uNewState & LVIS_SELECTED) &&
                    !(nm->uOldState & LVIS_SELECTED))
                    dispatch_cmd({{"cmd", "select_timeline"}, {"index", nm->iItem}});
            } else if (hdr->code == LVN_KEYDOWN) {
                // Delete on the timelines list closes the selected (current) timeline.
                if (reinterpret_cast<NMLVKEYDOWN*>(lp)->wVKey == VK_DELETE)
                    dispatch_cmd({{"cmd", "close_timeline"}});
            }
        }
        return 0;
    }

    case WM_DESTROY:
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

void MainWindow::bind_current_to_view(bool force) {
    Timeline* tc = current();
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
    case VK_RETURN:
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
    }
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
    if (auto result = show_settings_dialog(hwnd_, inst_, s, packs))
        dispatch_cmd({{"cmd", "update_settings"}, {"settings", store::settings_to_json(*result)}});
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
    case ID_NEW_TIMELINE:
        do_new_timeline();
        break;
    case ID_CLEAR_TIMELINE:
        if (!settings_.value("confirm_clear_timeline", true) ||
            confirm(hwnd_, L"Clear this timeline? This removes the loaded posts and its cache.",
                    L"Clear Timeline"))
            dispatch_cmd({{"cmd", "clear_timeline"}});
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
    else if (ev == "select_row")
        restore_selection(e.value("id", std::string{}));
    else if (ev == "open_url")
        ShellExecuteW(nullptr, L"open", to_wide(e.value("url", std::string{})).c_str(), nullptr,
                      nullptr, SW_SHOW);
    // accounts_changed / auth_result / post_result: nothing extra (sounds + any
    // announce come from the core).
}

void MainWindow::ev_timelines_changed(const json& e) {
    const size_t prev_count = timelines_.size();
    std::vector<Timeline> next;
    for (const auto& t : e.value("timelines", json::array())) {
        Timeline tl;
        tl.title = to_wide(t.value("title", std::string{}));
        tl.kind = t.value("kind", std::string{});
        tl.dismissable = t.value("dismissable", false);
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
        tl.rows.push_back(std::move(row));
    }
    if (tl.selected_id.empty()) // first load: adopt the core's remembered position
        tl.selected_id = e.value("selected_id", std::string{});
    if (index == current_)
        bind_current_to_view();
}

void MainWindow::ev_settings(const json& e) {
    settings_ = e.value("settings", json::object());
    soundpacks_.clear();
    for (const auto& p : e.value("soundpacks", json::array()))
        soundpacks_.push_back(p.get<std::string>());
}

void MainWindow::ev_compose_context(const json& e) {
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
    if (e.contains("quoted_status_id"))
        draft["quoted_status_id"] = e["quoted_status_id"];
    json cmd = {{"cmd", "post"}, {"draft", draft}};
    if (e.contains("edit_id"))
        cmd["edit_id"] = e["edit_id"];
    dispatch_cmd(cmd);
}

void MainWindow::ev_spawnable(const json& e) {
    spawnable_kinds_.clear();
    std::vector<std::wstring> titles;
    for (const auto& t : e.value("timelines", json::array())) {
        spawnable_kinds_.push_back(t.value("kind", std::string{}));
        titles.push_back(to_wide(t.value("title", std::string{})));
    }
    if (titles.empty()) {
        announce("No more timelines to add for this account.");
        return;
    }
    auto idx = show_new_timeline_dialog(hwnd_, inst_, titles);
    if (idx && *idx >= 0 && *idx < static_cast<int>(spawnable_kinds_.size()))
        dispatch_cmd({{"cmd", "spawn_timeline"}, {"kind", spawnable_kinds_[static_cast<size_t>(*idx)]}});
}

void MainWindow::announce(const std::string& message) {
    std::wstring title = L"FastSMRW";
    if (!message.empty()) {
        title += L" — ";
        title += to_wide(message);
    }
    SetWindowTextW(hwnd_, title.c_str());
    if (speaker_ && !message.empty())
        speaker_->speak(message, true);
}

} // namespace fastsmui
