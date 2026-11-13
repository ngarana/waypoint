// NotificationAppsIndicator.cpp - Dynamic per-app notification icons for the status bar.
#include "ui/indicators/NotificationAppsIndicator.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "notifications/NotificationActions.hpp"
#include "notifications/NotificationMonitor.hpp"
#include "render/IconResolver.hpp"
#include "render/Painter.hpp"
#include "system/DesktopIndex.hpp"
#include "system/ToplevelBackend.hpp"
#include "ui/Theme.hpp"
#include "ui/indicators/AppTile.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

namespace {

constexpr double kBtnW = 28.0;    // button width per app
constexpr double kIconPx = 18.0;  // icon render size
constexpr double kGap = 4.0;      // gap between apps
constexpr double kSidePad = 4.0;  // left/right padding

std::string toLower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool isGlyph(const std::string& s) {
    if (s.empty()) return false;
    return std::ranges::all_of(s, [](unsigned char c) {
        return !std::isalpha(c) && c != '-' && c != '_' && c != '.' && c != '/';
    });
}

bool matchesWindow(const ToplevelWindow& w, const NotificationAppsIndicator::AppEntry& app) {
    const std::string wId = toLower(w.appId);
    if (!app.desktopEntry.empty()) {
        const std::string de = toLower(app.desktopEntry);
        if (wId == de || wId.find(de) != std::string::npos || de.find(wId) != std::string::npos) {
            return true;
        }
    }
    if (!app.name.empty()) {
        const std::string nm = toLower(app.name);
        if (wId == nm || wId.find(nm) != std::string::npos || nm.find(wId) != std::string::npos) {
            return true;
        }
        const std::string title = toLower(w.title);
        if (title.find(nm) != std::string::npos) { return true; }
    }
    return false;
}

}  // namespace

NotificationAppsIndicator::NotificationAppsIndicator(const SystemBackends& backends)
    : StatusIndicator("notification-apps", Zone::Right, 640),
      backends_(backends) {
    visible = backends_.notifications != nullptr;
}

void NotificationAppsIndicator::onBackendUpdate() {
    apps_.clear();
    if (backends_.notifications == nullptr) {
        visible = false;
        return;
    }

    const auto& notes = backends_.notifications->notifications();
    if (notes.empty()) {
        visible = false;
        return;
    }

    std::map<std::string, AppEntry> grouped;
    for (const auto& n : notes) {
        std::string key = !n.desktopEntry.empty() ? n.desktopEntry : n.app;
        if (key.empty()) { key = "App"; }

        auto& entry = grouped[key];
        if (entry.name.empty()) { entry.name = !n.app.empty() ? n.app : key; }
        if (entry.desktopEntry.empty()) { entry.desktopEntry = n.desktopEntry; }
        if (entry.icon.empty() && !n.icon.empty()) { entry.icon = n.icon; }
        ++entry.count;
        entry.latestTime = n.postedAt;
        if (!n.title.empty()) { entry.latestTitle = n.title; }
        if (!n.body.empty()) { entry.latestBody = n.body; }
        if (n.daemonId != 0) { entry.daemonIds.push_back(n.daemonId); }
        if (n.urgency >= 2) { entry.hasCritical = true; }
    }

    apps_.reserve(grouped.size());
    for (auto& [_, entry] : grouped) { apps_.push_back(std::move(entry)); }

    std::ranges::sort(
        apps_, [](const AppEntry& a, const AppEntry& b) { return a.latestTime > b.latestTime; });

    visible = !apps_.empty();
}

int NotificationAppsIndicator::hitTest(double x) const {
    if (apps_.empty()) return -1;
    const double localX = x - (bounds.x + kSidePad);
    if (localX < 0) return -1;
    const int idx = static_cast<int>(localX / (kBtnW + kGap));
    if (idx < 0 || std::cmp_greater_equal(idx, apps_.size())) return -1;
    if (localX - (idx * (kBtnW + kGap)) > kBtnW) return -1;
    return idx;
}

