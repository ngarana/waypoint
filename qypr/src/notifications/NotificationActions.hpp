// NotificationActions.hpp - Send-side companion to NotificationMonitor.
//
// NotificationMonitor's connection is a *monitor* (BecomeMonitor): the D-Bus
// spec forbids it from ever sending a message, so it can watch notifications but
// can never dismiss one. This class is the other half — a normal caller on the
// shared session bus that invokes the daemon's CloseNotification.
//
// Daemon-agnostic: org.freedesktop.Notifications is the freedesktop spec
// interface every daemon implements (SwayNC, dunst, mako, …), not a
// daemon-specific API (STATUS_BAR.md decision D6).
//
// Fire-and-forget: the daemon answers by emitting NotificationClosed, which the
// monitor already observes — so the card disappears through the normal push
// path and this class holds no state of its own.

#pragma once

#include <cstdint>
#include <string>

namespace qypr {

class SystemBus;

class NotificationActions {
public:
    explicit NotificationActions(SystemBus& sessionBus) : bus_(sessionBus) {}

    // Ask the daemon to close `daemonId` (the id it assigned via its Notify
    // reply). A zero id means the reply has not landed yet — nothing to close.
    void close(uint32_t daemonId);

    // Invoke the action at `actionIndex` (0-based, into the notification's full
    // actions array) on the *newest* notification.
    //
    // Third-party action invocation is not in the freedesktop Notifications spec
    // (actions are delivered server→client via the ActionInvoked signal, which
    // only the daemon may emit), so this is necessarily daemon-specific. swaync —
    // the common wlroots daemon — exposes LatestInvokeAction(index) on its
    // control interface; it targets the most-recent notification only, which is
    // why callers must gate this to the newest card. A no-op (harmless) when
    // swaync is not the running daemon. Returns nothing: fire-and-forget.
    void invokeLatestAction(uint32_t actionIndex);

private:
    SystemBus& bus_;
};

}  // namespace qypr
