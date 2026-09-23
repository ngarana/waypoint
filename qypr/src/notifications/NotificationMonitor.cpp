// NotificationMonitor.cpp - Composition host; see the header for the design.

#include "notifications/NotificationMonitor.hpp"

#include <systemd/sd-bus.h>

#include <cstdio>
#include <cstring>

#include "core/Config.hpp"
#include "notifications/NotificationLog.hpp"

namespace qypr {

NotificationMonitor::NotificationMonitor(EventLoop& loop) : transport_(loop) {}

NotificationMonitor::~NotificationMonitor() = default;

bool NotificationMonitor::start(bool seedFromLog) {
    policy_.load(Config::configDir() + "/sensitive_apps.conf");
    // Pre-lock backlog from a running --record service, if any. Fetched over
    // its own connection, so it is independent of the monitor connection.
    if (seedFromLog) { fetchBacklog(); }

    NotificationTransport::Handlers handlers;
    handlers.onNotify = [this](const NotifyEvent& e) {
        onNotify(e);
    };
    handlers.onReturn = [this](const ReturnEvent& r) {
        onReturn(r);
    };
    handlers.onClosed = [this](const ClosedEvent& e) {
        onClosed(e);
    };
    handlers.onLost = [this]() {
        transport_.teardown();
    };
    transport_.setHandlers(std::move(handlers));
    return transport_.start();
}

void NotificationMonitor::onNotify(const NotifyEvent& event) {
    const bool sensitive = event.hints.sensitive || policy_.isSensitiveApp(event.app);
    if (store_.add(event, sensitive, theme()) != 0) { changed(); }
}

void NotificationMonitor::onReturn(const ReturnEvent& r) {
    if (!store_.hasPending()) { return; }
    store_.attachDaemonId(r);
}

void NotificationMonitor::onClosed(const ClosedEvent& e) {
    if (store_.close(e.id, e.reason)) { changed(); }
}

void NotificationMonitor::onLost() {
    transport_.teardown();
}

void NotificationMonitor::fetchBacklog() {
    // Use a dedicated, short-lived connection for the List() call. A previous
    // regression showed that a failed/version-skewed deserialization on the
    // monitor connection could cascade into BecomeMonitor failing and killing
    // all notifications; isolating the fetch here makes that impossible.
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) { return; }

    sd_bus_message* reply = nullptr;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    if (sd_bus_call_method(bus, notiflog::kBusName, notiflog::kObjectPath, notiflog::kInterface,
                           "List", &err, &reply, "") < 0) {
        std::fprintf(stderr,
                     "qypr-lock: no notification log service (%s); "
                     "pre-lock backlog unavailable\n",
                     (err.message != nullptr) ? err.message : "?");
        sd_bus_error_free(&err);
        sd_bus_unref(bus);
        return;
    }
    // enter_container only succeeds when the element type matches exactly, so an
    // older record service (7-field records without an icon) is skipped cleanly
    // rather than mis-parsed.
    std::vector<Notification> backlog;
    if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, notiflog::kRecord) > 0) {
        int64_t postedAt = 0;
        const char* app = nullptr;
        const char* title = nullptr;
        const char* body = nullptr;
        const char* icon = nullptr;
        uint32_t daemonId = 0;
        uint8_t urgency = 1;
        int sensitive = 0;
        while (sd_bus_message_read(reply, notiflog::kRecord, &postedAt, &app, &title, &body, &icon,
                                   &daemonId, &urgency, &sensitive) > 0) {
            Notification n;
            n.postedAt = postedAt;
            n.app = (app != nullptr) ? app : "";
            n.title = (title != nullptr) ? title : "";
            n.body = (body != nullptr) ? body : "";
            n.icon = (icon != nullptr) ? icon : "";
            n.daemonId = daemonId;
            n.urgency = urgency;
            n.sensitive = sensitive != 0;
            backlog.push_back(std::move(n));
        }
        sd_bus_message_exit_container(reply);
        if (store_.seed(std::move(backlog), theme())) { changed(); }
    }
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
}

}  // namespace qypr
