#include "compose_dialog.hpp"

#include <commctrl.h>
#include <commdlg.h>

#include <fstream>
#include <string>
#include <vector>

#include "../resources/resource.h"
#include "utf.hpp"

#include "fastsm/util/base64.hpp"
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
    std::vector<ComposeAttachment> attachments; // staged media (edited via the sub-dialog)
};

// Guess a MIME type from a file's extension (lowercased). Unknown -> octet-stream.
std::string guess_mime(const std::wstring& filename) {
    const size_t dot = filename.find_last_of(L'.');
    std::wstring ext = dot == std::wstring::npos ? L"" : filename.substr(dot + 1);
    for (wchar_t& c : ext)
        c = static_cast<wchar_t>(towlower(c));
    struct { const wchar_t* ext; const char* mime; } kinds[] = {
        {L"jpg", "image/jpeg"},  {L"jpeg", "image/jpeg"}, {L"png", "image/png"},
        {L"gif", "image/gif"},   {L"webp", "image/webp"}, {L"bmp", "image/bmp"},
        {L"heic", "image/heic"}, {L"mp4", "video/mp4"},   {L"m4v", "video/mp4"},
        {L"mov", "video/quicktime"}, {L"webm", "video/webm"}, {L"mp3", "audio/mpeg"},
        {L"m4a", "audio/mp4"},   {L"wav", "audio/wav"},   {L"ogg", "audio/ogg"},
        {L"oga", "audio/ogg"},   {L"flac", "audio/flac"},
    };
    for (const auto& k : kinds)
        if (ext == k.ext)
            return k.mime;
    return "application/octet-stream";
}

// Read a whole file and base64-encode it; nullopt if it can't be opened.
std::optional<std::string> read_file_base64(const std::wstring& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f)
        return std::nullopt;
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.empty())
        return std::nullopt;
    return fastsm::util::base64_encode(bytes);
}

std::wstring att_row_label(const ComposeAttachment& a) {
    return a.filename + (a.alt.empty() ? L"  (no alt text)" : L"  [alt set]");
}

struct AttCtx {
    std::vector<ComposeAttachment>* items;
};

void att_refresh(HWND dlg, AttCtx* c, int select) {
    HWND lb = GetDlgItem(dlg, IDC_ATT_LIST);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    for (const auto& a : *c->items)
        SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(att_row_label(a).c_str()));
    if (!c->items->empty()) {
        const int sel = select < 0 ? 0 : (select >= static_cast<int>(c->items->size())
                                              ? static_cast<int>(c->items->size()) - 1
                                              : select);
        SendMessageW(lb, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
    }
    const bool any = !c->items->empty();
    EnableWindow(GetDlgItem(dlg, IDC_ATT_REMOVE), any);
    EnableWindow(GetDlgItem(dlg, IDC_ATT_ALT), any);
    EnableWindow(GetDlgItem(dlg, IDC_ATT_ALT_SET), any);
}