std::string NotificationAppsIndicator::tooltip() const {
    if (apps_.empty()) return "No app notifications";

    const int idx = (hoverX_ >= 0) ? hitTest(hoverX_) : -1;
    if (idx >= 0 && std::cmp_less(idx, apps_.size())) {
        const auto& app = apps_[idx];
        std::string tip = app.name;
        if (app.count > 1) {
            tip += " (" + std::to_string(app.count) + " notifications)";
        } else {
            tip += " (1 notification)";
        }
        if (!app.latestTitle.empty()) {
            tip += "\n" + app.latestTitle;
            if (!app.latestBody.empty() && app.latestBody != app.latestTitle) {
                tip += ": " + app.latestBody;
            }
        }
        return tip;
    }

    if (apps_.size() == 1) {
        const auto& app = apps_.front();
        std::string tip = app.name + " (" + std::to_string(app.count) + ")";
        if (!app.latestTitle.empty()) { tip += ": " + app.latestTitle; }
        return tip;
    }

    std::string tip = std::to_string(apps_.size()) + " apps with notifications:";
    for (size_t i = 0; i < apps_.size(); ++i) {
        tip += "\n• " + apps_[i].name + " (" + std::to_string(apps_[i].count) + ")";
        if (i >= 4 && apps_.size() > 5) {
            tip += "\n• … and " + std::to_string(apps_.size() - 5) + " more";
            break;
        }
    }
    return tip;
}

double NotificationAppsIndicator::measureWidth(Painter& p) {
    (void)p;
    if (apps_.empty()) return 0.0;
    const auto n = static_cast<double>(apps_.size());
    return (n * kBtnW) + ((n - 1.0) * kGap) + (2.0 * kSidePad);
}

void NotificationAppsIndicator::draw(Painter& p, int64_t now) {
    (void)now;
    if (!visible || apps_.empty()) return;

    double x = bounds.x + kSidePad;
    const double btnH = bounds.h - 4.0;
    const double btnY = bounds.y + 2.0;

    for (const auto& app : apps_) {
        const Rect btnRect{x, btnY, kBtnW, btnH};

        const bool isHot = (hoverX_ >= 0) && btnRect.contains(hoverX_, hoverY_);
        if (isHot) { p.fillRoundedRect(btnRect, 6.0, theme().colors.glassHover); }

        const double iconX = x + ((kBtnW - kIconPx) / 2.0);
        const double iconY = bounds.y + ((bounds.h - kIconPx) / 2.0);
        const Rect iconRect{iconX, iconY, kIconPx, kIconPx};

        cairo_surface_t* s = nullptr;
        if (!app.desktopEntry.empty()) { s = apptile::resolveIcon(app.desktopEntry); }
        if (!s && !app.icon.empty()) {
            s = apptile::resolveIcon(app.icon);
            if (!s) { s = IconResolver::instance().get(app.icon); }
        }
        if (!s && !app.name.empty()) { s = apptile::resolveIcon(app.name); }

        if (s != nullptr) {
            if (apptile::surfaceIsMonochrome(s) ||
                app.icon.find("-symbolic") != std::string::npos) {
                p.drawSurfaceTinted(s, iconRect, theme().colors.text);
            } else {
                p.drawSurface(s, iconRect);
            }
        } else if (isGlyph(app.icon)) {
            const TextStyle ts{.family = theme().font.iconFamily,
                               .size = kIconPx,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = app.hasCritical ? theme().colors.red : theme().colors.text};
            const Size sz = p.measureText(app.icon, ts);
            p.drawText(iconX + ((kIconPx - sz.w) / 2.0), iconY + ((kIconPx - sz.h) / 2.0), app.icon,
                       ts);
        } else {
            const Color tileColor =
                app.hasCritical ? theme().colors.red : apptile::fallbackColor(app.name, theme());
            p.fillRoundedRect(iconRect, 5.0, tileColor);
            const TextStyle st{theme().font.family, 11.0, PANGO_WEIGHT_BOLD,
                               theme().colors.background};
            const std::string initial = apptile::initialFor(app.name);
            const Size cs = p.measureText(initial, st);
            p.drawText(iconX + ((kIconPx - cs.w) / 2.0), iconY + ((kIconPx - cs.h) / 2.0), initial,
                       st);
        }

        if (app.count > 1) {
            const std::string countStr = app.count > 99 ? "99+" : std::to_string(app.count);
            const TextStyle badgeStyle{theme().font.family, 8.5, PANGO_WEIGHT_BOLD,
                                       theme().colors.background};
            const Size badgeSz = p.measureText(countStr, badgeStyle);
            const double badgeH = 12.0;
            const double badgeW = std::max(badgeH, badgeSz.w + 6.0);
            const double badgeX = x + kBtnW - badgeW;
            const double badgeY = btnY + 1.0;
            const Rect badgeRect{badgeX, badgeY, badgeW, badgeH};
            const Color badgeBg = app.hasCritical ? theme().colors.red : theme().colors.primary;
            p.fillRoundedRect(badgeRect, badgeH / 2.0, badgeBg);
            p.drawText(badgeX + ((badgeW - badgeSz.w) / 2.0), badgeY + ((badgeH - badgeSz.h) / 2.0),
                       countStr, badgeStyle);
        } else if (app.hasCritical) {
            const double dotR = 3.0;
            const double dotX = x + kBtnW - 4.0;
            const double dotY = btnY + 4.0;
            p.fillCircle(dotX, dotY, dotR, theme().colors.red);
        }

        x += kBtnW + kGap;
    }
}

