#pragma once

#include <string>

#include "fastsm/net/http_client.hpp"

// In-app update check against GitHub Releases. Two branches, mirroring the CI:
//   "latest" — the rolling `latest` release, rebuilt on every push to main; a new
//              build is detected by comparing the release's commit to this binary's
//              embedded build commit.
//   "stable" — the newest version-tagged (`v*`) release; a newer one is detected by
//              comparing its version number to this binary's version.
// Networking/parsing/comparison all live here (portable); the front end only shows
// the result and applies the downloaded zip.
namespace fastsm::update {

struct UpdateInfo {
    bool available = false;
    std::string branch;       // "latest" | "stable" (echoed back)
    std::string version;      // remote version ("0.0.4") or short commit ("a1b2c3d")
    std::string notes;        // release notes / body
    std::string download_url;  // FastSMRW.zip (portable) asset URL, when available
    std::string installer_url; // FastSMRWInstaller.exe asset URL, when available
    std::string apk_url;       // FastSMRW.apk (Android) asset URL, when available
    std::string dmg_url;       // FastSMRW.dmg (macOS) asset URL, when available
    std::string error;         // non-empty on failure (network / parse / no release)
};

// Compare two dotted numeric versions ("0.0.3"). Returns <0 if a<b, 0 if equal,
// >0 if a>b. Missing trailing components count as 0 ("1.2" == "1.2.0"). A leading
// 'v' and any non-numeric suffix on a component are ignored.
int compare_versions(const std::string& a, const std::string& b);

// Check `branch` for an update. `repo` is "owner/name". Blocking (call off the UI
// thread). On any failure, UpdateInfo::error is set and available is false.
UpdateInfo check_for_update(net::IHttpClient& http, const std::string& branch,
                            const std::string& current_version,
                            const std::string& current_commit, const std::string& repo);

} // namespace fastsm::update
