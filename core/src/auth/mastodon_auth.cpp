#include "fastsm/auth/mastodon_auth.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <nlohmann/json.hpp>

#include <random>
#include <string>

#include "fastsm/platform/mastodon/mastodon_map.hpp"
#include "fastsm/util/url.hpp"

#pragma comment(lib, "ws2_32.lib")

using nlohmann::json;

namespace fastsm {
namespace {

constexpr const char* kScopes = "read write follow";
constexpr const char* kClientName = "FastSMRW";

std::string random_state() {
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 15);
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 24; ++i)
        s.push_back(hex[dist(rd)]);
    return s;
}

// A one-shot loopback HTTP listener: binds 127.0.0.1 on an OS-chosen port,
// hands back the port, then blocks until the browser hits the redirect and
// returns the `code` query parameter.
class LoopbackListener {
public:
    bool start() {
        if (WSAStartup(MAKEWORD(2, 2), &wsa_) != 0)
            return false;
        started_ = true;
        sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_ == INVALID_SOCKET)
            return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = 0; // OS picks
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
            return false;
        int len = sizeof(addr);
        if (getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len) != 0)
            return false;
        port_ = ntohs(addr.sin_port);
        return listen(sock_, 1) == 0;
    }

    int port() const { return port_; }
    std::string redirect_uri() const {
        return "http://127.0.0.1:" + std::to_string(port_) + "/";
    }

    // Blocks for the redirect; returns the authorization code (or empty).
    std::string wait_for_code() {
        SOCKET client = accept(sock_, nullptr, nullptr);
        if (client == INVALID_SOCKET)
            return {};
        std::string request;
        char buf[2048];
        int n = recv(client, buf, sizeof(buf) - 1, 0);
        if (n > 0)
            request.assign(buf, static_cast<size_t>(n));

        const char* body =
            "<html><head><meta charset=\"utf-8\"><title>FastSMRW</title></head>"
            "<body><h2>FastSMRW</h2><p>Authorization complete. You can close this "
            "window and return to FastSMRW.</p></body></html>";
        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                               "Connection: close\r\nContent-Length: " +
                               std::to_string(std::char_traits<char>::length(body)) + "\r\n\r\n" +
                               body;
        send(client, response.c_str(), static_cast<int>(response.size()), 0);
        closesocket(client);

        return extract_code(request);
    }

    ~LoopbackListener() {
        if (sock_ != INVALID_SOCKET)
            closesocket(sock_);
        if (started_)
            WSACleanup();
    }

private:
    static std::string extract_code(const std::string& request) {
        // First line: "GET /?code=XXX&state=YYY HTTP/1.1"
        const size_t q = request.find("code=");
        if (q == std::string::npos)
            return {};
        size_t start = q + 5;
        size_t end = start;
        while (end < request.size() && request[end] != '&' && request[end] != ' ' &&
               request[end] != '\r' && request[end] != '\n')
            ++end;
        return request.substr(start, end - start);
    }

    WSADATA wsa_{};
    bool started_ = false;
    SOCKET sock_ = INVALID_SOCKET;
    int port_ = 0;
};

} // namespace

MastodonAuth::MastodonAuth(net::IHttpClient* http) : http_(http) {}

std::string MastodonAuth::normalize_instance(std::string input) {
    // Trim whitespace.
    size_t a = input.find_first_not_of(" \t\r\n");
    size_t b = input.find_last_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    input = input.substr(a, b - a + 1);
    // Strip scheme.
    if (input.rfind("https://", 0) == 0)
        input = input.substr(8);
    else if (input.rfind("http://", 0) == 0)
        input = input.substr(7);
    // Strip trailing slashes and any path.
    if (size_t slash = input.find('/'); slash != std::string::npos)
        input = input.substr(0, slash);
    return input.empty() ? std::string{} : "https://" + input;
}

MastodonLoginResult
MastodonAuth::login(const std::string& instance_input,
                    const std::function<void(const std::string&)>& open_browser) {
    MastodonLoginResult result;
    const std::string instance = normalize_instance(instance_input);
    if (instance.empty()) {
        result.error = "Empty instance";
        return result;
    }

    LoopbackListener listener;
    if (!listener.start()) {
        result.error = "Could not start local listener";
        return result;
    }
    const std::string redirect = listener.redirect_uri();

    // 1) Register the app (client_name=FastSMRW). redirect_uris must match.
    {
        std::vector<std::pair<std::string, std::string>> params = {
            {"client_name", kClientName},
            {"redirect_uris", redirect},
            {"scopes", kScopes},
            {"website", "https://github.com/masonasons/FastSM"}};
        net::HttpRequest req;
        req.method = "POST";
        req.url = instance + "/api/v1/apps";
        req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
        req.body = util::form_encode(params);
        const net::HttpResponse res = http_->send(req);
        if (!res.ok()) {
            result.error = "App registration failed (" + std::to_string(res.status) + ")";
            return result;
        }
        try {
            const json j = json::parse(res.body);
            result.credentials.instance_url = instance;
            result.credentials.client_id = j.value("client_id", "");
            result.credentials.client_secret = j.value("client_secret", "");
        } catch (...) {
            result.error = "Bad app registration response";
            return result;
        }
        if (result.credentials.client_id.empty()) {
            result.error = "No client_id returned";
            return result;
        }
    }

    // 2) Open the authorize URL in the browser.
    const std::string state = random_state();
    const std::string authorize_url =
        instance + "/oauth/authorize?response_type=code&client_id=" +
        util::percent_encode(result.credentials.client_id) +
        "&redirect_uri=" + util::percent_encode(redirect) +
        "&scope=" + util::percent_encode(kScopes) + "&state=" + state;
    open_browser(authorize_url);

    // 3) Wait for the redirect and capture the code.
    const std::string code = listener.wait_for_code();
    if (code.empty()) {
        result.error = "No authorization code received";
        return result;
    }

    // 4) Exchange the code for an access token.
    {
        std::vector<std::pair<std::string, std::string>> params = {
            {"grant_type", "authorization_code"},
            {"code", code},
            {"client_id", result.credentials.client_id},
            {"client_secret", result.credentials.client_secret},
            {"redirect_uri", redirect},
            {"scope", kScopes}};
        net::HttpRequest req;
        req.method = "POST";
        req.url = instance + "/oauth/token";
        req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
        req.body = util::form_encode(params);
        const net::HttpResponse res = http_->send(req);
        if (!res.ok()) {
            result.error = "Token exchange failed (" + std::to_string(res.status) + ")";
            return result;
        }
        try {
            result.credentials.access_token = json::parse(res.body).value("access_token", "");
        } catch (...) {
            result.error = "Bad token response";
            return result;
        }
        if (result.credentials.access_token.empty()) {
            result.error = "No access token returned";
            return result;
        }
    }

    // 5) Verify credentials -> me.
    {
        net::HttpRequest req;
        req.method = "GET";
        req.url = instance + "/api/v1/accounts/verify_credentials";
        req.headers.push_back({"Authorization", "Bearer " + result.credentials.access_token});
        const net::HttpResponse res = http_->send(req);
        if (!res.ok()) {
            result.error = "verify_credentials failed (" + std::to_string(res.status) + ")";
            return result;
        }
        try {
            result.me = mastodon::map_user(json::parse(res.body));
        } catch (...) {
            result.error = "Bad verify_credentials response";
            return result;
        }
    }

    result.ok = true;
    return result;
}

} // namespace fastsm