bool NotificationAppsIndicator::onClick(double x, double y) {
    (void)y;
    const int idx = hitTest(x);
    if (idx < 0 || std::cmp_greater_equal(idx, apps_.size())) return false;
    const auto& app = apps_[idx];

    // 1. Try to focus an existing window for this application
    if (backends_.toplevel != nullptr) {
        const auto snap = backends_.toplevel->snapshot();
        for (const auto& w : snap.windows) {
            if (matchesWindow(w, app)) {
                backends_.toplevel->activate(w.id);
                // Also close the notifications for this app
                if (backends_.notificationActions != nullptr) {
                    for (uint32_t daemonId : app.daemonIds) {
                        if (daemonId != 0) { backends_.notificationActions->close(daemonId); }
                    }
                }
                return true;
            }
        }
    }

    // 2. Try to launch the application via DesktopIndex
    if (backends_.desktopIndex != nullptr && backends_.loop != nullptr) {
        if (!backends_.desktopIndex->loaded()) { backends_.desktopIndex->load(); }
        const DesktopEntry* entry = nullptr;
        if (!app.desktopEntry.empty()) {
            entry = backends_.desktopIndex->resolve(app.desktopEntry);
        }
        if (entry == nullptr && !app.name.empty()) {
            entry = backends_.desktopIndex->resolve(app.name);
        }
        if (entry != nullptr) {
            launchDetached(*backends_.loop, entry->exec, entry->terminal);
            if (backends_.notificationActions != nullptr) {
                for (uint32_t daemonId : app.daemonIds) {
                    if (daemonId != 0) { backends_.notificationActions->close(daemonId); }
                }
            }
            return true;
        }
    }

    // 3. Fallbacks for known apps
    if (backends_.loop != nullptr) {
        const std::string lower = toLower(app.name);
        if (lower.find("whatsapp") != std::string::npos) {
            launchDetached(*backends_.loop, "xdg-open https://web.whatsapp.com", false);
            return true;
        }
        if (lower.find("mail") != std::string::npos || lower.find("email") != std::string::npos) {
            launchDetached(*backends_.loop, "xdg-open mailto:", false);
            return true;
        }
        if (lower.find("kdeconnect") != std::string::npos ||
            lower.find("kde-connect") != std::string::npos) {
            launchDetached(*backends_.loop, "kdeconnect-app", false);
            return true;
        }
    }

    return true;
}

bool NotificationAppsIndicator::onSecondaryClick(double x, double y) {
    (void)y;
    const int idx = hitTest(x);
    if (idx < 0 || std::cmp_greater_equal(idx, apps_.size())) return false;
    const auto& app = apps_[idx];

    if (backends_.notificationActions != nullptr) {
        for (uint32_t daemonId : app.daemonIds) {
            if (daemonId != 0) { backends_.notificationActions->close(daemonId); }
        }
    }
    return true;
}

bool NotificationAppsIndicator::onMiddleClick(double x, double y) {
    return onSecondaryClick(x, y);
}

void NotificationAppsIndicator::onHoverMotion(double x, double y) {
    hoverX_ = x;
    hoverY_ = y;
}

void NotificationAppsIndicator::onHoverLeave() {
    hoverX_ = -1.0;
    hoverY_ = -1.0;
}

REGISTER_INDICATOR("notification-apps", Zone::Right, 640, NotificationAppsIndicator)

}  // namespace qypr
