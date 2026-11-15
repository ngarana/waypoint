// NotificationAppsIndicator.hpp - Every app with something to say.
//
// One button per app with live notifications, an unread count, progress or
// urgency (AppStatus). Apps that a whatsapp or email module in the layout
// shows are left to that module. Gestures: AppStatusIndicator.
#pragma once

#include "ui/indicators/AppStatusIndicator.hpp"

namespace qypr {

class NotificationAppsIndicator : public AppStatusIndicator {
public:
    explicit NotificationAppsIndicator(const SystemBackends& backends);

protected:
    [[nodiscard]] std::vector<AppStatus> select(std::vector<AppStatus> all) const override;
    [[nodiscard]] std::string emptyTooltip() const override { return "No app notifications"; }
};

}  // namespace qypr
