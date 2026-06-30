#include "fastsm/net/winhttp_client.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <string>
#include <string_view>

namespace fastsm::net {
namespace {

std::wstring to_wide(std::string_view s) {
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string to_utf8(const wchar_t* s, int len) {
    if (len <= 0)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
    std::string o(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, len, o.data(), n, nullptr, nullptr);
    return o;
}

// RAII wrapper for HINTERNET handles.
struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET handle) : h(handle) {}
    ~Handle() {
        if (h)
            WinHttpCloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h != nullptr; }
    operator HINTERNET() const { return h; }
};

// Parse a "HTTP/.. status\r\nKey: Value\r\n..." block into header pairs,
// skipping the initial status line.
void parse_raw_headers(const std::string& raw, Headers& out) {
    size_t pos = 0;
    bool first = true;
    while (pos < raw.size()) {
        size_t eol = raw.find("\r\n", pos);
        if (eol == std::string::npos)
            eol = raw.size();
        const std::string_view line(raw.data() + pos, eol - pos);
        pos = eol + 2;
        if (first) {
            first = false; // status line
            continue;
        }
        if (line.empty())
            continue;
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos)
            continue;
        std::string_view key = line.substr(0, colon);
        std::string_view value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.remove_prefix(1);
        out.emplace_back(std::string(key), std::string(value));
    }
}

} // namespace

WinHttpClient::WinHttpClient(std::string user_agent) : user_agent_(to_wide(user_agent)) {}

HttpResponse WinHttpClient::send(const HttpRequest& req) {
    HttpResponse res;

    const std::wstring wurl = to_wide(req.url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) {
        res.error = "invalid url";
        return res;
    }

    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    // lpszUrlPath and lpszExtraInfo are contiguous in wurl, so this yields
    // "/path?query".
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength + uc.dwExtraInfoLength);
    if (path.empty())
        path = L"/";
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;

    // DEFAULT_PROXY (not AUTOMATIC_PROXY, which needs Win8.1+) so WinHttpOpen
    // works on Windows 7 too; it uses the system/IE proxy configuration.
    Handle session(WinHttpOpen(user_agent_.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        res.error = "WinHttpOpen failed";
        return res;
    }
    WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);

    Handle connect(WinHttpConnect(session, host.c_str(), uc.nPort, 0));
    if (!connect) {
        res.error = "WinHttpConnect failed";
        return res;
    }

    const std::wstring method = to_wide(req.method);
    const DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    Handle request(WinHttpOpenRequest(connect, method.c_str(), path.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        res.error = "WinHttpOpenRequest failed";
        return res;
    }

    std::wstring header_block;
    for (const auto& [key, value] : req.headers) {
        header_block += to_wide(key);
        header_block += L": ";
        header_block += to_wide(value);
        header_block += L"\r\n";
    }
    if (!header_block.empty()) {
        WinHttpAddRequestHeaders(request, header_block.c_str(), static_cast<DWORD>(-1),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    const DWORD body_len = static_cast<DWORD>(req.body.size());
    LPVOID body_ptr =
        req.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(req.body.data());
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body_ptr, body_len, body_len,
                            0)) {
        res.error = "WinHttpSendRequest failed";
        return res;
    }
    if (!WinHttpReceiveResponse(request, nullptr)) {
        res.error = "WinHttpReceiveResponse failed";
        return res;
    }

    DWORD code = 0;
    DWORD code_size = sizeof(code);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &code, &code_size, WINHTTP_NO_HEADER_INDEX);
    res.status = static_cast<long>(code);

    DWORD header_bytes = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                        nullptr, &header_bytes, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && header_bytes > 0) {
        std::wstring raw(header_bytes / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &header_bytes,
                                WINHTTP_NO_HEADER_INDEX)) {
            const std::string utf8 = to_utf8(raw.c_str(), static_cast<int>(wcslen(raw.c_str())));
            parse_raw_headers(utf8, res.headers);
        }
    }

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            res.error = "WinHttpQueryDataAvailable failed";
            break;
        }
        if (available == 0)
            break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read))
            break;
        res.body.append(chunk.data(), read);
    }

    return res;
}

} // namespace fastsm::net
