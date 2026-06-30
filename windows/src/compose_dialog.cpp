#include "compose_dialog.hpp"

#include <commctrl.h>

#include <string>

#include "../resources/resource.h"
#include "utf.hpp"

#include "fastsm/util/languages.hpp"

using namespace fastsm;

namespace fastsmui {
namespace {

struct DurationItem {
    const wchar_t* label;
    int seconds;
};
constexpr DurationItem kDurations[] = {
    {L"5 minutes", 300},     {L"30 minutes", 1800}, {L"1 hour", 3600},   {L"6 hours", 21600},
    {L"1 day", 86400},       {L"3 days", 259200},   {L"7 days", 604800},
};

struct Ctx {
    const ComposeRequest* req;
    ComposeResult result;
    bool ok = false;
    bool eat_char = false; // swallow the WM_CHAR after we handled a Return
};

void show(HWND dlg, int id, bool visible) {
    ShowWindow(GetDlgItem(dlg, id), visible ? SW_SHOW : SW_HIDE);
}

std::string text_of(HWND ctrl) {
    const int len = GetWindowTextLengthW(ctrl);
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(ctrl, buf.data(), len + 1);
    buf.resize(static_cast<size_t>(got));
    return to_utf8(buf);
}

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos)
        return {};
    const size_t b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

bool poll_active(HWND dlg) {
    return IsWindowVisible(GetDlgItem(dlg, IDC_COMPOSE_POLL)) &&
           SendDlgItemMessageW(dlg, IDC_COMPOSE_POLL, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

int poll_option_count(HWND dlg) {
    int n = 0;
    for (int id : {IDC_COMPOSE_POLL_OPT1, IDC_COMPOSE_POLL_OPT2, IDC_COMPOSE_POLL_OPT3,
                   IDC_COMPOSE_POLL_OPT4})
        if (!trim(text_of(GetDlgItem(dlg, id))).empty())
            ++n;
    return n;
}

bool poll_valid(HWND dlg) { return !poll_active(dlg) || poll_option_count(dlg) >= 2; }

void update_counter(HWND dlg, Ctx* ctx) {
    const int len = GetWindowTextLengthW(GetDlgItem(dlg, IDC_COMPOSE_EDIT));
    const int remaining = ctx->req->max_chars - len;
    SetDlgItemTextW(dlg, IDC_COMPOSE_COUNTER, std::to_wstring(remaining).c_str());

    const std::string body = trim(text_of(GetDlgItem(dlg, IDC_COMPOSE_EDIT)));
    const bool ok = remaining >= 0 && !body.empty() && poll_valid(dlg);
    EnableWindow(GetDlgItem(dlg, IDOK), ok);
}

void toggle_poll(HWND dlg, Ctx* ctx) {
    const bool on = SendDlgItemMessageW(dlg, IDC_COMPOSE_POLL, BM_GETCHECK, 0, 0) == BST_CHECKED;
    for (int id : {IDC_COMPOSE_POLL_OPT1, IDC_COMPOSE_POLL_OPT2, IDC_COMPOSE_POLL_OPT3,
                   IDC_COMPOSE_POLL_OPT4, IDC_COMPOSE_POLL_MULTI, IDC_COMPOSE_DUR_LABEL,
                   IDC_COMPOSE_DURATION})
        show(dlg, id, on);
    update_counter(dlg, ctx);
}

void toggle_schedule(HWND dlg) {
    const bool on =
        SendDlgItemMessageW(dlg, IDC_COMPOSE_SCHEDULE, BM_GETCHECK, 0, 0) == BST_CHECKED;
    show(dlg, IDC_COMPOSE_SCHED_TIME, on);
    SetDlgItemTextW(dlg, IDOK, on ? L"&Schedule" : L"&Post");
}

std::int64_t systemtime_to_unix(const SYSTEMTIME& local) {
    SYSTEMTIME utc{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc))
        utc = local;
    FILETIME ft{};
    if (!SystemTimeToFileTime(&utc, &ft))
        return 0;
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<std::int64_t>(u.QuadPart / 10000000ULL) - 11644473600LL;
}

// Subclass the multiline edit to honor the enter-to-send preference.
LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    Ctx* ctx = reinterpret_cast<Ctx*>(ref);
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool send = ctx->req->enter_to_send ? !ctrl : ctrl;
        if (send) {
            PostMessageW(GetParent(h), WM_COMMAND, IDOK, 0);
            ctx->eat_char = true;
            return 0;
        }
        if (ctx->req->enter_to_send && ctrl) {
            SendMessageW(h, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\r\n"));
            ctx->eat_char = true;
            return 0;
        }
        // else: plain Return inserts a newline (ES_WANTRETURN default).
    } else if (msg == WM_CHAR && ctx->eat_char) {
        ctx->eat_char = false;
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

INT_PTR CALLBACK Proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, static_cast<LONG_PTR>(lp));
        auto* ctx = reinterpret_cast<Ctx*>(lp);
        const ComposeRequest& req = *ctx->req;
        const bool editing = req.mode == ComposeMode::Edit;

        SetWindowTextW(dlg, req.title.c_str());

        // Context (reply/quote).
        const bool has_context =
            (req.mode == ComposeMode::Reply || req.mode == ComposeMode::Quote) &&
            !req.context_label.empty();
        if (has_context)
            SetDlgItemTextW(dlg, IDC_COMPOSE_CONTEXT, to_wide(req.context_label).c_str());
        show(dlg, IDC_COMPOSE_CONTEXT, has_context);

        // Content warning.
        const bool cw = req.features.content_warning;
        show(dlg, IDC_COMPOSE_CW_LABEL, cw);
        show(dlg, IDC_COMPOSE_CW, cw);
        if (cw)
            SetDlgItemTextW(dlg, IDC_COMPOSE_CW, to_wide(req.prefill_cw).c_str());

        // Visibility.
        const bool vis = req.features.visibility && !editing;
        show(dlg, IDC_COMPOSE_VIS_LABEL, vis);
        show(dlg, IDC_COMPOSE_VISIBILITY, vis);
        if (vis) {
            HWND combo = GetDlgItem(dlg, IDC_COMPOSE_VISIBILITY);
            // Order matches the Visibility enum (Public/Unlisted/Private/Direct).
            for (const wchar_t* name : {L"Public", L"Unlisted", L"Followers only", L"Direct"})
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
            SendMessageW(combo, CB_SETCURSEL,
                         req.default_visibility ? static_cast<int>(*req.default_visibility) : 0, 0);
        }

        // Language (always).
        {
            HWND combo = GetDlgItem(dlg, IDC_COMPOSE_LANGUAGE);
            for (const auto& [code, name] : util::languages())
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(to_wide(name).c_str()));
            SendMessageW(combo, CB_SETCURSEL, 0, 0);
        }

