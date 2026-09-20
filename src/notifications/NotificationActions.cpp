#include "notifications/NotificationActions.hpp"

#include <systemd/sd-bus.h>

#include "system/SystemBus.hpp"

namespace qypr {

void NotificationActions::close(uint32_t daemonId) {
    if (daemonId == 0 || !bus_.available()) { return; }

    // Async and reply-less: dismissing must never block the UI thread, and the
    // outcome arrives as a NotificationClosed signal on the monitor anyway.
    sd_bus_call_method_async(bus_.get(), nullptr, "org.freedesktop.Notifications",
                             "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                             "CloseNotification", nullptr, nullptr, "u", daemonId);
}

void NotificationActions::invokeLatestAction(uint32_t actionIndex) {
    if (!bus_.available()) { return; }

    // swaync control interface: LatestInvokeAction(u index) fires the action on
    // the most-recent notification. Async and reply-less — a wrong daemon simply
    // has no such service and the call is dropped (UnknownService), never blocks.
    sd_bus_call_method_async(bus_.get(), nullptr, "org.erikreider.swaync.cc",
                             "/org/erikreider/swaync/cc", "org.erikreider.swaync.cc",
                             "LatestInvokeAction", nullptr, nullptr, "u", actionIndex);
}

}  // namespace qypr
