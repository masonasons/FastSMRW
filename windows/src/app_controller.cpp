#include "app_controller.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "fastsm/auth/bluesky_auth.hpp"
#include "fastsm/auth/mastodon_auth.hpp"
#include "fastsm/platform/bluesky/bluesky_account.hpp"
#include "fastsm/platform/mastodon/mastodon_account.hpp"
#include "fastsm/store/app_config.hpp"
#include "fastsm/store/paths.hpp"

#include "utf.hpp"

using namespace fastsm;

namespace fastsmui {

AppController::AppController(runtime::IMainExecutor* main, SoundManager* sound)
    : cache_(store::cache_dir()), accounts_(&http_), main_(main), sound_(sound) {}

bool AppController::has_account() const { return !accounts_.empty(); }

std::vector<TimelineController*> AppController::timelines() const {
    std::vector<TimelineController*> out;
    out.reserve(timelines_.size());
    for (const auto& t : timelines_)
        out.push_back(t.get());
    return out;
}

TimelineController* AppController::current() const {
    if (current_ < 0 || current_ >= static_cast<int>(timelines_.size()))
        return nullptr;
    return timelines_[static_cast<size_t>(current_)].get();
}

void AppController::select_timeline(int index) {
    if (index < 0 || index >= static_cast<int>(timelines_.size()) || index == current_)
        return;
    current_ = index;
    if (view_)
        view_->current_timeline_changed();
}

void AppController::next_timeline() {
    if (timelines_.empty())
        return;
    current_ = (current_ + 1) % static_cast<int>(timelines_.size());
    if (sound_)
        sound_->play(sound::Earcon::Navigate);
    if (view_)
        view_->current_timeline_changed();
}

void AppController::previous_timeline() {
    if (timelines_.empty())
        return;
    const int n = static_cast<int>(timelines_.size());
    current_ = (current_ - 1 + n) % n;
    if (sound_)
        sound_->play(sound::Earcon::Navigate);
    if (view_)
        view_->current_timeline_changed();
}

void AppController::next_account() {
    auto accts = accounts_.accounts();
    if (accts.size() < 2)
        return;
    int idx = 0;
    for (size_t i = 0; i < accts.size(); ++i)
        if (accts[i]->account_key() == accounts_.selected_key())
            idx = static_cast<int>(i);
    accounts_.select(accts[(idx + 1) % accts.size()]->account_key());
    if (sound_)
        sound_->play(sound::Earcon::Navigate);
    rebuild_timelines();
}

void AppController::previous_account() {
    auto accts = accounts_.accounts();
    if (accts.size() < 2)
        return;
    int idx = 0;
    for (size_t i = 0; i < accts.size(); ++i)
        if (accts[i]->account_key() == accounts_.selected_key())
            idx = static_cast<int>(i);
    const int n = static_cast<int>(accts.size());
    accounts_.select(accts[(idx - 1 + n) % n]->account_key());
    if (sound_)
        sound_->play(sound::Earcon::Navigate);
    rebuild_timelines();
}

void AppController::bootstrap() {
    worker_.post([this] {
        store::AppConfig config = store::AppConfigStore::default_store().load();
        accounts_.load(config); // Bluesky re-establishes a session here
        main_->post([this] {
            rebuild_timelines();
            if (view_ && !has_account())
                view_->announce("No account yet. Use Add Account (Ctrl+Shift+A).");
        });
    });
}

void AppController::rebuild_timelines() {
    timelines_.clear();
    current_ = 0;
    SocialAccount* account = accounts_.selected();
    if (account) {
        for (const TimelineSource& src : account->default_timelines()) {
            auto tc = std::make_unique<TimelineController>(account, src, &cache_, &worker_, main_);
            TimelineController* p = tc.get();
            tc->on_change = [this, p] {
                if (view_)
                    view_->timeline_updated(p);
            };
            tc->on_error = [this](std::string e) {
                if (view_)
                    view_->announce(e);
                if (sound_)
                    sound_->play(sound::Earcon::Error);
            };
            tc->on_received_new = [this](int n) {
                if (sound_ && n > 0)
                    sound_->play(sound::Earcon::New);
            };
            timelines_.push_back(std::move(tc));
        }
    }

    if (view_)
        view_->timelines_rebuilt();

    bool first = true;
    for (auto& tc : timelines_) {
        tc->load_cached();
        tc->refresh();
        if (first && sound_)
            sound_->play(sound::Earcon::Refresh);
        first = false;
    }
}

void AppController::save_config() {
    store::AppConfig config = accounts_.to_config();
    worker_.post([config] { store::AppConfigStore::default_store().save(config); });
}

void AppController::add_mastodon(const std::string& instance,
                                 std::function<void(bool, std::string)> done) {
    worker_.post([this, instance, done] {
        MastodonAuth auth(&http_);
        auto open_browser = [this](const std::string& url) {
            main_->post([url] {
                ShellExecuteW(nullptr, L"open", to_wide(url).c_str(), nullptr, nullptr, SW_SHOW);
            });
        };
        MastodonLoginResult r = auth.login(instance, open_browser);
        main_->post([this, r = std::move(r), done]() mutable {
            if (r.ok) {
                store::StoredCredential cred;
                cred.mastodon = r.credentials;
                auto account = std::make_unique<MastodonAccount>(r.credentials, r.me, &http_);
                const std::string key = account->account_key();
                accounts_.add(std::move(account), cred);
                accounts_.select(key);
                save_config();
                rebuild_timelines();
            }
            done(r.ok, r.error);
        });
    });
}

void AppController::add_bluesky(const std::string& service, const std::string& handle,
                                const std::string& app_password,
                                std::function<void(bool, std::string)> done) {
    worker_.post([this, service, handle, app_password, done] {
        BlueskyAuth auth(&http_);
        BlueskyLoginResult r = auth.login(service, handle, app_password);
        main_->post([this, r = std::move(r), done]() mutable {
            if (r.ok) {
                store::StoredCredential cred;
                cred.bluesky = r.credentials;
                auto account = std::make_unique<BlueskyAccount>(r.credentials, r.session, r.me,
                                                                &http_);
                const std::string key = account->account_key();
                accounts_.add(std::move(account), cred);
                accounts_.select(key);
                save_config();
                rebuild_timelines();
            }
            done(r.ok, r.error);
        });
    });
}

} // namespace fastsmui
