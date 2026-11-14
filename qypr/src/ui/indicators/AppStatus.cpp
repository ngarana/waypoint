// AppStatus.cpp - Merging an app's notifications, launcher entry and windows.
#include "ui/indicators/AppStatus.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <utility>

#include "system/ApplicationsBackend.hpp"
#include "system/DesktopIndex.hpp"
#include "system/ToplevelBackend.hpp"
#include "ui/Notification.hpp"
#include "ui/statusbar/StatusArea.hpp"

namespace qypr {

namespace {

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

class Builder {
public:
    explicit Builder(const DesktopIndex* index)
        : index_(index != nullptr && index->loaded() ? index : nullptr) {}

    // The record for an app named `raw` by some channel; `entry` is what the
    // desktop index resolved that name to (null: unknown app).
    AppStatus& recordFor(const std::string& raw, const DesktopEntry* entry) {
        const std::string key = lower(entry != nullptr ? entry->id : raw);
        auto it = std::ranges::find(apps_, key, &AppStatus::key);
        if (it == apps_.end()) {
            AppStatus app;
            app.key = key;
            if (entry != nullptr) {
                app.desktopId = entry->id;
                app.name = entry->name;
                app.icon = entry->icon;
                app.categories = entry->categories;
            }
            apps_.push_back(std::move(app));
            it = apps_.end() - 1;
        }
        addAlias(*it, raw);
        return *it;
    }

    // App names that some notification tied to a desktop entry through its
    // hint, so the same app's notifications without the hint join it.
    void learnAppNames(const std::vector<Notification>& notes) {
        if (index_ == nullptr) { return; }
        for (const auto& n : notes) {
            if (n.app.empty() || n.desktopEntry.empty()) { continue; }
            if (const DesktopEntry* e = index_->resolve(n.desktopEntry)) {
                appNames_.emplace_back(lower(n.app), e);
            }
        }
    }

    // A notification names its app by desktop-entry hint and app name; the
    // first of them the index knows decides the record.
    const DesktopEntry* resolveNotification(const Notification& n, std::string* raw) const {
        *raw = !n.desktopEntry.empty() ? n.desktopEntry : n.app;
        if (raw->empty()) { *raw = "App"; }
        if (index_ == nullptr) { return nullptr; }
        if (const DesktopEntry* e = index_->resolve(n.desktopEntry)) { return e; }
        if (const DesktopEntry* e = index_->resolve(n.app)) { return e; }
        const std::string app = lower(n.app);
        const auto it =
            std::ranges::find(appNames_, app, &std::pair<std::string, const DesktopEntry*>::first);
        return it != appNames_.end() ? it->second : nullptr;
    }

    [[nodiscard]] const DesktopEntry* resolveId(const std::string& id) const {
        if (index_ == nullptr) { return nullptr; }
        const DesktopEntry* e = index_->byId(id);
        return e != nullptr ? e : index_->resolve(id);
    }

    [[nodiscard]] const DesktopEntry* resolveWindow(const std::string& appId) const {
        return index_ != nullptr ? index_->resolveWindow(appId) : nullptr;
    }

    std::vector<AppStatus> take() { return std::move(apps_); }

    static void addAlias(AppStatus& app, const std::string& raw) {
        std::string a = lower(raw);
        if (!a.empty() && std::ranges::find(app.aliases, a) == app.aliases.end()) {
            app.aliases.push_back(std::move(a));
        }
    }

private:
    const DesktopIndex* index_;
    std::vector<AppStatus> apps_;
    std::vector<std::pair<std::string, const DesktopEntry*>> appNames_;
};

}  // namespace

bool AppStatus::hasNews() const {
    return notifications > 0 || badge > 0 || urgent || progress >= 0;
}

bool AppStatus::matches(const std::string& pattern) const {
    static constexpr std::string_view kCategory = "category:";
    if (pattern.starts_with(kCategory)) {
        const std::string want = lower(pattern.substr(kCategory.size()));
        std::stringstream ss(categories);
        std::string cat;
        while (std::getline(ss, cat, ';')) {
            if (!cat.empty() && lower(cat) == want) { return true; }
        }
        return false;
    }
    if (StatusArea::patternMatches(pattern, key) || StatusArea::patternMatches(pattern, name)) {
        return true;
    }
    return std::ranges::any_of(
        aliases, [&](const std::string& a) { return StatusArea::patternMatches(pattern, a); });
}

bool AppStatus::matchesAny(const std::vector<std::string>& patterns) const {
    return std::ranges::any_of(patterns, [this](const std::string& p) { return matches(p); });
}

std::vector<AppStatus> buildAppStatus(const AppStatusInputs& in) {
    Builder b(in.index);

    if (in.notifications != nullptr) {
        b.learnAppNames(*in.notifications);
        for (const auto& n : *in.notifications) {
            std::string raw;
            const DesktopEntry* entry = b.resolveNotification(n, &raw);
            AppStatus& app = b.recordFor(raw, entry);
            Builder::addAlias(app, n.app);
            if (app.name.empty()) { app.name = !n.app.empty() ? n.app : raw; }
            if (app.icon.empty()) { app.icon = n.icon; }
            ++app.notifications;
            app.critical = app.critical || n.urgency >= 2;
            if (n.postedAt >= app.latestTime) {
                app.latestTime = n.postedAt;
                if (!n.title.empty()) { app.latestTitle = n.title; }
                if (!n.body.empty()) { app.latestBody = n.body; }
            }
            if (n.daemonId != 0) { app.daemonIds.push_back(n.daemonId); }
        }
    }

    if (in.launcher != nullptr) {
        for (const auto& l : *in.launcher) {
            AppStatus& app = b.recordFor(l.desktopId, b.resolveId(l.desktopId));
            if (app.name.empty()) { app.name = l.desktopId; }
            app.badge = std::max(app.badge, l.badge());
            app.progress = std::max(app.progress, l.shownProgress());
            app.urgent = app.urgent || l.urgent;
            if (!l.quicklist.empty() && app.menuPath.empty()) {
                app.menuService = l.sender;
                app.menuPath = l.quicklist;
            }
        }
    }

    if (in.windows != nullptr) {
        for (const auto& w : *in.windows) {
            if (w.appId.empty()) { continue; }
            AppStatus& app = b.recordFor(w.appId, b.resolveWindow(w.appId));
            if (app.name.empty()) { app.name = w.appId; }
            app.windows.push_back(w.id);
        }
    }

    auto apps = b.take();
    std::ranges::stable_sort(apps, [](const AppStatus& a, const AppStatus& c) {
        if (a.hasNews() != c.hasNews()) { return a.hasNews(); }
        return a.latestTime > c.latestTime;
    });
    return apps;
}

}  // namespace qypr
