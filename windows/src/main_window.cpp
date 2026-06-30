#include "main_window.hpp"

#include <commctrl.h>

#include "../resources/resource.h"
#include "add_account_dialog.hpp"
#include "app_messages.hpp"
#include "compose_dialog.hpp"
#include "new_timeline_dialog.hpp"
#include "utf.hpp"

#include "fastsm/fastsm.hpp"
#include "fastsm/presentation/reply_helper.hpp"
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

// GetDpiForWindow is Windows 10 (1607)+. Load it dynamically so the app still
// launches on Windows 7/8 (where the static import would fail with "entry point
// not found"); there we fall back to the system DPI.
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

int dpi_scale(HWND hwnd, int value) {
    return MulDiv(value, window_dpi(hwnd), 96);
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
    AppendMenuW(status, MF_STRING, ID_QUOTE, L"&Quote\tCtrl+Shift+Q");
    AppendMenuW(status, MF_STRING, ID_POST_INFO, L"Post &Info…\tCtrl+I");
    AppendMenuW(status, MF_STRING | MF_GRAYED, ID_VIEW_THREAD, L"View &Thread");
    AppendMenuW(status, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(status, MF_STRING | MF_GRAYED, ID_USER_TIMELINE, L"Open &User Timeline");
    AppendMenuW(status, MF_STRING | MF_GRAYED, ID_USER_PROFILE, L"Open User &Profile\tCtrl+U");
    AppendMenuW(status, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(status, MF_STRING | MF_GRAYED, ID_OPEN_BROWSER, L"Open in Browser");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(status), L"&Status");

    HMENU timeline = CreatePopupMenu();
    AppendMenuW(timeline, MF_STRING, ID_NEW_TIMELINE, L"&New Timeline…\tCtrl+T");
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
        {FVIRTKEY | FCONTROL, 'T', ID_NEW_TIMELINE},
        {FVIRTKEY | FCONTROL, 'Q', ID_QUIT},
        {FVIRTKEY | FCONTROL, 'I', ID_POST_INFO},
        {FVIRTKEY | FCONTROL | FSHIFT, 'A', ID_ADD_ACCOUNT},
        {FVIRTKEY | FCONTROL | FSHIFT, 'B', ID_BOOST},
        {FVIRTKEY | FCONTROL | FSHIFT, 'D', ID_FAVORITE},
        {FVIRTKEY | FCONTROL | FSHIFT, 'Q', ID_QUOTE},
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
            } else if (hdr->code == LVN_ITEMCHANGED || hdr->code == LVN_ODSTATECHANGED) {
                // Remember the focused row per-timeline (position memory). A
                // virtual list reports focus via either notification, so query
                // the focused item rather than trust the struct. (No per-row
                // earcon: like the Mac app, row movement is conveyed by the
                // screen reader, not a "navigate" sound, which is silent.)
                if (!updating_selection_ && app_ && app_->current()) {
                    const int focused = ListView_GetNextItem(timeline_view_, -1, LVNI_FOCUSED);
                    const auto& items = app_->current()->items();
                    if (focused >= 0 && focused < static_cast<int>(items.size()))
                        app_->current()->note_selection(items[static_cast<size_t>(focused)].id());
                }
            }
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

    // Build the current row-id list and only reload when it actually changes
    // (the Mac reloads on `ids != renderedIDs`). This stops non-structural
    // updates (e.g. a favorite toggle) from disturbing the selection/scroll.
    std::vector<std::string> ids;
    if (tc) {
        const auto& items = tc->items();
        ids.reserve(items.size());
        for (const auto& it : items)
            ids.push_back(it.id());
    }
    if (ids == rendered_ids_)
        return;
    rendered_ids_ = ids;

    const int count = static_cast<int>(ids.size());
    updating_selection_ = true;
    ListView_SetItemCountEx(timeline_view_, count, LVSICF_NOSCROLL);
    if (tc && count > 0) {
        // Restore to the remembered post; if it's gone (or none yet), adopt the
        // top row as the position so future incoming posts track it instead of
        // a fixed index.
        int idx = tc->visible_index_of(tc->selected_id());
        if (idx < 0) {
            idx = 0;
            tc->note_selection(tc->items()[0].id());
        }
        // Only move the selection when it differs, to avoid a redundant
        // screen-reader re-announcement.
        if (selected_row() != idx)
            ListView_SetItemState(timeline_view_, idx, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(timeline_view_, idx, FALSE);
    }
    updating_selection_ = false;
    InvalidateRect(timeline_view_, nullptr, FALSE);
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
    case 'Q':
        do_quote();
        break;
    case 'E':
        do_edit();
        break;
    case VK_DELETE:
        // Close a spawned (dismissable) timeline.
        if (app_ && app_->close_current_timeline() && app_->sound())
            app_->sound()->play(sound::Earcon::Close);
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

const Status* MainWindow::selected_status() const {
    TimelineController* tc = app_ ? app_->current() : nullptr;
    const int row = selected_row();
    if (!tc || row < 0)
        return nullptr;
    const auto& items = tc->items();
    if (row >= static_cast<int>(items.size()))
        return nullptr;
    return items[static_cast<size_t>(row)].actionable_status();
}

void MainWindow::present_compose(ComposeMode mode, const Status* target) {
    TimelineController* tc = app_ ? app_->current() : nullptr;
    if (!tc || !tc->account()) {
        announce("Add an account before posting.");
        return;
    }
    SocialAccount* account = tc->account();

    ComposeRequest req;
    req.mode = mode;
    req.features = account->features();
    req.max_chars = account->max_chars();
    req.enter_to_send = enter_to_send_;

    std::string reply_to_id, quoted_status_id, edit_id;
    if (mode == ComposeMode::Reply && target) {
        req.title = L"Reply";
        req.context_label = "Replying to " + target->account.best_name() + ": " + target->text;
        if (account->platform() == Platform::Mastodon)
            req.prefill_text = present::mention_prefix(*target, account->me());
        // Inherit the original post's attributes: visibility + content warning.
        req.default_visibility = target->visibility;
        if (target->spoiler_text)
            req.prefill_cw = *target->spoiler_text;
        reply_to_id = target->id;
    } else if (mode == ComposeMode::Quote && target) {
        req.title = L"Quote Post";
        req.context_label = "Quoting " + target->account.best_name() + ": " + target->text;
        quoted_status_id = target->id;
    } else if (mode == ComposeMode::Edit && target) {
        req.title = L"Edit Post";
        req.prefill_text = target->text;
        if (target->spoiler_text)
            req.prefill_cw = *target->spoiler_text;
        edit_id = target->id;
    } else {
        req.title = L"New Post";
    }

    auto result = show_compose_dialog(hwnd_, inst_, req);
    if (!result)
        return;
    PostDraft draft = std::move(result->draft);
    if (!reply_to_id.empty())
        draft.reply_to_id = reply_to_id;
    if (!quoted_status_id.empty())
        draft.quoted_status_id = quoted_status_id;

    auto done = [this](bool ok) {
        if (app_ && app_->sound())
            app_->sound()->play(ok ? sound::Earcon::PostSent : sound::Earcon::Error);
    };
    if (mode == ComposeMode::Edit && !edit_id.empty())
        tc->edit_post(edit_id, draft, done);
    else
        tc->post(draft, done);
}

void MainWindow::do_new_post() { present_compose(ComposeMode::New, nullptr); }

void MainWindow::do_reply() {
    if (const Status* s = selected_status())
        present_compose(ComposeMode::Reply, s);
}

void MainWindow::do_quote() {
    if (const Status* s = selected_status())
        present_compose(ComposeMode::Quote, s);
}

void MainWindow::do_edit() {
    const Status* s = selected_status();
    TimelineController* tc = app_ ? app_->current() : nullptr;
    if (!s || !tc || !tc->account())
        return;
    if (s->account.id != tc->account()->me().id) {
        announce("You can only edit your own posts.");
        return;
    }
    present_compose(ComposeMode::Edit, s);
}

void MainWindow::do_new_timeline() {
    if (!app_)
        return;
    const auto sources = app_->spawnable_timelines();
    if (sources.empty()) {
        announce("No more timelines to add for this account.");
        return;
    }
    std::vector<std::wstring> titles;
    titles.reserve(sources.size());
    for (const auto& s : sources)
        titles.push_back(to_wide(s.title()));
    if (auto choice = show_new_timeline_dialog(hwnd_, inst_, titles))
        if (*choice >= 0 && *choice < static_cast<int>(sources.size()))
            app_->spawn_timeline(sources[static_cast<size_t>(*choice)]);
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
    case ID_QUOTE:
        do_quote();
        break;
    case ID_POST_INFO:
        do_post_info();
        break;
    case ID_NEW_TIMELINE:
        do_new_timeline();
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
