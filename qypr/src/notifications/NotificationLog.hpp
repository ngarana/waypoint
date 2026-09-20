// NotificationLog.hpp - D-Bus service exposing the recorder's mirror.
//
// The `qypr-lock --record` session service runs a NotificationMonitor from
// login onward, mirroring the daemon's queue (captures minus dismissals).
// This class publishes that mirror as org.qypr.Notifications so a lock screen
// starting later can seed itself with the pre-lock backlog. Runs on the same
// EventLoop/thread as the monitor it reads — no locking anywhere.

#pragma once

#include <systemd/sd-bus.h>

namespace qypr {

class EventLoop;
class NotificationMonitor;

// Wire protocol shared by the service (here) and the client
// (NotificationMonitor::start's backlog fetch).
namespace notiflog {
inline constexpr char kBusName[] = "org.qypr.Notifications";
inline constexpr char kObjectPath[] = "/org/qypr/Notifications";
inline constexpr char kInterface[] = "org.qypr.Notifications";
// One record: postedAt(ms), app, title, body, icon, daemon id, urgency, sensitive.
inline constexpr char kRecord[] = "(xssssuyb)";
// D-Bus method return type for List(): array of records.
inline constexpr char kListReturn[] = "a(xssssuyb)";
}  // namespace notiflog

class NotificationLog {
public:
    NotificationLog(EventLoop& loop, const NotificationMonitor& monitor)
        : loop_(loop),
          monitor_(monitor) {}
    ~NotificationLog();

    NotificationLog(const NotificationLog&) = delete;
    NotificationLog& operator=(const NotificationLog&) = delete;

    // Own the bus name and start serving List(). False when the bus is
    // unavailable or another recorder already owns the name.
    bool start();

private:
    static int onList(sd_bus_message* m, void* userdata, sd_bus_error* err);
    void drain();

    EventLoop& loop_;
    const NotificationMonitor& monitor_;
    sd_bus* bus_ = nullptr;
    sd_bus_slot* vtableSlot_ = nullptr;
    int fd_ = -1;
};

}  // namespace qypr
