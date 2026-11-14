// AppStatusIndicator.cpp - App buttons: badges, progress, focus/launch, menus.
#include "ui/indicators/AppStatusIndicator.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

#include "notifications/NotificationActions.hpp"
#include "notifications/NotificationMonitor.hpp"
#include "render/IconResolver.hpp"
#include "render/Painter.hpp"
#include "system/ApplicationsBackend.hpp"
#include "system/DesktopIndex.hpp"
#include "system/ToplevelBackend.hpp"
#include "ui/Theme.hpp"
#include "ui/indicators/AppTile.hpp"
#include "ui/statusbar/MenuPopover.hpp"
#include "ui/statusbar/MenuSource.hpp"

namespace qypr {

namespace {

constexpr double kBtnW = 28.0;    // button width per app
constexpr double kIconPx = 18.0;  // icon render size
constexpr double kGap = 4.0;      // gap between apps
constexpr double kSidePad = 4.0;  // left/right padding

// A notification icon that is a font glyph rather than an icon name or path.
bool isGlyph(const std::string& s) {
    if (s.empty()) return false;
    return std::ranges::all_of(s, [](unsigned char c) {
        return !std::isalpha(c) && c != '-' && c != '_' && c != '.' && c != '/';
    });
}

cairo_surface_t* resolveSurface(const AppStatus& app) {
    if (auto* s = apptile::resolveIcon(app.desktopId)) return s;
    if (!app.icon.empty() && !isGlyph(app.icon)) {
        if (auto* s = apptile::resolveIcon(app.icon)) return s;
        if (auto* s = IconResolver::instance().get(app.icon)) return s;
    }
    return apptile::resolveIcon(app.key);
}

void drawBadge(Painter& p, const theme::State& t, double right, double top, const std::string& text,
               Color bg) {
    const TextStyle style{t.font.family, 8.5, PANGO_WEIGHT_BOLD, t.colors.background};
    const Size sz = p.measureText(text, style);
    const double h = 12.0;
    const double w = std::max(h, sz.w + 6.0);
    const Rect r{right - w, top, w, h};
    p.fillRoundedRect(r, h / 2.0, bg);
    p.drawText(r.x + ((w - sz.w) / 2.0), r.y + ((h - sz.h) / 2.0), text, style);
}

}  // namespace

AppStatusIndicator::AppStatusIndicator(const SystemBackends& backends, const std::string& id,
                                       int priority)
    : StatusIndicator(id, Zone::Right, priority),
      backends_(backends) {
    visible = false;
}

void AppStatusIndicator::onBackendUpdate() {
    const auto* notes =
        backends_.notifications != nullptr ? &backends_.notifications->notifications() : nullptr;
    const auto* launcher =
        backends_.applications != nullptr ? &backends_.applications->launcherEntries() : nullptr;
    const auto* windows =
        backends_.toplevel != nullptr ? &backends_.toplevel->snapshot().windows : nullptr;
    const bool news =
        (notes != nullptr && !notes->empty()) || (launcher != nullptr && !launcher->empty());
    if (!news && !tracksWindows()) {
        apps_.clear();
        visible = false;
        return;
    }
    // The index is read the first time an app has something to show, not at
    // startup (DesktopIndex::load()).
    DesktopIndex* index = backends_.desktopIndex;
    if (index != nullptr && !index->loaded() &&
        (news || (windows != nullptr && !windows->empty()))) {
        index->load();
    }
    apps_ = select(buildAppStatus(
        {.notifications = notes, .launcher = launcher, .windows = windows, .index = index}));
    visible = !apps_.empty();
}

int AppStatusIndicator::hitTest(double x) const {
    if (apps_.empty()) return -1;
    const double localX = x - (bounds.x + kSidePad);
    if (localX < 0) return -1;
    const int idx = static_cast<int>(localX / (kBtnW + kGap));
    if (idx < 0 || std::cmp_greater_equal(idx, apps_.size())) return -1;
    if (localX - (idx * (kBtnW + kGap)) > kBtnW) return -1;
    return idx;
}

std::string AppStatusIndicator::describe(const AppStatus& app) {
    std::vector<std::string> parts;
    if (app.badge > 0) { parts.push_back(std::to_string(app.badge) + " unread"); }
    if (app.notifications > 0) {
        parts.push_back(std::to_string(app.notifications) +
                        (app.notifications == 1 ? " notification" : " notifications"));
    }
    if (app.progress >= 0) {
        parts.push_back(std::to_string(static_cast<int>(std::lround(app.progress * 100))) + "%");
    }
    if (app.urgent) { parts.emplace_back("needs attention"); }
    if (parts.empty() && !app.windows.empty()) { parts.emplace_back("running"); }
    std::string tip = app.name;
    for (size_t i = 0; i < parts.size(); ++i) { tip += (i == 0 ? " (" : ", ") + parts[i]; }
    if (!parts.empty()) { tip += ')'; }
    if (!app.latestTitle.empty()) {
        tip += "\n" + app.latestTitle;
        if (!app.latestBody.empty() && app.latestBody != app.latestTitle) {
            tip += ": " + app.latestBody;
        }
    }
    return tip;
}

std::string AppStatusIndicator::tooltip() const {
    if (apps_.empty()) return emptyTooltip();
    const int idx = hoverX_ >= 0 ? hitTest(hoverX_) : -1;
    if (idx >= 0) return describe(apps_[static_cast<size_t>(idx)]);
    if (apps_.size() == 1) return describe(apps_.front());

    constexpr size_t kListed = 5;
    std::string tip = std::to_string(apps_.size()) + " apps:";
    for (size_t i = 0; i < apps_.size() && i < kListed; ++i) {
        tip += "\n• " + apps_[i].name;
        if (apps_[i].count() > 0) { tip += " (" + std::to_string(apps_[i].count()) + ")"; }
    }
    if (apps_.size() > kListed) {
        tip += "\n• … and " + std::to_string(apps_.size() - kListed) + " more";
    }
    return tip;
}

double AppStatusIndicator::measureWidth(Painter& /*p*/) {
    if (apps_.empty()) return 0.0;
    const auto n = static_cast<double>(apps_.size());
    return (n * kBtnW) + ((n - 1.0) * kGap) + (2.0 * kSidePad);
}

void AppStatusIndicator::draw(Painter& p, int64_t /*now*/) {
    if (!visible || apps_.empty()) return;
    const auto& c = theme().colors;
    double x = bounds.x + kSidePad;
    const double btnH = bounds.h - 4.0;
    const double btnY = bounds.y + 2.0;

    for (const auto& app : apps_) {
        const Rect btn{x, btnY, kBtnW, btnH};
        const bool hot = hoverX_ >= 0 && btn.contains(hoverX_, hoverY_);
        if (hot) { p.fillRoundedRect(btn, 6.0, c.glassHover); }

        const double iconX = x + ((kBtnW - kIconPx) / 2.0);
        const double iconY = bounds.y + ((bounds.h - kIconPx) / 2.0);
        const Rect iconRect{iconX, iconY, kIconPx, kIconPx};
        if (cairo_surface_t* s = resolveSurface(app)) {
            if (apptile::surfaceIsMonochrome(s) ||
                app.icon.find("-symbolic") != std::string::npos) {
                p.drawSurfaceTinted(s, iconRect, c.text);
            } else {
                p.drawSurface(s, iconRect);
            }
        } else if (isGlyph(app.icon)) {
            const TextStyle ts{.family = theme().font.iconFamily,
                               .size = kIconPx,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = app.critical ? c.red : c.text};
            const Size sz = p.measureText(app.icon, ts);
            p.drawText(iconX + ((kIconPx - sz.w) / 2.0), iconY + ((kIconPx - sz.h) / 2.0), app.icon,
                       ts);
        } else {
            p.fillRoundedRect(iconRect, 5.0,
                              app.critical ? c.red : apptile::fallbackColor(app.name, theme()));
            const TextStyle st{theme().font.family, 11.0, PANGO_WEIGHT_BOLD, c.background};
            const std::string initial = apptile::initialFor(app.name);
            const Size cs = p.measureText(initial, st);
            p.drawText(iconX + ((kIconPx - cs.w) / 2.0), iconY + ((kIconPx - cs.h) / 2.0), initial,
                       st);
        }

        // The app's own count shows from 1; a lone notification needs none.
        const int64_t n = app.count();
        Color badgeBg = c.primary;
        if (app.critical) {
            badgeBg = c.red;
        } else if (app.urgent) {
            badgeBg = c.warning;
        }
        if (app.badge > 0 || n > 1) {
            drawBadge(p, theme(), x + kBtnW, btnY + 1.0, n > 99 ? "99+" : std::to_string(n),
                      badgeBg);
        } else if (app.critical || app.urgent) {
            p.fillCircle(x + kBtnW - 4.0, btnY + 4.0, 3.0, badgeBg);
        }

        if (app.progress >= 0) {
            constexpr double kBarH = 2.0;
            const Rect track{iconX, iconY + kIconPx + 2.0, kIconPx, kBarH};
            p.fillRoundedRect(track, kBarH / 2.0, c.glassHover);
            p.fillRoundedRect({track.x, track.y, track.w * app.progress, kBarH}, kBarH / 2.0,
                              c.primary);
        }
        x += kBtnW + kGap;
    }
}

void AppStatusIndicator::open(const AppStatus& app) const {
    bool done = false;
    if (backends_.toplevel != nullptr && !app.windows.empty()) {
        // The window after the focused one, so repeated clicks cycle.
        const auto& windows = backends_.toplevel->snapshot().windows;
        size_t pick = 0;
        for (size_t i = 0; i < app.windows.size(); ++i) {
            const auto it = std::ranges::find(windows, app.windows[i], &ToplevelWindow::id);
            if (it != windows.end() && it->active) {
                pick = (i + 1) % app.windows.size();
                break;
            }
        }
        backends_.toplevel->activate(app.windows[pick]);
        done = true;
    }
    // A tray app with its window closed: its own Activate brings it back
    // instead of a second instance.
    if (const TrayAppBinding* t = tray(); !done && t != nullptr) {
        done = t->activate(bounds.x + (bounds.w / 2.0), bounds.y + (bounds.h / 2.0));
    }
    DesktopIndex* index = backends_.desktopIndex;
    if (!done && index != nullptr && !app.desktopId.empty()) {
        if (const DesktopEntry* e = index->byId(app.desktopId)) {
            if (backends_.applications != nullptr) {
                done = backends_.applications->activate(*e);
            } else if (backends_.loop != nullptr) {
                done = launchDetached(*backends_.loop, e->exec, e->terminal);
            }
        }
    }
    if (!done && backends_.loop != nullptr && !fallbackCommand().empty()) {
        launchDetached(*backends_.loop, fallbackCommand(), /*terminal=*/false);
    }
    dismiss(app);
}

void AppStatusIndicator::dismiss(const AppStatus& app) const {
    if (backends_.notificationActions == nullptr) return;
    for (const uint32_t id : app.daemonIds) { backends_.notificationActions->close(id); }
}

bool AppStatusIndicator::onClick(double x, double /*y*/) {
    const int idx = hitTest(x);
    if (idx < 0) return false;
    open(apps_[static_cast<size_t>(idx)]);
    return true;
}

bool AppStatusIndicator::onMiddleClick(double x, double /*y*/) {
    const int idx = hitTest(x);
    if (idx < 0) return false;
    dismiss(apps_[static_cast<size_t>(idx)]);
    return true;
}

std::unique_ptr<DetailedPopover> AppStatusIndicator::createContextMenu(double x, double /*y*/) {
    int idx = hitTest(x);
    if (idx < 0 && apps_.size() == 1) idx = 0;  // the Menu key on a one-app module
    if (idx < 0) return nullptr;
    const AppStatus app = apps_[static_cast<size_t>(idx)];
    auto menu = std::make_unique<CompositeMenu>();

    // The app's own verbs (New Window, Compose…), as its desktop file lists them.
    const DesktopEntry* entry = backends_.desktopIndex != nullptr && !app.desktopId.empty()
                                    ? backends_.desktopIndex->byId(app.desktopId)
                                    : nullptr;
    if (entry != nullptr && !entry->actions.empty()) {
        auto actions = std::make_unique<NativeMenu>();
        for (const auto& a : entry->actions) {
            actions->add(a.name,
                         [apps = backends_.applications, loop = backends_.loop, e = *entry, a] {
                             if (apps != nullptr) {
                                 apps->activateAction(e, a);
                             } else if (loop != nullptr) {
                                 launchDetached(*loop, a.exec, e.terminal);
                             }
                         });
        }
        menu->addSection(std::move(actions));
    }
    // What the app put in its LauncherEntry quicklist.
    if (backends_.dbusMenu != nullptr && !app.menuService.empty() && !app.menuPath.empty()) {
        menu->addSection(
            std::make_unique<DbusMenuSource>(backends_.dbusMenu, app.menuService, app.menuPath));
    }
    // The tray icon's menu, when this module claims the app's tray icon.
    if (const TrayAppBinding* t = tray()) {
        if (auto section = t->menuSection()) menu->addSection(std::move(section));
    }
    if (app.notifications > 0 && backends_.notificationActions != nullptr) {
        auto clear = std::make_unique<NativeMenu>();
        const std::string label =
            app.notifications == 1
                ? std::string("Clear notification")
                : "Clear " + std::to_string(app.notifications) + " notifications";
        clear->add(label, [actions = backends_.notificationActions, ids = app.daemonIds] {
            for (const uint32_t id : ids) { actions->close(id); }
        });
        menu->addSection(std::move(clear));
    }

    auto popover = std::make_unique<MenuPopover>(std::move(menu), app.name);
    if (!popover->hasItems()) return nullptr;
    return popover;
}

void AppStatusIndicator::onHoverMotion(double x, double y) {
    hoverX_ = x;
    hoverY_ = y;
}

void AppStatusIndicator::onHoverLeave() {
    hoverX_ = -1.0;
    hoverY_ = -1.0;
}

}  // namespace qypr
