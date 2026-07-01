#include "fastsm/update/update_checker.hpp"

#include <cctype>
#include <vector>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace fastsm::update {
namespace {

// Parse the numeric components of a dotted version, ignoring a leading 'v' and any
// trailing non-digits on a component ("v0.0.4-beta" -> {0,0,4}).
std::vector<int> version_parts(const std::string& s) {
    std::vector<int> parts;
    size_t i = 0;
    if (i < s.size() && (s[i] == 'v' || s[i] == 'V'))
        ++i;
    while (i < s.size()) {
        int n = 0;
        bool any = false;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            n = n * 10 + (s[i] - '0');
            any = true;
            ++i;
        }
        if (any)
            parts.push_back(n);
        // Skip to the next '.'-separated component.
        while (i < s.size() && s[i] != '.')
            ++i;
        if (i < s.size() && s[i] == '.')
            ++i;
    }
    return parts;
}

json get_json(net::IHttpClient& http, const std::string& url, std::string& error) {
    net::HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers.push_back({"Accept", "application/vnd.github+json"});
    const net::HttpResponse res = http.send(req);
    if (!res.ok()) {
        error = res.error.empty() ? ("GitHub returned " + std::to_string(res.status)) : res.error;
        return json{};
    }
    try {
        return json::parse(res.body);
    } catch (...) {
        error = "Couldn't read GitHub's response";
        return json{};
    }
}

// The FastSMRW.zip asset's download URL within a release object, or "".
std::string zip_asset_url(const json& release) {
    if (auto it = release.find("assets"); it != release.end() && it->is_array())
        for (const auto& a : *it)
            if (a.value("name", std::string()) == "FastSMRW.zip")
                return a.value("browser_download_url", std::string());
    return {};
}

bool looks_like_sha(const std::string& s) {
    if (s.size() < 7)
        return false;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    return true;
}

// The commit a `latest` release was built from: the target_commitish if it's a
// raw SHA, else the first hex run of length >= 7 in the notes body.
std::string release_commit(const json& release) {
    const std::string target = release.value("target_commitish", std::string());
    if (looks_like_sha(target))
        return target;
    const std::string body = release.value("body", std::string());
    for (size_t i = 0; i < body.size();) {
        if (std::isxdigit(static_cast<unsigned char>(body[i]))) {
            size_t j = i;
            while (j < body.size() && std::isxdigit(static_cast<unsigned char>(body[j])))
                ++j;
            if (j - i >= 7)
                return body.substr(i, j - i);
            i = j;
        } else {
            ++i;
        }
    }
    return {};
}

std::string api_base(const std::string& repo) {
    return "https://api.github.com/repos/" + repo;
}

UpdateInfo check_latest(net::IHttpClient& http, const std::string& current_commit,
                        const std::string& repo) {
    UpdateInfo info;
    info.branch = "latest";
    std::string error;
    const json rel = get_json(http, api_base(repo) + "/releases/tags/latest", error);
    if (!error.empty()) {
        info.error = error;
        return info;
    }
    const std::string remote = release_commit(rel);
    info.notes = rel.value("body", std::string());
    info.download_url = zip_asset_url(rel);
    info.version = remote.substr(0, 7);
    if (remote.empty() || info.download_url.empty()) {
        info.error = "No usable 'latest' build was published.";
        return info;
    }
    // A local build without an embedded commit can't be compared; treat as current.
    if (current_commit.empty())
        return info;
    info.available = remote.substr(0, 7) != current_commit.substr(0, 7);
    return info;
}

UpdateInfo check_stable(net::IHttpClient& http, const std::string& current_version,
                        const std::string& repo) {
    UpdateInfo info;
    info.branch = "stable";
    std::string error;
    const json arr = get_json(http, api_base(repo) + "/releases?per_page=30", error);
    if (!error.empty()) {
        info.error = error;
        return info;
    }
    if (!arr.is_array()) {
        info.error = "Unexpected response from GitHub.";
        return info;
    }
    // Pick the highest version-tagged release (tag "vX.Y.Z"), skipping the rolling
    // "latest" tag, drafts and prereleases.
    const json* best = nullptr;
    std::string best_version;
    for (const auto& rel : arr) {
        if (rel.value("draft", false) || rel.value("prerelease", false))
            continue;
        const std::string tag = rel.value("tag_name", std::string());
        if (tag.size() < 2 || (tag[0] != 'v' && tag[0] != 'V'))
            continue;
        const std::string ver = tag.substr(1);
        if (!best || compare_versions(ver, best_version) > 0) {
            best = &rel;
            best_version = ver;
        }
    }
    if (!best) {
        info.error = "No stable release has been published yet.";
        return info;
    }
    info.version = best_version;
    info.notes = best->value("body", std::string());
    info.download_url = zip_asset_url(*best);
    info.available = compare_versions(best_version, current_version) > 0 && !info.download_url.empty();
    return info;
}

} // namespace

int compare_versions(const std::string& a, const std::string& b) {
    const std::vector<int> pa = version_parts(a);
    const std::vector<int> pb = version_parts(b);
    const size_t n = std::max(pa.size(), pb.size());
    for (size_t i = 0; i < n; ++i) {
        const int x = i < pa.size() ? pa[i] : 0;
        const int y = i < pb.size() ? pb[i] : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

UpdateInfo check_for_update(net::IHttpClient& http, const std::string& branch,
                            const std::string& current_version,
                            const std::string& current_commit, const std::string& repo) {
    if (branch == "latest")
        return check_latest(http, current_commit, repo);
    return check_stable(http, current_version, repo);
}

} // namespace fastsm::update
