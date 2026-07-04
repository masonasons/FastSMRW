#include "post_info_dialog.hpp"

#include <commctrl.h>

#include "edit_util.hpp"

#include "../resources/resource.h"

namespace fastsmui {
namespace {

// Dynamically-created poll controls use these local ids (they aren't in the .rc).
constexpr int kPollListId = 1500;
constexpr int kVoteBtnId = 1501;

struct Ctx {
    const std::wstring* text;
    bool quote_ok;
    bool browser_ok;
    bool is_mine; // your own post -> the Delete button is shown
    const PollInfo* poll;
    HWND poll_list = nullptr; // LISTBOX (single) or checklist LISTVIEW (multi)
    PostInfoResult result;
};

// Read the chosen option indexes out of whichever poll control we created.
std::vector<int> collect_choices(const Ctx* c) {
    std::vector<int> out;
    if (!c->poll_list || !c->poll)
        return out;
    if (c->poll->multiple) {
        const int n = ListView_GetItemCount(c->poll_list);
        for (int i = 0; i < n; ++i)
            if (ListView_GetCheckState(c->poll_list, i))
                out.push_back(i);
    } else {
        const int sel = static_cast<int>(SendMessageW(c->poll_list, LB_GETCURSEL, 0, 0));
        if (sel >= 0)
            out.push_back(sel);
    }
    return out;
}

// Build the voting control (list box / checklist) and Vote button in the space
// freed by shrinking the read-only text field.
void build_poll_controls(HWND dlg, Ctx* c) {
    if (!c->poll || !c->poll->present || c->poll->options.empty())
        return;
    HWND hText = GetDlgItem(dlg, IDC_POSTINFO_TEXT);
    RECT tr;
    GetWindowRect(hText, &tr);
    MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&tr), 2);
    const int left = tr.left;
    const int right = tr.right;
    const int fullH = tr.bottom - tr.top;
    const int textH = fullH * 45 / 100;
    const int gap = 6;
    MoveWindow(hText, left, tr.top, right - left, textH, TRUE);
    const int pollTop = tr.top + textH + gap;
    const int pollBottom = tr.bottom;
    const int voteW = 60;
    const int voteH = 22;
    const int listW = (right - left) - voteW - gap;
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dlg, GWLP_HINSTANCE));

    if (c->poll->multiple) {
        HWND lv = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL,
            left, pollTop, listW, pollBottom - pollTop, dlg,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPollListId)), inst, nullptr);
        ListView_SetExtendedListViewStyle(lv, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        LVCOLUMNW col{};
        col.mask = LVCF_WIDTH;
        col.cx = listW - 24;
        ListView_InsertColumn(lv, 0, &col);
        for (size_t i = 0; i < c->poll->options.size(); ++i) {
            LVITEMW it{};
            it.mask = LVIF_TEXT;
            it.iItem = static_cast<int>(i);
            it.pszText = const_cast<wchar_t*>(c->poll->options[i].c_str());
            ListView_InsertItem(lv, &it);
        }
        c->poll_list = lv;
    } else {
        HWND lb = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS, left,
            pollTop, listW, pollBottom - pollTop, dlg,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPollListId)), inst, nullptr);
        for (const auto& o : c->poll->options)
            SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(o.c_str()));
        SendMessageW(lb, LB_SETCURSEL, 0, 0);
        c->poll_list = lb;
    }
    HWND vote =
        CreateWindowExW(0, L"BUTTON", L"&Vote", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        left + listW + gap, pollTop, voteW, voteH, dlg,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kVoteBtnId)), inst, nullptr);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0));
    SendMessageW(c->poll_list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(vote, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    // Tab order: poll list, then Vote, right after the text field.
    SetWindowPos(c->poll_list, hText, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(vote, c->poll_list, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    // Enter votes (the default push button becomes Vote while a poll is open).
    SendMessageW(dlg, DM_SETDEFID, kVoteBtnId, 0);
}

INT_PTR CALLBACK Proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        auto* c = reinterpret_cast<Ctx*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(c));
        // Win32 multiline EDIT needs CRLF line breaks; the core composes with LF.
        std::wstring crlf;
        crlf.reserve(c->text->size() + 16);
        for (wchar_t ch : *c->text) {
            if (ch == L'\n')
                crlf += L"\r\n";
            else
                crlf += ch;
        }
        SetDlgItemTextW(dlg, IDC_POSTINFO_TEXT, crlf.c_str());
        enable_edit_select_all(GetDlgItem(dlg, IDC_POSTINFO_TEXT)); // Ctrl+A select-all
        EnableWindow(GetDlgItem(dlg, IDC_POSTINFO_QUOTE), c->quote_ok);
        EnableWindow(GetDlgItem(dlg, IDC_POSTINFO_BROWSER), c->browser_ok);
        // Delete is only for your own posts; hide it entirely otherwise.
        if (!c->is_mine)
            ShowWindow(GetDlgItem(dlg, IDC_POSTINFO_DELETE), SW_HIDE);
        build_poll_controls(dlg, c);
        SetFocus(GetDlgItem(dlg, IDC_POSTINFO_TEXT)); // read the post immediately
        return FALSE;                                 // focus set explicitly
    }
    case WM_COMMAND: {
        auto* c = reinterpret_cast<Ctx*>(GetWindowLongPtrW(dlg, DWLP_USER));
        auto finish = [&](PostInfoAction a) {
            if (c)
                c->result.action = a;
            EndDialog(dlg, 1);
        };
        switch (LOWORD(wp)) {
        case kVoteBtnId: {
            if (!c)
                return TRUE;
            std::vector<int> choices = collect_choices(c);
            if (choices.empty()) {
                MessageBeep(MB_ICONWARNING); // nothing selected
                return TRUE;
            }
            c->result.action = PostInfoAction::Vote;
            c->result.choices = std::move(choices);
            EndDialog(dlg, 1);
            return TRUE;
        }
        case IDC_POSTINFO_REPLY:
            finish(PostInfoAction::Reply);
            return TRUE;
        case IDC_POSTINFO_BOOST:
            finish(PostInfoAction::Boost);
            return TRUE;
        case IDC_POSTINFO_FAVORITE:
            finish(PostInfoAction::Favorite);
            return TRUE;
        case IDC_POSTINFO_QUOTE:
            finish(PostInfoAction::Quote);
            return TRUE;
        case IDC_POSTINFO_BROWSER:
            finish(PostInfoAction::OpenBrowser);
            return TRUE;
        case IDC_POSTINFO_LINKS:
            finish(PostInfoAction::OpenLinks);
            return TRUE;
        case IDC_POSTINFO_THREAD:
            finish(PostInfoAction::ViewThread);
            return TRUE;
        case IDC_POSTINFO_AUTHOR:
            finish(PostInfoAction::ViewAuthor);
            return TRUE;
        case IDC_POSTINFO_DELETE:
            finish(PostInfoAction::Delete);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, 0);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

} // namespace

PostInfoResult show_post_info_dialog(HWND parent, HINSTANCE inst, const std::wstring& text,
                                     bool quote_ok, bool browser_ok, bool is_mine,
                                     const PollInfo& poll) {
    Ctx ctx{&text, quote_ok, browser_ok, is_mine, &poll, nullptr, {}};
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_POST_INFO), parent, Proc,
                    reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

} // namespace fastsmui
