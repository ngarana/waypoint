// NotificationMonitor.hpp - Spec-correct observer of the notification bus.
//
// Watches the org.freedesktop.Notifications traffic of whatever daemon is
// running (SwayNC, dunst, mako, ...) the same way `busctl monitor` does: a
// dedicated sd-bus connection calls org.freedesktop.DBus.Monitoring
// .BecomeMonitor with the match rules passed *in the call*, as the D-Bus spec
// requires (a monitor connection may not send messages afterwards, so
// AddMatch-after-the-fact can never work). The connection's fd plugs into the
// app's EventLoop — everything runs on the UI thread; no background loop, no
// locking, no fake data.
//
// Composition host: the transport owns the connection, the policy owns the
// privacy rules, and the store owns the cards. This class wires them to the
// loop and the theme and publishes changes.
//
// Spec behaviours honoured (Desktop Notifications 1.2):
//   - `replaces_id` updates the existing card in place (no re-animation),
//   - the daemon's Notify reply is correlated back so cards learn their
//     daemon-assigned id,
//   - NotificationClosed removes cards the user dismissed or apps closed
//     (expired popups stay: while locked, this stack *is* the user's queue),
//   - the `urgency` hint styles critical cards; `transient` ones are skipped.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "notifications/NotificationPolicy.hpp"
#include "notifications/NotificationStore.hpp"
#include "notifications/NotificationTransport.hpp"
#include "ui/Notification.hpp"
#include "ui/Theme.hpp"

namespace qypr {

class EventLoop;

class NotificationMonitor : public theme::ThemeAware {
public:
    explicit NotificationMonitor(EventLoop& loop);
    ~NotificationMonitor();

    NotificationMonitor(const NotificationMonitor&) = delete;
    NotificationMonitor& operator=(const NotificationMonitor&) = delete;

    // Connect to the session bus and become a monitor. False when the bus or
    // the Monitoring interface is unavailable; the monitor then stays inert.
    // With `seedFromLog`, the pre-lock backlog is first fetched from a running
    // `--record` service (org.qypr.Notifications) — done on a separate
    // short-lived connection just before BecomeMonitor makes the monitor
    // connection receive-only.
    bool start(bool seedFromLog = false);

    // Current notifications, oldest → newest. Only mutated on the loop thread.
    const std::vector<Notification>& notifications() const { return store_.notes(); }
    // Test seam for indicator tests (see NotificationStore::testNotes).
    std::vector<Notification>& testNotes() { return store_.testNotes(); }

    // Fired on the loop thread whenever the set above changes.
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    void teardown() { transport_.teardown(); }

private:
    void onNotify(const NotifyEvent& e);
    void onReturn(const ReturnEvent& r);
    void onClosed(const ClosedEvent& e);
    void onLost();
    void fetchBacklog();

    void changed() {
        if (onChange_) onChange_();
    }

    NotificationTransport transport_;
    NotificationPolicy policy_;
    NotificationStore store_;
    std::function<void()> onChange_;
};

}  // namespace qypr
