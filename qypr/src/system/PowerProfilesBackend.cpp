// PowerProfilesBackend.cpp - power-profiles-daemon client implementation.
#include "system/PowerProfilesBackend.hpp"

#include <systemd/sd-bus.h>

#include <cstring>
#include <utility>

#include "system/SystemBus.hpp"

namespace qypr {

namespace {
constexpr const char* kSvc = "net.hadess.PowerProfiles";
constexpr const char* kPath = "/net/hadess/PowerProfiles";
constexpr const char* kIface = "net.hadess.PowerProfiles";
constexpr const char* kProps = "org.freedesktop.DBus.Properties";

bool readVariantString(sd_bus_message* message, std::string* value) {
    if (sd_bus_message_enter_container(message, 'v', "s") <= 0) {
        sd_bus_message_skip(message, "v");
        return false;
    }
    const char* text = nullptr;
    const int result = sd_bus_message_read(message, "s", &text);
    sd_bus_message_exit_container(message);
    if (result < 0 || text == nullptr) { return false; }
    *value = text;
    return true;
}

bool readProfiles(sd_bus_message* message, std::vector<std::string>* profiles) {
    if (sd_bus_message_enter_container(message, 'v', "aa{sv}") <= 0) {
        sd_bus_message_skip(message, "v");
        return false;
    }
    bool valid = sd_bus_message_enter_container(message, 'a', "a{sv}") > 0;
    if (valid) {
        while (sd_bus_message_enter_container(message, 'a', "{sv}") > 0) {
            while (sd_bus_message_enter_container(message, 'e', "sv") > 0) {
                const char* key = nullptr;
                sd_bus_message_read(message, "s", &key);
                const bool isProfile = key != nullptr && std::strcmp(key, "Profile") == 0;
                if (isProfile) {
                    std::string name;
                    if (readVariantString(message, &name)) { profiles->push_back(std::move(name)); }
                }
                if (!isProfile) { sd_bus_message_skip(message, "v"); }
                sd_bus_message_exit_container(message);
            }
            sd_bus_message_exit_container(message);
        }
        sd_bus_message_exit_container(message);
    }
    sd_bus_message_exit_container(message);
    return valid;
}
}  // namespace

PowerProfilesBackend::PowerProfilesBackend(EventLoop& loop, SystemBus& systemBus)
    : bus_(systemBus),
      retry_(loop, [this] { refresh(); }) {}

PowerProfilesBackend::~PowerProfilesBackend() {
    retry_.cancel();
    sd_bus_slot_unref(slot_);
    sd_bus_slot_unref(ownerSlot_);
}

bool PowerProfilesBackend::start() {
    if (!bus_.available()) {
        publish({});
        return false;
    }
    if (started_) {
        refresh();
        return snap_.available;
    }
    started_ = true;
    // Push: any property change (usually ActiveProfile) triggers a re-read.
    slot_ = bus_.addMatch("type='signal',sender='net.hadess.PowerProfiles',"
                          "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',"
                          "path='/net/hadess/PowerProfiles'",
                          &PowerProfilesBackend::onPropsChanged, this);
    ownerSlot_ = bus_.addMatch(
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',arg0='net.hadess.PowerProfiles'",
        &PowerProfilesBackend::onNameOwnerChanged, this);
    refresh();
    return snap_.available;
}

int PowerProfilesBackend::onPropsChanged(sd_bus_message*, void* ud, sd_bus_error*) {
    static_cast<PowerProfilesBackend*>(ud)->refresh();
    return 0;
}

int PowerProfilesBackend::onNameOwnerChanged(sd_bus_message* message, void* userdata,
                                             sd_bus_error*) {
    auto* self = static_cast<PowerProfilesBackend*>(userdata);
    const char* name = nullptr;
    const char* oldOwner = nullptr;
    const char* newOwner = nullptr;
    if (sd_bus_message_read(message, "sss", &name, &oldOwner, &newOwner) < 0 || name == nullptr) {
        return 0;
    }
    if (newOwner == nullptr || *newOwner == '\0') {
        self->publish({});
        self->retry_.schedule();
        return 0;
    }
    self->refresh();
    return 0;
}

void PowerProfilesBackend::publish(PowerProfilesSnapshot&& next) {
    if (next == snap_) return;
    snap_ = std::move(next);
    if (onChange_) onChange_();
}

void PowerProfilesBackend::refresh() {  // NOLINT(misc-no-recursion) async D-Bus reply cycle
    sd_bus* bus = bus_.get();
    if (!bus) return;
    retry_.cancel();
    if (fetchInFlight_) {
        pendingRefresh_ = true;
        return;
    }
    fetchInFlight_ = true;
    const int result =
        sd_bus_call_method_async(bus, nullptr, kSvc, kPath, kProps, "GetAll",
                                 &PowerProfilesBackend::onRefreshReply, this, "s", kIface);
    if (result < 0) { endRefresh(true); }
}

int PowerProfilesBackend::onRefreshReply(sd_bus_message* reply, void* userdata,
                                         sd_bus_error*) {  // NOLINT(misc-no-recursion) async reply
    auto* self = static_cast<PowerProfilesBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        self->publish({});
        self->endRefresh(true);
        return 0;
    }

    PowerProfilesSnapshot next;
    bool activeRead = false;
    bool profilesRead = false;
    if (sd_bus_message_enter_container(reply, 'a', "{sv}") > 0) {
        while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
            const char* key = nullptr;
            sd_bus_message_read(reply, "s", &key);
            const bool isActive = key != nullptr && std::strcmp(key, "ActiveProfile") == 0;
            const bool isProfiles = key != nullptr && std::strcmp(key, "Profiles") == 0;
            if (isActive) {
                activeRead = readVariantString(reply, &next.active);
            } else if (isProfiles) {
                profilesRead = readProfiles(reply, &next.profiles);
            }
            if (!isActive && !isProfiles) { sd_bus_message_skip(reply, "v"); }
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply);
    }
    next.available = activeRead && !next.active.empty();
    self->publish(std::move(next));
    self->endRefresh(activeRead && profilesRead);
    return 0;
}

// NOLINTNEXTLINE(misc-no-recursion) Async D-Bus replies resume on the event loop.
void PowerProfilesBackend::endRefresh(bool success) {
    fetchInFlight_ = false;
    if (success) {
        retry_.reset();
    } else if (started_) {
        retry_.schedule();
    }
    if (pendingRefresh_) {
        pendingRefresh_ = false;
        refresh();
    }
}

void PowerProfilesBackend::setActiveProfile(const std::string& name) {
    sd_bus* bus = bus_.get();
    if (!bus || name.empty()) return;
    // Properties.Set(interface, "ActiveProfile", variant<s>) — async.
    sd_bus_call_method_async(bus, nullptr, kSvc, kPath, kProps, "Set", nullptr, nullptr, "ssv",
                             kIface, "ActiveProfile", "s", name.c_str());
    // Optimistic; PropertiesChanged confirms.
    if (snap_.active != name) {
        snap_.active = name;
        if (onChange_) onChange_();
    }
}

}  // namespace qypr
