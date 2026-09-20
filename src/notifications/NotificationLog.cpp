// NotificationLog.cpp - See the header for the design.

#include "notifications/NotificationLog.hpp"

#include <cstdio>
#include <cstring>

#include "core/EventLoop.hpp"
#include "notifications/NotificationMonitor.hpp"

namespace qypr {

NotificationLog::~NotificationLog() {
    if (fd_ >= 0) { loop_.removeFd(fd_); }
    if (vtableSlot_ != nullptr) { sd_bus_slot_unref(vtableSlot_); }
    if (bus_ != nullptr) { sd_bus_unref(bus_); }
}

bool NotificationLog::start() {
    if (sd_bus_open_user(&bus_) < 0) {
        std::fprintf(stderr, "qypr-record: no session bus\n");
        return false;
    }
    static const sd_bus_vtable kVtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("List", "", notiflog::kListReturn, &NotificationLog::onList,
                      SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END,
    };
    if (sd_bus_add_object_vtable(bus_, &vtableSlot_, notiflog::kObjectPath, notiflog::kInterface,
                                 kVtable, this) < 0) {
        return false;
    }
    int const r = sd_bus_request_name(bus_, notiflog::kBusName, 0);
    if (r < 0) {
        std::fprintf(stderr, "qypr-record: cannot own %s (%s) — already running?\n",
                     notiflog::kBusName, strerror(-r));
        return false;
    }
    fd_ = sd_bus_get_fd(bus_);
    loop_.addFd(fd_, [this](uint32_t) { drain(); });
    return true;
}

void NotificationLog::drain() {
    while (sd_bus_process(bus_, nullptr) > 0) {}
}

int NotificationLog::onList(sd_bus_message* m, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<NotificationLog*>(userdata);
    sd_bus_message* reply = nullptr;
    if (sd_bus_message_new_method_return(m, &reply) < 0) { return -ENOMEM; }
    sd_bus_message_open_container(reply, 'a', notiflog::kRecord);
    for (const auto& n : self->monitor_.notifications()) {
        sd_bus_message_append(reply, notiflog::kRecord, n.postedAt, n.app.c_str(), n.title.c_str(),
                              n.body.c_str(), n.icon.c_str(), n.daemonId, n.urgency,
                              static_cast<int>(n.sensitive));
    }
    sd_bus_message_close_container(reply);
    sd_bus_send(nullptr, reply, nullptr);
    sd_bus_message_unref(reply);
    return 1;
}

}  // namespace qypr
