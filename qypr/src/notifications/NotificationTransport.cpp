// NotificationTransport.cpp - See the header for the design.

#include "notifications/NotificationTransport.hpp"

#include <systemd/sd-bus.h>

#include <cstdio>
#include <cstring>

#include "core/EventLoop.hpp"

namespace qypr {

namespace {

// Ceiling for calls on the monitor connection. This one is short because the
// only call made here is BecomeMonitor, which the bus broker answers itself —
// no service activation to wait on, unlike SystemBus::kCallTimeoutUs.
constexpr uint64_t kCallTimeoutUs = 2'000'000;

}  // namespace

NotificationTransport::NotificationTransport(EventLoop& loop) : loop_(loop) {}

NotificationTransport::~NotificationTransport() {
    teardown();
}

bool NotificationTransport::start() {
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) {
        std::fprintf(stderr, "qypr-lock: no session bus; notifications disabled\n");
        return false;
    }

    // Spec: the match rules are the *argument* of BecomeMonitor. Once the
    // reply arrives this connection is receive-only.
    //
    // Bounded rather than left at sd-bus's 25-second default: this call blocks
    // the event loop, and at login the bar comes up alongside the notification
    // daemon and the bus itself. BecomeMonitor is answered by the broker
    // directly, so it normally returns instantly — waiting much longer means
    // something is wrong, and losing notifications beats freezing the bar.
    sd_bus_set_method_call_timeout(bus, kCallTimeoutUs);

    sd_bus_error err = SD_BUS_ERROR_NULL;
    int const r = sd_bus_call_method(
        bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus.Monitoring",
        "BecomeMonitor", &err, nullptr, "asu", 3U,
        "type='method_call',interface='org.freedesktop.Notifications',member='Notify'",
        "type='method_return',sender='org.freedesktop.Notifications'",
        "type='signal',interface='org.freedesktop.Notifications',member='NotificationClosed'", 0U);
    if (r < 0) {
        std::fprintf(stderr, "qypr-lock: BecomeMonitor failed (%s); notifications disabled\n",
                     (err.message != nullptr) ? err.message : strerror(-r));
        sd_bus_error_free(&err);
        sd_bus_unref(bus);
        return false;
    }

    if (sd_bus_add_filter(bus, &filter_, &NotificationTransport::onMessage, this) < 0) {
        sd_bus_unref(bus);
        return false;
    }

    bus_ = bus;
    fd_ = sd_bus_get_fd(bus_);
    loop_.addFd(fd_, [this](uint32_t) { drain(); });
    drain();  // messages may already sit in sd-bus's queue from the call above
    return true;
}

void NotificationTransport::teardown() {
    if (fd_ >= 0) {
        loop_.removeFd(fd_);
        fd_ = -1;
    }
    if (filter_ != nullptr) {
        sd_bus_slot_unref(filter_);
        filter_ = nullptr;
    }
    if (bus_ != nullptr) {
        sd_bus_unref(bus_);
        bus_ = nullptr;
    }
}

void NotificationTransport::drain() {
    int r = 0;
    while ((r = sd_bus_process(bus_, nullptr)) > 0) {}
    if (r < 0) {
        std::fprintf(stderr, "qypr-lock: notification monitor lost (%s)\n", strerror(-r));
        // Deferred: this runs from the fd callback removeFd would destroy.
        loop_.post([this] {
            if (handlers_.onLost) { handlers_.onLost(); }
        });
    }
}

int NotificationTransport::onMessage(sd_bus_message* m, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<NotificationTransport*>(userdata);
    uint8_t type = 0;
    sd_bus_message_get_type(m, &type);
    if (type == SD_BUS_MESSAGE_METHOD_CALL &&
        (sd_bus_message_is_method_call(m, "org.freedesktop.Notifications", "Notify") != 0)) {
        if (auto event = parseNotify(m); event.has_value() && self->handlers_.onNotify) {
            self->handlers_.onNotify(*event);
        }
    } else if (type == SD_BUS_MESSAGE_METHOD_RETURN) {
        if (auto event = parseReturn(m); event.has_value() && self->handlers_.onReturn) {
            self->handlers_.onReturn(*event);
        }
    } else if (sd_bus_message_is_signal(m, "org.freedesktop.Notifications", "NotificationClosed") !=
               0) {
        if (auto event = parseClosed(m); event.has_value() && self->handlers_.onClosed) {
            self->handlers_.onClosed(*event);
        }
    }
    return 1;  // consumed: nothing else may react to monitored traffic
}

}  // namespace qypr