        // Poll.
        const bool polls = req.features.polls && !editing;
        show(dlg, IDC_COMPOSE_POLL, polls);
        for (int id : {IDC_COMPOSE_POLL_OPT1, IDC_COMPOSE_POLL_OPT2, IDC_COMPOSE_POLL_OPT3,
                       IDC_COMPOSE_POLL_OPT4, IDC_COMPOSE_POLL_MULTI, IDC_COMPOSE_DUR_LABEL,
                       IDC_COMPOSE_DURATION})
            show(dlg, id, false);
        if (polls) {
            HWND combo = GetDlgItem(dlg, IDC_COMPOSE_DURATION);
            for (const auto& d : kDurations)
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(d.label));
            SendMessageW(combo, CB_SETCURSEL, 4, 0); // 1 day
        }

        // Schedule.
        const bool sched = req.features.scheduling && !editing;
        show(dlg, IDC_COMPOSE_SCHEDULE, sched);
        show(dlg, IDC_COMPOSE_SCHED_TIME, false);
        if (sched) {
            HWND dtp = GetDlgItem(dlg, IDC_COMPOSE_SCHED_TIME);
            SendMessageW(dtp, DTM_SETFORMATW, 0,
                         reinterpret_cast<LPARAM>(L"yyyy-MM-dd HH:mm"));
            SYSTEMTIME now{};
            GetLocalTime(&now);
            SendMessageW(dtp, DTM_SETRANGE, GDTR_MIN, reinterpret_cast<LPARAM>(&now));
        }

        // Prefill + post button label.
        SetDlgItemTextW(dlg, IDC_COMPOSE_EDIT, to_wide(req.prefill_text).c_str());
        SetDlgItemTextW(dlg, IDOK, editing ? L"&Save" : L"&Post");

        SetWindowSubclass(GetDlgItem(dlg, IDC_COMPOSE_EDIT), EditProc, 1,
                          reinterpret_cast<DWORD_PTR>(ctx));

        HWND edit = GetDlgItem(dlg, IDC_COMPOSE_EDIT);
        SetFocus(edit);
        SendMessageW(edit, EM_SETSEL, GetWindowTextLengthW(edit), GetWindowTextLengthW(edit));
        update_counter(dlg, ctx);
        return FALSE; // focus set explicitly
    }

    case WM_COMMAND: {
        auto* ctx = reinterpret_cast<Ctx*>(GetWindowLongPtrW(dlg, DWLP_USER));
        const int id = LOWORD(wp);
        if (id == IDC_COMPOSE_POLL) {
            toggle_poll(dlg, ctx);
            return TRUE;
        }
        if (id == IDC_COMPOSE_SCHEDULE) {
            toggle_schedule(dlg);
            return TRUE;
        }
        if (id == IDC_COMPOSE_EDIT && HIWORD(wp) == EN_CHANGE) {
            update_counter(dlg, ctx);
            return TRUE;
        }
        if (HIWORD(wp) == CBN_SELCHANGE || (id >= IDC_COMPOSE_POLL_OPT1 && id <= IDC_COMPOSE_POLL_OPT4 &&
                                            HIWORD(wp) == EN_CHANGE)) {
            update_counter(dlg, ctx);
            return TRUE;
        }
        if (id == IDOK) {
            const std::string body = trim(text_of(GetDlgItem(dlg, IDC_COMPOSE_EDIT)));
            if (body.empty() || !poll_valid(dlg))
                return TRUE;
            PostDraft draft;
            draft.text = body;
            if (ctx->req->features.content_warning) {
                std::string spoiler = trim(text_of(GetDlgItem(dlg, IDC_COMPOSE_CW)));
                if (!spoiler.empty())
                    draft.spoiler_text = spoiler;
            }
            if (IsWindowVisible(GetDlgItem(dlg, IDC_COMPOSE_VISIBILITY))) {
                const int sel = static_cast<int>(
                    SendDlgItemMessageW(dlg, IDC_COMPOSE_VISIBILITY, CB_GETCURSEL, 0, 0));
                draft.visibility = static_cast<Visibility>(sel < 0 ? 0 : sel);
            }
            {
                const int sel = static_cast<int>(
                    SendDlgItemMessageW(dlg, IDC_COMPOSE_LANGUAGE, CB_GETCURSEL, 0, 0));
                const auto& langs = util::languages();
                if (sel >= 0 && sel < static_cast<int>(langs.size()))
                    draft.language = langs[static_cast<size_t>(sel)].first;
            }
            if (poll_active(dlg)) {
                PollDraft poll;
                for (int oid : {IDC_COMPOSE_POLL_OPT1, IDC_COMPOSE_POLL_OPT2, IDC_COMPOSE_POLL_OPT3,
                                IDC_COMPOSE_POLL_OPT4}) {
                    std::string opt = trim(text_of(GetDlgItem(dlg, oid)));
                    if (!opt.empty())
                        poll.options.push_back(opt);
                }
                poll.multiple =
                    SendDlgItemMessageW(dlg, IDC_COMPOSE_POLL_MULTI, BM_GETCHECK, 0, 0) == BST_CHECKED;
                const int di = static_cast<int>(
                    SendDlgItemMessageW(dlg, IDC_COMPOSE_DURATION, CB_GETCURSEL, 0, 0));
                poll.expires_in_seconds =
                    (di >= 0 && di < static_cast<int>(std::size(kDurations))) ? kDurations[di].seconds
                                                                             : 86400;
                draft.poll = poll;
            }
            if (IsWindowVisible(GetDlgItem(dlg, IDC_COMPOSE_SCHED_TIME))) {
                SYSTEMTIME st{};
                if (SendDlgItemMessageW(dlg, IDC_COMPOSE_SCHED_TIME, DTM_GETSYSTEMTIME, 0,
                                        reinterpret_cast<LPARAM>(&st)) == GDT_VALID)
                    draft.scheduled_at = systemtime_to_unix(st);
            }
            ctx->result.draft = std::move(draft);
            ctx->result.mode = ctx->req->mode;
            ctx->ok = true;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lp) == GetDlgItem(dlg, IDC_COMPOSE_COUNTER)) {
            auto* ctx = reinterpret_cast<Ctx*>(GetWindowLongPtrW(dlg, DWLP_USER));
            const int len = GetWindowTextLengthW(GetDlgItem(dlg, IDC_COMPOSE_EDIT));
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetTextColor(hdc, len > ctx->req->max_chars ? RGB(200, 0, 0) : GetSysColor(COLOR_GRAYTEXT));
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_3DFACE));
        }
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(GetDlgItem(dlg, IDC_COMPOSE_EDIT), EditProc, 1);
        break;
    }
    return FALSE;
}

} // namespace

std::optional<ComposeResult> show_compose_dialog(HWND parent, HINSTANCE inst,
                                                 const ComposeRequest& req) {
    Ctx ctx;
    ctx.req = &req;
    const INT_PTR r = DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_COMPOSE), parent, Proc,
                                      reinterpret_cast<LPARAM>(&ctx));
    if (r == IDOK && ctx.ok)
        return ctx.result;
    return std::nullopt;
}

} // namespace fastsmui
