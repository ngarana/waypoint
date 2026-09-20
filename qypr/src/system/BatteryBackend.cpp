// BatteryBackend.cpp - UPower monitor implementation (async startup, push via PropertiesChanged).
#include "system/BatteryBackend.hpp"

#include <systemd/sd-bus.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "system/SystemBus.hpp"

namespace qypr {

namespace {
constexpr const char* kUPower = "org.freedesktop.UPower";
constexpr const char* kUPowerPath = "/org/freedesktop/UPower";
constexpr const char* kDisplayDevice = "/org/freedesktop/UPower/devices/DisplayDevice";
constexpr const char* kDeviceIface = "org.freedesktop.UPower.Device";
constexpr const char* kPropsIface = "org.freedesktop.DBus.Properties";

BatterySnapshot::State mapState(uint32_t s) {
    switch (s) {
        case 1:
            return BatterySnapshot::Charging;
        case 2:
        case 3:
        case 6:
            return BatterySnapshot::Discharging;
        case 4:
            return BatterySnapshot::Full;
        case 5:
            return BatterySnapshot::PendingCharge;
        default:
            return BatterySnapshot::Unknown;
    }
}

bool readVariant(sd_bus_message* m, const char* contents, void* out) {
    if (sd_bus_message_enter_container(m, 'v', contents) < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    int const r = sd_bus_message_read_basic(m, contents[0], out);
    sd_bus_message_exit_container(m);
    return r >= 0;
}
}  // namespace

BatteryBackend::BatteryBackend(SystemBus& bus) : bus_(bus) {}

BatteryBackend::~BatteryBackend() {
    if (signalSlot_ != nullptr) { sd_bus_slot_unref(signalSlot_); }
}

bool BatteryBackend::start() {
    if (!bus_.available()) { return false; }

    // Subscribe before the first fetch so no state change can fall in the gap
    // (standard subscribe-then-fetch order).
    subscribeSignal();
    fetchInitial();
    return true;
}

void BatteryBackend::fetchInitial() {
    if (!bus_.available()) { return; }
    // Async: GetAll on DisplayDevice → callback decides next step.
    sd_bus_call_method_async(bus_.get(), nullptr, kUPower, kDisplayDevice, kPropsIface, "GetAll",
                             &BatteryBackend::onGetAllDisplay, this, "s", kDeviceIface);
}

int BatteryBackend::onGetAllDisplay(sd_bus_message* reply, void* userdata,
                                    sd_bus_error* /*unused*/) {
    auto* self = static_cast<BatteryBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        // DisplayDevice unavailable — try EnumerateDevices fallback.
        sd_bus_call_method_async(self->bus_.get(), nullptr, kUPower, kUPowerPath, kUPower,
                                 "EnumerateDevices", &BatteryBackend::onEnumerateDevices, self, "");
        return 0;
    }

    if (self->parseProps(reply) && self->snap_.present) {
        self->devicePath_ = kDisplayDevice;
        self->subscribeSignal();
        self->notifyReady();
        return 0;
    }

    // DisplayDevice says not present — try EnumerateDevices fallback.
    sd_bus_call_method_async(self->bus_.get(), nullptr, kUPower, kUPowerPath, kUPower,
                             "EnumerateDevices", &BatteryBackend::onEnumerateDevices, self, "");
    return 0;
}

int BatteryBackend::onEnumerateDevices(sd_bus_message* reply, void* userdata,
                                       sd_bus_error* /*unused*/) {
    auto* self = static_cast<BatteryBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        // Publish a definitive "absent" instead of leaving the placeholder
        // pending forever.
        std::fprintf(stderr, "qypr: no battery via UPower; battery indicator disabled\n");
        self->snap_.present = false;
        self->notifyReady();  // hide the placeholder
        return 0;
    }

    // Walk object paths, find first that looks like a battery.
    if (sd_bus_message_enter_container(reply, 'a', "o") < 0) {
        std::fprintf(stderr, "qypr: no battery via UPower; battery indicator disabled\n");
        self->snap_.present = false;
        self->notifyReady();  // hide the placeholder
        return 0;
    }
    const char* path = nullptr;
    std::string found;
    while (sd_bus_message_read(reply, "o", &path) > 0 && (path != nullptr)) {
        if (std::strstr(path, "/devices/bat") != nullptr) {
            found = path;
            break;
        }
    }
    sd_bus_message_exit_container(reply);

    if (found.empty()) {
        std::fprintf(stderr, "qypr: no battery via UPower; battery indicator disabled\n");
        self->snap_.present = false;
        self->notifyReady();  // hide the placeholder
        return 0;
    }

