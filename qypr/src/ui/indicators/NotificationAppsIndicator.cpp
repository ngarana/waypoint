// NotificationAppsIndicator.cpp
#include "ui/indicators/NotificationAppsIndicator.hpp"

#include "notifications/NotificationMonitor.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

#include <algorithm>
#include <map>
#include <string>

namespace qypr {

NotificationAppsIndicator::NotificationAppsIndicator(const SystemBackends& backends)
    : StatusIndicator("notification-apps", Zone::Right, 640),
      monitor_(backends.notifications) {
    visible = monitor_ != nullptr;
}

void NotificationAppsIndicator::onBackendUpdate() {
    apps_.clear();
    if (monitor_ == nullptr) {
        visible = false;
        return;
    }
    std::map<std::string, AppEntry> byApp;
    for (const auto& n : monitor_->notifications()) {
        const std::string key = n.app.empty() ? "App" : n.app;
        auto& e = byApp[key];
        e.name = key;
        if (e.glyph.empty()) { e.glyph = n.icon.empty() ? std::string("󰂚") : n.icon; }
        ++e.count;
    }
    for (const auto& [k, v] : byApp) { apps_.push_back(v); }
    visible = !apps_.empty();
}

std::string NotificationAppsIndicator::tooltip() const {
    if (apps_.empty()) return "No app notifications";
    if (apps_.size() == 1) return apps_.front().name + " notifications";
    return std::to_string(apps_.size()) + " apps with notifications";
}

double NotificationAppsIndicator::measureWidth(Painter& p) {
    (void)p;
    if (apps_.empty()) return 0.0;
    const double iconPx = theme().statusbar.iconSize;
    const double gap = 6.0;
    return (static_cast<double>(apps_.size()) * iconPx) +
           (static_cast<double>(apps_.size() - 1) * gap) + 8.0;
}

void NotificationAppsIndicator::draw(Painter& p, int64_t now) {
    if (!visible || apps_.empty()) return;
    const double iconPx = theme().statusbar.iconSize;
    const double gap = 6.0;
    double x = bounds.x + 4.0;
    const double y = bounds.y + ((bounds.h - iconPx) / 2.0);
    const TextStyle ts{.family = theme().font.family,
                       .size = iconPx,
                       .weight = PANGO_WEIGHT_NORMAL,
                       .color = theme().colors.text};
    for (const auto& app : apps_) {
        p.drawText(x, y, app.glyph, ts, HAlign::Left);
        if (app.count > 1) {
            const TextStyle small{.family = theme().font.family,
                                  .size = 9.0,
                                  .weight = PANGO_WEIGHT_BOLD,
                                  .color = theme().colors.primary};
            p.drawText(x + iconPx - 4.0, y - 2.0, std::to_string(app.count), small, HAlign::Left);
        }
        x += iconPx + gap;
    }
}

bool NotificationAppsIndicator::onClick(double x, double y) {
    (void)x;
    (void)y;
    return true;
}

REGISTER_INDICATOR("notification-apps", Zone::Right, 640, NotificationAppsIndicator)

}  // namespace qypr
