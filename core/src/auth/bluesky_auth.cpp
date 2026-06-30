#include "fastsm/auth/bluesky_auth.hpp"

#include <nlohmann/json.hpp>

#include "fastsm/platform/bluesky/bluesky_map.hpp"

using nlohmann::json;

namespace fastsm {
namespace {

std::string normalize_service(std::string input) {
    size_t a = input.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "https://bsky.social";
    size_t b = input.find_last_not_of(" \t\r\n");
    input = input.substr(a, b - a + 1);
    if (input.empty())
        return "https://bsky.social";
    if (input.rfind("http://", 0) != 0 && input.rfind("https://", 0) != 0)
        input = "https://" + input;
    while (!input.empty() && input.back() == '/')
        input.pop_back();
    return input;
}

std::string strip_at(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n@");
    if (a == std::string::npos)
        return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Find the AtprotoPersonalDataServer endpoint in a DID document.
std::string pds_from_did_doc(const json& doc, const std::string& fallback) {
    if (auto it = doc.find("service"); it != doc.end() && it->is_array()) {
        for (const auto& svc : *it) {
            const std::string type = svc.value("type", "");
            const std::string id = svc.value("id", "");
            if (type == "AtprotoPersonalDataServer" || id.find("atproto_pds") != std::string::npos) {
                std::string endpoint = svc.value("serviceEndpoint", "");
                if (!endpoint.empty())
                    return endpoint;
            }
        }
    }
    return fallback;
}

} // namespace

BlueskyAuth::BlueskyAuth(net::IHttpClient* http) : http_(http) {}

BlueskyLoginResult BlueskyAuth::login(const std::string& service_input,
                                      const std::string& identifier,
                                      const std::string& app_password) {
    BlueskyLoginResult result;
    const std::string service = normalize_service(service_input);
    const std::string id = strip_at(identifier);
    if (id.empty() || app_password.empty()) {
        result.error = "Handle and app password are required";
        return result;
    }

    // 1) createSession
    json create;
    {
        json body;
        body["identifier"] = id;
        body["password"] = app_password;
        net::HttpRequest req;
        req.method = "POST";
        req.url = service + "/xrpc/com.atproto.server.createSession";
        req.headers.push_back({"Content-Type", "application/json"});
        req.body = body.dump();
        const net::HttpResponse res = http_->send(req);
        if (!res.ok()) {
            result.error = "Sign-in failed (" + std::to_string(res.status) + ")";
            return result;
        }
        try {
            create = json::parse(res.body);
        } catch (...) {
            result.error = "Bad sign-in response";
            return result;
        }
    }

    result.session.access_jwt = create.value("accessJwt", "");
    result.session.refresh_jwt = create.value("refreshJwt", "");
    result.session.did = create.value("did", "");
    result.session.handle = create.value("handle", "");
    if (result.session.access_jwt.empty() || result.session.did.empty()) {
        result.error = "Incomplete session";
        return result;
    }
    result.session.pds_url = service;
    if (auto it = create.find("didDoc"); it != create.end() && it->is_object())
        result.session.pds_url = pds_from_did_doc(*it, service);

    result.credentials.service_url = service;
    result.credentials.identifier = id;
    result.credentials.app_password = app_password;
    result.credentials.did = result.session.did;
    result.credentials.handle = result.session.handle;

    // 2) Fetch the profile for `me`.
    {
        net::HttpRequest req;
        req.method = "GET";
        req.url = result.session.pds_url +
                  "/xrpc/app.bsky.actor.getProfile?actor=" + result.session.did;
        req.headers.push_back({"Authorization", "Bearer " + result.session.access_jwt});
        const net::HttpResponse res = http_->send(req);
        if (res.ok()) {
            try {
                result.me = bluesky::map_author(json::parse(res.body));
            } catch (...) {
            }
        }
        // Fall back to handle/did even if the profile fetch failed.
        if (result.me.id.empty()) {
            result.me.platform = Platform::Bluesky;
            result.me.id = result.session.did;
            result.me.acct = result.session.handle;
            result.me.username = result.session.handle;
            result.me.display_name = result.session.handle;
        }
    }

    result.ok = true;
    return result;
}

} // namespace fastsm
