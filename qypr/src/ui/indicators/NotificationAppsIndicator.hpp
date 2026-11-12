// NotificationAppsIndicator.hpp - Persistent application status area.
#pragma once

#include "ui/indicators/SNITrayHost.hpp"

namespace qypr {

// Keep existing configurations working while sharing the system-tray protocol
// implementation. Toasts and their history belong to NotificationIndicator.
class NotificationAppsIndicator : public SNITrayHost {
public:
    explicit NotificationAppsIndicator(const SystemBackends& backends) : SNITrayHost(backends) {
        id_ = "notification-apps";
        priority_ = 640;
    }
};

}  // namespace qypr