    self->devicePath_ = found;
    sd_bus_call_method_async(self->bus_.get(), nullptr, kUPower, found.c_str(), kPropsIface,
                             "GetAll", &BatteryBackend::onGetAllDevice, self, "s", kDeviceIface);
    return 0;
}

int BatteryBackend::onGetAllDevice(sd_bus_message* reply, void* userdata,
                                   sd_bus_error* /*unused*/) {
    auto* self = static_cast<BatteryBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        std::fprintf(stderr, "qypr: no battery via UPower; battery indicator disabled\n");
        self->snap_.present = false;
        self->notifyReady();  // hide the placeholder
        return 0;
    }

    self->parseProps(reply);
    if (!self->snap_.present) {
        std::fprintf(stderr, "qypr: no battery via UPower; battery indicator disabled\n");
        self->notifyReady();  // hide the placeholder
        return 0;
    }

    self->subscribeSignal();
    self->notifyReady();
    return 0;
}

void BatteryBackend::subscribeSignal() {
    if (subscribed_) { return; }
    subscribed_ = true;
    // Scoped by sender, not by path: the handler filters by the tracked device
    // path, covering both the DisplayDevice and the EnumerateDevices fallback
    // (the fallback path is only known after the initial fetch).
    std::string const rule = std::string("type='signal',sender='") + kUPower + "',interface='" +
                             kPropsIface + "',member='PropertiesChanged'";
    signalSlot_ = bus_.addMatch(rule.c_str(), &BatteryBackend::onPropertiesChanged, this);
    // UPower may not be up when the bar starts; retry the fetch when it
    // (re)appears.
    ownerSlot_ = bus_.addMatch(
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',arg0='org.freedesktop.UPower'",
        &BatteryBackend::onNameOwnerChanged, this);
}

bool BatteryBackend::parseProps(sd_bus_message* m) {
    if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) { return false; }

    bool any = false;
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read(m, "s", &key) < 0 || (key == nullptr)) {
            sd_bus_message_skip(m, "v");
            sd_bus_message_exit_container(m);
            continue;
        }

        if (std::strcmp(key, "Percentage") == 0) {
            double d = 0;
            if (readVariant(m, "d", &d)) {
                snap_.percentage = static_cast<int>(std::lround(d));
                any = true;
            }
        } else if (std::strcmp(key, "State") == 0) {
            uint32_t s = 0;
            if (readVariant(m, "u", &s)) {
                snap_.state = mapState(s);
                any = true;
            }
        } else if (std::strcmp(key, "IsPresent") == 0) {
            int b = 0;
            if (readVariant(m, "b", &b)) {
                snap_.present = b != 0;
                any = true;
            }
        } else if (std::strcmp(key, "TimeToEmpty") == 0) {
            int64_t t = 0;
            if (readVariant(m, "x", &t)) {
                snap_.timeToEmpty = t;
                any = true;
            }
        } else if (std::strcmp(key, "TimeToFull") == 0) {
            int64_t t = 0;
            if (readVariant(m, "x", &t)) {
                snap_.timeToFull = t;
                any = true;
            }
        } else if (std::strcmp(key, "EnergyRate") == 0) {
            double d = 0;
            if (readVariant(m, "d", &d)) {
                snap_.energyRate = d;
                any = true;
            }
        } else if (std::strcmp(key, "NativePath") == 0) {
            const char* s = nullptr;
            if (readVariant(m, "s", static_cast<void*>(&s)) && (s != nullptr)) {
                snap_.nativePath = s;
                any = true;
            }
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return any;
}

int BatteryBackend::onPropertiesChanged(sd_bus_message* m, void* userdata,
                                        sd_bus_error* /*unused*/) {
    auto* self = static_cast<BatteryBackend*>(userdata);
    const char* path = sd_bus_message_get_path(m);
    if (path == nullptr || self->devicePath_ != path) { return 0; }
    const char* iface = nullptr;
    if (sd_bus_message_read(m, "s", &iface) < 0 || (iface == nullptr)) { return 0; }
    if (std::strcmp(iface, kDeviceIface) != 0) { return 0; }
    if (self->parseProps(m)) { self->notifyReady(); }
    return 0;
}

int BatteryBackend::onNameOwnerChanged(sd_bus_message* m, void* userdata,
                                       sd_bus_error* /*unused*/) {
    auto* self = static_cast<BatteryBackend*>(userdata);
    const char* name = nullptr;
    const char* oldOwner = nullptr;
    const char* newOwner = nullptr;
    if (sd_bus_message_read(m, "sss", &name, &oldOwner, &newOwner) < 0 || (name == nullptr)) {
        return 0;
    }
    if (newOwner == nullptr || *newOwner == '\0') { return 0; }  // gone: keep last state
    self->fetchInitial();  // (re)appeared: retry the initial fetch
    return 0;
}

}  // namespace qypr
