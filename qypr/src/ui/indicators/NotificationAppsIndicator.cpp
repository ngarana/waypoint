// NotificationAppsIndicator.cpp - Every app with something to say.
#include "ui/indicators/NotificationAppsIndicator.hpp"

#include <algorithm>

#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/StatusArea.hpp"

namespace qypr {

NotificationAppsIndicator::NotificationAppsIndicator(const SystemBackends& backends)
    : AppStatusIndicator(backends, "notification-apps", 640) {}

std::vector<AppStatus> NotificationAppsIndicator::select(std::vector<AppStatus> all) const {
    std::vector<std::vector<std::string>> claimed;
    if (backends_.statusArea != nullptr) {
        for (const auto& module : StatusArea::appModules()) {
            claimed.push_back(backends_.statusArea->claimPatterns(module));
        }
    }
    std::erase_if(all, [&](const AppStatus& app) {
        if (!app.hasNews()) return true;
        return std::ranges::any_of(
            claimed, [&](const std::vector<std::string>& p) { return app.matchesAny(p); });
    });
    return all;
}

REGISTER_SESSION_INDICATOR("notification-apps", Zone::Right, 640, NotificationAppsIndicator)

}  // namespace qypr
