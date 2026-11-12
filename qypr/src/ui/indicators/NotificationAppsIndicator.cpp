#include "ui/indicators/NotificationAppsIndicator.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {
REGISTER_SESSION_INDICATOR("notification-apps", Zone::Right, 640, NotificationAppsIndicator)
}  // namespace qypr
