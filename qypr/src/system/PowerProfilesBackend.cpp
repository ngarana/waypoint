// PowerProfilesBackend.cpp - power-profiles-daemon client implementation.
#include "system/PowerProfilesBackend.hpp"

#include <systemd/sd-bus.h>

#include <cstring>

#include "system/SystemBus.hpp"

namespace qypr {

namespace {
constexpr const char* kSvc = "net.hadess.PowerProfiles";
constexpr const char* kPath = "/net/hadess/PowerProfiles";
constexpr const char* kIface = "net.hadess.PowerProfiles";
constexpr const char* kProps = "org.freedesktop.DBus.Properties";
}  // namespace

PowerProfilesBackend::~PowerProfilesBackend() {
    if (slot_) sd_bus_slot_unref(slot_);
}

bool PowerProfilesBackend::start() {
    if (!bus_.available()) return false;
    refresh();
    if (!snap_.available) return false;  // daemon absent → caller omits the UI
    // Push: any property change (usually ActiveProfile) triggers a re-read.
    slot_ = bus_.addMatch("type='signal',sender='net.hadess.PowerProfiles',"
                          "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',"
                          "path='/net/hadess/PowerProfiles'",
                          &PowerProfilesBackend::onPropsChanged, this);
    return true;
}

int PowerProfilesBackend::onPropsChanged(sd_bus_message*, void* ud, sd_bus_error*) {
    static_cast<PowerProfilesBackend*>(ud)->refresh();
    return 0;
}

void PowerProfilesBackend::refresh() {
    sd_bus* bus = bus_.get();
    if (!bus) return;

    PowerProfilesSnapshot next;

    // ActiveProfile (s).
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        char* active = nullptr;
        int r =
            sd_bus_get_property_string(bus, kSvc, kPath, kIface, "ActiveProfile", &err, &active);
        if (r >= 0 && active) {
            next.active = active;
            next.available = true;
        }
        free(active);
        sd_bus_error_free(&err);
    }
    if (!next.available) {  // daemon not present
        if (next != snap_) {
            snap_ = next;
            if (onChange_) onChange_();
        }
        return;
    }

    // Profiles (aa{sv}); pull the "Profile" name string out of each dict.
    {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        int r = sd_bus_get_property(bus, kSvc, kPath, kIface, "Profiles", &err, &reply, "aa{sv}");
        if (r >= 0 && reply) {
            if (sd_bus_message_enter_container(reply, 'a', "a{sv}") >= 0) {
                while (sd_bus_message_enter_container(reply, 'a', "{sv}") > 0) {
                    while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
                        const char* key = nullptr;
                        sd_bus_message_read_basic(reply, 's', &key);
                        if (key && std::strcmp(key, "Profile") == 0 &&
                            sd_bus_message_enter_container(reply, 'v', "s") >= 0) {
                            const char* name = nullptr;
                            if (sd_bus_message_read_basic(reply, 's', &name) >= 0 && name)
                                next.profiles.emplace_back(name);
                            sd_bus_message_exit_container(reply);
                        } else {
                            sd_bus_message_skip(reply, "v");
                        }
                        sd_bus_message_exit_container(reply);  // dict entry
                    }
                    sd_bus_message_exit_container(reply);  // a{sv}
                }
                sd_bus_message_exit_container(reply);  // outer array
            }
        }
        if (reply) sd_bus_message_unref(reply);
        sd_bus_error_free(&err);
    }

    if (next != snap_) {
        snap_ = std::move(next);
        if (onChange_) onChange_();
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
