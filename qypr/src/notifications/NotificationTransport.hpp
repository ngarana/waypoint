// NotificationTransport.hpp - The monitor D-Bus connection (sd-bus only).
//
// A dedicated connection that calls BecomeMonitor with the match rules in the
// call (the D-Bus spec forbids sending afterwards, so AddMatch-after-the-fact
// can never work), then plugs its fd into the app's EventLoop. Parsed events
// flow to the handlers; a bus error reports onLost and the owner tears down.
// No policy, no storage, no theme.

#pragma once

#include <cstdint>
#include <functional>

struct sd_bus;
struct sd_bus_message;
struct sd_bus_slot;
struct sd_bus_error;

#include "notifications/NotificationParser.hpp"

namespace qypr {

class EventLoop;

class NotificationTransport {
public:
    struct Handlers {
        std::function<void(const NotifyEvent&)> onNotify;
        std::function<void(const ReturnEvent&)> onReturn;
        std::function<void(const ClosedEvent&)> onClosed;
        std::function<void()> onLost;  // bus error -> owner teardown
    };
    explicit NotificationTransport(EventLoop& loop);
    ~NotificationTransport();

    NotificationTransport(const NotificationTransport&) = delete;
    NotificationTransport& operator=(const NotificationTransport&) = delete;

    void setHandlers(Handlers handlers) { handlers_ = std::move(handlers); }

    // Connect to the session bus and become a monitor. False when the bus or
    // the Monitoring interface is unavailable; the transport then stays inert.
    bool start();
    // Drop the connection (safe to call inert or twice; deferred-safe: never
    // call from inside the fd callback itself — post it like drain() does).
    void teardown();

private:
    // sd-bus filter: sees every monitored message during sd_bus_process().
    static int onMessage(sd_bus_message* m, void* userdata, sd_bus_error* err);
    void drain();  // process all queued bus messages; reports onLost on error

    EventLoop& loop_;
    Handlers handlers_;
    sd_bus* bus_ = nullptr;
    sd_bus_slot* filter_ = nullptr;
    int fd_ = -1;
};

}  // namespace qypr
