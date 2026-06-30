#include "main_window.hpp"

#include <commctrl.h>

#include "../resources/resource.h"
#include "add_account_dialog.hpp"
#include "app_messages.hpp"
#include "compose_dialog.hpp"
#include "utf.hpp"

#include "fastsm/fastsm.hpp"
#include "fastsm/presentation/status_presenter.hpp"
#include "fastsm/util/date_parsing.hpp"

using namespace fastsm;

namespace fastsmui {
namespace {

constexpr wchar_t kClassName[] = L"FastSMRWMain";
constexpr int kTimelinesPaneWidth = 220;
constexpr int kMinWidth = 920;
constexpr int kMinHeight = 720;

// Command ids.
enum {
    ID_ABOUT = 40010,
    ID_SETTINGS,
    ID_QUIT,
    ID_NEW_POST,
    ID_REFRESH,
    ID_CLOSE,
    ID_CUT,
    ID_COPY,
    ID_PASTE,
    ID_SELECT_ALL,
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
    ID_PREV_ACCOUNT,
    ID_NEXT_ACCOUNT,
    ID_ADD_ACCOUNT,
    ID_GOTO_TIMELINE_1 = 40100, // .. +8 for timelines 1-9
};

int dpi_scale(HWND hwnd, int value) {
    return MulDiv(value, static_cast<int>(GetDpiForWindow(hwnd)), 96);
}

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

// Mirrors the Mac main menu (MainMenu.swift). The "App menu" is renamed
// "Application". Items for features not yet implemented are present but grayed,
// so the structure matches and stays discoverable.
HMENU build_menu() {
    HMENU bar = CreateMenu();

    HMENU app = CreatePopupMenu();
    AppendMenuW(app, MF_STRING, ID_ABOUT, L"&About FastSMRW");
    AppendMenuW(app, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(app, MF_STRING | MF_GRAYED, ID_SETTINGS, L"&Settings…\tCtrl+,");
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
    AppendMenuW(status, MF_STRING | MF_GRAYED, ID_QUOTE, L"&Quote\tCtrl+Shift+Q");
    AppendMenuW(status, MF_STRING, ID_POST_INFO, L"Post &Info…\tCtrl+I");
    AppendMenuW(status, MF_STRING | MF_GRAYED, ID_VIEW_THREAD, L"View &Thread");
    AppendMenuW(status, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(status, MF_STRING | MF_GRAYED, ID_USER_TIMELINE, L"Open &User Timeline");
    AppendMenuW(status, MF_STRING | MF_GRAYED, ID_USER_PROFILE, L"Open User &Profile\tCtrl+U");
    AppendMenuW(status, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(status, MF_STRING | MF_GRAYED, ID_OPEN_BROWSER, L"Open in Browser");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(status), L"&Status");

    HMENU timeline = CreatePopupMenu();
    AppendMenuW(timeline, MF_STRING | MF_GRAYED, ID_NEW_TIMELINE, L"&New Timeline…\tCtrl+T");
    AppendMenuW(timeline, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(timeline, MF_STRING, ID_CLEAR_TIMELINE, L"&Clear Timeline\tCtrl+Backspace");
    AppendMenuW(timeline, MF_STRING | MF_GRAYED, ID_CLEAR_ALL, L"Clear &All Timelines");
    AppendMenuW(timeline, MF_STRING | MF_GRAYED, ID_GO_BACK, L"Go &Back\tCtrl+Z");
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

MainWindow::MainWindow(HINSTANCE inst, WinExecutor* exec) : inst_(inst), exec_(exec) {}

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
        {FVIRTKEY | FCONTROL, 'Q', ID_QUIT},
        {FVIRTKEY | FCONTROL, 'I', ID_POST_INFO},
        {FVIRTKEY | FCONTROL | FSHIFT, 'A', ID_ADD_ACCOUNT},
        {FVIRTKEY | FCONTROL | FSHIFT, 'B', ID_BOOST},
        {FVIRTKEY | FCONTROL | FSHIFT, 'D', ID_FAVORITE},
        {FVIRTKEY | FCONTROL, VK_OEM_4, ID_PREV_ACCOUNT}, // Ctrl+[
        {FVIRTKEY | FCONTROL, VK_OEM_6, ID_NEXT_ACCOUNT}, // Ctrl+]
        {FVIRTKEY | FCONTROL, VK_BACK, ID_CLEAR_TIMELINE},
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

    case WM_APP_DISPATCH:
        if (exec_)
            exec_->drain();
        return 0;

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
                TimelineController* tc = app_ ? app_->current() : nullptr;
                if (tc && (di->item.mask & LVIF_TEXT)) {
                    const auto& items = tc->items();
                    const int idx = di->item.iItem;
                    if (idx >= 0 && idx < static_cast<int>(items.size())) {
                        scratch_ = to_wide(
                            present::compact_line(items[static_cast<size_t>(idx)], util::now_unix()));
                        di->item.pszText = scratch_.data();
                    }
                }
            } else if (hdr->code == LVN_KEYDOWN) {
                auto* kd = reinterpret_cast<NMLVKEYDOWN*>(lp);
                on_view_keydown(kd->wVKey);
            }
            // No per-row earcon: like the Mac app, row movement is conveyed by
            // the screen reader, not a "navigate" sound (which is silent).
        } else if (hdr->hwndFrom == timelines_list_) {
            if (hdr->code == LVN_ITEMCHANGED && !updating_selection_) {
                auto* nm = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((nm->uChanged & LVIF_STATE) && (nm->uNewState & LVIS_SELECTED) &&
                    !(nm->uOldState & LVIS_SELECTED)) {
                    if (app_)
                        app_->select_timeline(nm->iItem);
                }
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

void MainWindow::populate_timelines_list() {
    updating_selection_ = true;
    ListView_DeleteAllItems(timelines_list_);
    if (app_) {
        const auto timelines = app_->timelines();
        for (size_t i = 0; i < timelines.size(); ++i) {
            const std::wstring title = to_wide(timelines[i]->source().title());
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(i);
            item.pszText = const_cast<wchar_t*>(title.c_str());
            ListView_InsertItem(timelines_list_, &item);
        }
        const int cur = app_->current_index();
        if (cur >= 0 && cur < static_cast<int>(timelines.size()))
            ListView_SetItemState(timelines_list_, cur, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
    }
    updating_selection_ = false;
}

void MainWindow::bind_current_to_view() {
    TimelineController* tc = app_ ? app_->current() : nullptr;
    const int count = tc ? static_cast<int>(tc->items().size()) : 0;
    ListView_SetItemCountEx(timeline_view_, count, LVSICF_NOSCROLL);
    InvalidateRect(timeline_view_, nullptr, FALSE);
    if (count > 0 && selected_row() < 0)
        ListView_SetItemState(timeline_view_, 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
}

int MainWindow::selected_row() const {
    return ListView_GetNextItem(timeline_view_, -1, LVNI_FOCUSED);
}

void MainWindow::cycle_focus() {
    HWND focus = GetFocus();
    SetFocus(focus == timeline_view_ ? timelines_list_ : timeline_view_);
}

void MainWindow::on_view_keydown(int vk) {
    switch (vk) {
    case VK_UP:
    case VK_DOWN: {
        // Boundary earcon when trying to move past the top/bottom (the native
        // list won't move; the screen reader stays silent otherwise).
        TimelineController* tc = app_ ? app_->current() : nullptr;
        const int count = tc ? static_cast<int>(tc->items().size()) : 0;
        const int row = selected_row();
        const bool at_edge = (vk == VK_UP && row <= 0) || (vk == VK_DOWN && row >= count - 1);
        if (at_edge && count > 0 && app_ && app_->sound())
            app_->sound()->play(sound::Earcon::Boundary);
        break;
    }
    case VK_LEFT:
        if (app_)
            app_->previous_timeline();
        break;
    case VK_RIGHT:
        if (app_)
            app_->next_timeline();
        break;
    case 'B':
        do_boost();
        break;
    case 'F':
        do_favorite();
        break;
    case 'R':
        do_reply();
        break;
    default:
        break;
    }
}

void MainWindow::do_boost() {
    TimelineController* tc = app_ ? app_->current() : nullptr;
    const int row = selected_row();
    if (!tc || row < 0)
        return;
    const bool now_boosted = tc->toggle_boost(row);
    if (now_boosted && app_ && app_->sound())
        app_->sound()->play(sound::Earcon::Boost);
}

void MainWindow::do_favorite() {
    TimelineController* tc = app_ ? app_->current() : nullptr;
    const int row = selected_row();
    if (!tc || row < 0)
        return;
    const bool now_fav = tc->toggle_favorite(row);
    if (app_ && app_->sound())
        app_->sound()->play(now_fav ? sound::Earcon::Favorite : sound::Earcon::Unfavorite);
}

void MainWindow::do_reply() {
    TimelineController* tc = app_ ? app_->current() : nullptr;
    const int row = selected_row();
    if (!tc || row < 0)
        return;
    const auto& items = tc->items();
    if (row >= static_cast<int>(items.size()))
        return;
    const Status* s = items[static_cast<size_t>(row)].actionable_status();
    if (!s)
        return;

    std::wstring context = L"Replying to " + to_wide(s->account.best_name());
    auto text = show_compose_dialog(hwnd_, inst_, L"Reply", context);
    if (text) {
        PostDraft draft;
        draft.text = *text;
        draft.reply_to_id = s->id;
        tc->post(draft, [this](bool ok) {
            if (app_ && app_->sound())
                app_->sound()->play(ok ? sound::Earcon::PostSent : sound::Earcon::Error);
        });
    }
}

void MainWindow::do_new_post() {
    TimelineController* tc = app_ ? app_->current() : nullptr;
    if (!tc) {
        announce("Add an account before posting.");
        return;
    }
    auto text = show_compose_dialog(hwnd_, inst_, L"New Post", L"");
    if (text) {
        PostDraft draft;
        draft.text = *text;
        tc->post(draft, [this](bool ok) {
            if (app_ && app_->sound())
                app_->sound()->play(ok ? sound::Earcon::PostSent : sound::Earcon::Error);
        });
    }
}

void MainWindow::do_add_account() {
    auto data = show_add_account_dialog(hwnd_, inst_);
    if (!data || !app_)
        return;
    auto done = [this](bool ok, std::string err) {
        announce(ok ? "Account added." : ("Add account failed: " + err));
    };
    if (data->platform == 0)
        app_->add_mastodon(data->service, done);
    else
        app_->add_bluesky(data->service, data->handle, data->app_password, done);
    announce(data->platform == 0 ? "Authorizing in your browser..." : "Signing in...");
}

void MainWindow::about() {
    std::wstring text = L"FastSMRW\r\nA fast, accessible Mastodon/Bluesky client.\r\nVersion ";
    for (const char* p = fastsm::version(); *p; ++p)
        text.push_back(static_cast<wchar_t>(*p));
    MessageBoxW(hwnd_, text.c_str(), L"About FastSMRW", MB_OK | MB_ICONINFORMATION);
}

void MainWindow::do_post_info() {
    TimelineController* tc = app_ ? app_->current() : nullptr;
    const int row = selected_row();
    if (!tc || row < 0)
        return;
    const auto& items = tc->items();
    if (row >= static_cast<int>(items.size()))
        return;
    const std::wstring label =
        to_wide(present::accessibility_label(items[static_cast<size_t>(row)], util::now_unix()));
    MessageBoxW(hwnd_, label.c_str(), L"Post Info", MB_OK);
}

void MainWindow::handle_command(int id) {
    if (id >= ID_GOTO_TIMELINE_1 && id <= ID_GOTO_TIMELINE_1 + 8) {
        if (app_)
            app_->select_timeline(id - ID_GOTO_TIMELINE_1);
        return;
    }
    switch (id) {
    case ID_NEW_POST:
        do_new_post();
        break;
    case ID_REFRESH:
        if (app_ && app_->current())
            app_->current()->refresh();
        break;
    case ID_QUIT:
        DestroyWindow(hwnd_);
        break;
    case ID_ABOUT:
        about();
        break;
    case ID_ADD_ACCOUNT:
        do_add_account();
        break;
    case ID_REPLY:
        do_reply();
        break;
    case ID_BOOST:
        do_boost();
        break;
    case ID_FAVORITE:
        do_favorite();
        break;
    case ID_POST_INFO:
        do_post_info();
        break;
    case ID_CLEAR_TIMELINE:
        if (app_ && app_->current()) {
            app_->current()->clear();
            if (app_->sound())
                app_->sound()->play(sound::Earcon::Delete);
        }
        break;
    case ID_PREV_ACCOUNT:
        if (app_)
            app_->previous_account();
        break;
    case ID_NEXT_ACCOUNT:
        if (app_)
            app_->next_account();
        break;
    default:
        break;
    }
}

// --- AppView ---

void MainWindow::timelines_rebuilt() {
    populate_timelines_list();
    bind_current_to_view();
}

void MainWindow::current_timeline_changed() {
    updating_selection_ = true;
    const int cur = app_ ? app_->current_index() : -1;
    if (cur >= 0)
        ListView_SetItemState(timelines_list_, cur, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    updating_selection_ = false;
    bind_current_to_view();
    if (TimelineController* tc = app_ ? app_->current() : nullptr)
        announce(tc->source().title());
}

void MainWindow::timeline_updated(TimelineController* tc) {
    if (app_ && tc == app_->current())
        bind_current_to_view();
}

void MainWindow::announce(const std::string& message) {
    std::wstring title = L"FastSMRW";
    if (!message.empty())
        title += L" \x2014 " + to_wide(message);
    SetWindowTextW(hwnd_, title.c_str());
    if (speaker_ && !message.empty())
        speaker_->speak(message, true); // spoken when a speech backend is present
}

} // namespace fastsmui