void att_load_alt(HWND dlg, AttCtx* c) {
    const int sel = static_cast<int>(SendDlgItemMessageW(dlg, IDC_ATT_LIST, LB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel < static_cast<int>(c->items->size()))
        SetDlgItemTextW(dlg, IDC_ATT_ALT, (*c->items)[static_cast<size_t>(sel)].alt.c_str());
}

void att_add(HWND dlg, AttCtx* c) {
    wchar_t buf[2048] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dlg;
    ofn.lpstrFilter = L"Media (images, video, audio)\0"
                      L"*.jpg;*.jpeg;*.png;*.gif;*.webp;*.bmp;*.heic;*.mp4;*.m4v;*.mov;*.webm;"
                      L"*.mp3;*.m4a;*.wav;*.ogg;*.oga;*.flac\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 2048;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn))
        return;
    // With multi-select: buf = "dir\0file1\0file2\0\0"; single: buf = full path.
    std::vector<std::wstring> paths;
    const std::wstring dir = buf;
    const wchar_t* p = buf + dir.size() + 1;
    if (*p == L'\0') {
        paths.push_back(dir); // single selection: buf is the full path
    } else {
        for (; *p != L'\0'; p += wcslen(p) + 1) {
            std::wstring full = dir;
            if (!full.empty() && full.back() != L'\\')
                full += L'\\';
            full += p;
            paths.push_back(full);
        }
    }
    for (const auto& full : paths) {
        auto data = read_file_base64(full);
        if (!data)
            continue;
        const size_t slash = full.find_last_of(L"\\/");
        ComposeAttachment a;
        a.filename = slash == std::wstring::npos ? full : full.substr(slash + 1);
        a.mime = guess_mime(a.filename);
        a.data_base64 = std::move(*data);
        c->items->push_back(std::move(a));
    }
    att_refresh(dlg, c, static_cast<int>(c->items->size()) - 1);
    att_load_alt(dlg, c);
}

INT_PTR CALLBACK AttProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        auto* c = reinterpret_cast<AttCtx*>(lp);
        att_refresh(dlg, c, 0);
        att_load_alt(dlg, c);
        return TRUE;
    }
    case WM_COMMAND: {
        auto* c = reinterpret_cast<AttCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
        const int id = LOWORD(wp);
        if (id == IDC_ATT_ADD) {
            att_add(dlg, c);
            return TRUE;
        }
        if (id == IDC_ATT_LIST && HIWORD(wp) == LBN_SELCHANGE) {
            att_load_alt(dlg, c);
            return TRUE;
        }
        if (id == IDC_ATT_REMOVE) {
            const int sel =
                static_cast<int>(SendDlgItemMessageW(dlg, IDC_ATT_LIST, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(c->items->size())) {
                c->items->erase(c->items->begin() + sel);
                att_refresh(dlg, c, sel);
                att_load_alt(dlg, c);
            }
            return TRUE;
        }
        if (id == IDC_ATT_ALT_SET) {
            const int sel =
                static_cast<int>(SendDlgItemMessageW(dlg, IDC_ATT_LIST, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(c->items->size())) {
                wchar_t buf[1024] = {0};
                GetDlgItemTextW(dlg, IDC_ATT_ALT, buf, 1024);
                (*c->items)[static_cast<size_t>(sel)].alt = buf;
                att_refresh(dlg, c, sel);
            }
            return TRUE;
        }
        if (id == IDOK) {
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// Edit the attachment list in a modal sub-dialog; true if the user pressed OK.
bool run_attachments_dialog(HWND parent, HINSTANCE inst, std::vector<ComposeAttachment>& list) {
    std::vector<ComposeAttachment> working = list; // edit a copy; commit only on OK
    AttCtx c{&working};
    if (DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_ATTACHMENTS), parent, AttProc,
                        reinterpret_cast<LPARAM>(&c)) != IDOK)
        return false;
    list = std::move(working);
    return true;
}

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
    // The remaining character count lives in the title bar, not the window.
    std::wstring title = ctx->req->title + L" (" + std::to_wstring(remaining) + L")";
    SetWindowTextW(dlg, title.c_str());

    const std::string body = trim(text_of(GetDlgItem(dlg, IDC_COMPOSE_EDIT)));
    // A post needs text, or at least one attachment (media-only posts are allowed).
    const bool has_content = !body.empty() || !ctx->attachments.empty();
    const bool ok = remaining >= 0 && has_content && poll_valid(dlg);
    EnableWindow(GetDlgItem(dlg, IDOK), ok);
}

void update_media_status(HWND dlg, Ctx* ctx) {
    if (!IsWindowVisible(GetDlgItem(dlg, IDC_COMPOSE_MEDIA)))
        return;
    const size_t n = ctx->attachments.size();
    std::wstring s = n == 0   ? L"No attachments"
                     : n == 1 ? L"1 attachment"
                              : std::to_wstring(n) + L" attachments";
    SetDlgItemTextW(dlg, IDC_COMPOSE_MEDIA_STATUS, s.c_str());
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
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        // When Enter sends, either Ctrl+Enter or Shift+Enter inserts a newline;
        // when Enter inserts a newline, Ctrl+Enter sends.
        const bool newline_combo = ctrl || shift;
        const bool send = ctx->req->enter_to_send ? !newline_combo : ctrl;
        if (send) {
            PostMessageW(GetParent(h), WM_COMMAND, IDOK, 0);
            ctx->eat_char = true;
            return 0;
        }
        if (ctx->req->enter_to_send && newline_combo) {
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

        // Character count goes in the title bar (update_counter); hide the
        // in-window counter label.
        show(dlg, IDC_COMPOSE_COUNTER, false);

        // Context (reply/quote).
        const bool has_context =
            (req.mode == ComposeMode::Reply || req.mode == ComposeMode::Quote) &&
            !req.context_label.empty();
        if (has_context)
            SetDlgItemTextW(dlg, IDC_COMPOSE_CONTEXT, to_wide(req.context_label).c_str());
        show(dlg, IDC_COMPOSE_CONTEXT, has_context);

        // Reply recipients: a checklist to the left of the post body. Every one is
        // checked by default; only the ones left checked get mentioned (the core
        // prepends them). When there are none the list is hidden and the body keeps
        // its full width.
        const bool has_recipients = !req.recipients.empty();
        show(dlg, IDC_COMPOSE_RCP_LABEL, has_recipients);
        show(dlg, IDC_COMPOSE_RECIPIENTS, has_recipients);
        if (has_recipients) {
            HWND lv = GetDlgItem(dlg, IDC_COMPOSE_RECIPIENTS);
            ListView_SetExtendedListViewStyle(lv, LVS_EX_CHECKBOXES);
            for (size_t i = 0; i < req.recipients.size(); ++i) {
                LVITEMW it{};
                it.mask = LVIF_TEXT;
                it.iItem = static_cast<int>(i);
                it.pszText = const_cast<wchar_t*>(req.recipients[i].display.c_str());
                ListView_InsertItem(lv, &it);
                ListView_SetCheckState(lv, static_cast<int>(i),
                                       req.recipients[i].checked ? TRUE : FALSE);
            }
            // Shift the Post label + edit box to the right of the recipient list.
            auto client_rect = [&](int cid) {
                RECT r;
                GetWindowRect(GetDlgItem(dlg, cid), &r);
                MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&r), 2);
                return r;
            };
            const RECT lr = client_rect(IDC_COMPOSE_RECIPIENTS);
            const int gap = lr.left; // reuse the left margin as the gap
            const int new_left = lr.right + gap;
            for (int cid : {IDC_COMPOSE_POST_LABEL, IDC_COMPOSE_EDIT}) {
                const RECT r = client_rect(cid);
                MoveWindow(GetDlgItem(dlg, cid), new_left, r.top, r.right - new_left, r.bottom - r.top,
                           TRUE);
            }
        }

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

        // Media attachments (not while editing an existing post).
        const bool media = req.features.media && !editing;
        show(dlg, IDC_COMPOSE_MEDIA, media);
        show(dlg, IDC_COMPOSE_MEDIA_STATUS, media);
        if (media)
            update_media_status(dlg, ctx);

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
        if (id == IDC_COMPOSE_MEDIA) {
            HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dlg, GWLP_HINSTANCE));
            if (run_attachments_dialog(dlg, inst, ctx->attachments)) {
                update_media_status(dlg, ctx);
                update_counter(dlg, ctx); // media-only posts can now enable Post
            }
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
            if ((body.empty() && ctx->attachments.empty()) || !poll_valid(dlg))
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
            // The reply recipients left checked (the core prepends the @handles).
            if (IsWindowVisible(GetDlgItem(dlg, IDC_COMPOSE_RECIPIENTS))) {
                HWND lv = GetDlgItem(dlg, IDC_COMPOSE_RECIPIENTS);
                for (size_t i = 0; i < ctx->req->recipients.size(); ++i)
                    if (ListView_GetCheckState(lv, static_cast<int>(i)))
                        ctx->result.mentions.push_back(ctx->req->recipients[i].acct);
            }
            ctx->result.draft = std::move(draft);
            ctx->result.mode = ctx->req->mode;
            ctx->result.attachments = std::move(ctx->attachments);
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
