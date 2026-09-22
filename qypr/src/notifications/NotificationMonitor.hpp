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
// Spec behaviours honoured (Desktop Notifications 1.2):
//   - `replaces_id` updates the existing card in place (no re-animation),
//   - the daemon's Notify reply is correlated back so cards learn their
//     daemon-assigned id,
//   - NotificationClosed removes cards the user dismissed or apps closed
//     (expired popups stay: while locked, this stack *is* the user's queue),
//   - the `urgency` hint styles critical cards; `transient` ones are skipped.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <systemd/sd-bus.h>

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
    // `--record` service (org.qypr.Notifications) — done on this same
    // connection just before BecomeMonitor makes it receive-only.
    bool start(bool seedFromLog = false);

    // Current notifications, oldest → newest. Only mutated on the loop thread.
    const std::vector<Notification>& notifications() const { return notes_; }

    // Fired on the loop thread whenever the set above changes.
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

private:
    // sd-bus filter: sees every monitored message during sd_bus_process().
    static int onMessage(sd_bus_message* m, void* userdata, sd_bus_error*);

    void handleNotify(sd_bus_message* m);
    void handleReturn(sd_bus_message* m);
    void handleClosed(sd_bus_message* m);
    void fetchBacklog();

    void drain();     // process all queued bus messages; tears down on error
    void teardown();  // drop the connection (deferred out of fd callbacks)
    void changed() {
        if (onChange_) onChange_();
    }

    EventLoop& loop_;
    sd_bus* bus_ = nullptr;
    sd_bus_slot* filter_ = nullptr;
    int fd_ = -1;

    std::vector<Notification> notes_;
    uint64_t nextKey_ = 1;  // local card identity (view reconciliation)

    struct PendingCall {
        std::string sender;
        uint64_t cookie = 0;
        uint64_t id = 0;
    };
    std::vector<PendingCall> pending_;

    std::function<void()> onChange_;
};

}  // namespace qypr
