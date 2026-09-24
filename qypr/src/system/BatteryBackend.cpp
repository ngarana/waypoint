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

BatteryBackend::BatteryBackend(EventLoop& loop, SystemBus& bus)
    : bus_(bus),
      retry_(loop, [this] { fetchInitial(); }) {}

BatteryBackend::~BatteryBackend() {
    sd_bus_slot_unref(signalSlot_);
    sd_bus_slot_unref(ownerSlot_);
    sd_bus_slot_unref(addedSlot_);
    sd_bus_slot_unref(removedSlot_);
}

bool BatteryBackend::start() {
    if (!bus_.available()) {
        snap_ = {};
        notifyReady();
        return false;
    }
    if (started_) {
        fetchInitial();
        return true;
    }
    started_ = true;

    // Subscribe before the first fetch so no state change can fall in the gap
    // (standard subscribe-then-fetch order).
    subscribeSignal();
    fetchInitial();
    return true;
}

void BatteryBackend::fetchInitial() {  // NOLINT(misc-no-recursion) D-Bus callbacks are deferred
    if (!bus_.available()) { return; }
    retry_.cancel();
    if (fetchInFlight_) {
        pendingFetch_ = true;
        return;
    }
    fetchInFlight_ = true;
    // Async: GetAll on DisplayDevice → callback decides next step.
    if (sd_bus_call_method_async(bus_.get(), nullptr, kUPower, kDisplayDevice, kPropsIface,
                                 "GetAll", &BatteryBackend::onGetAllDisplay, this, "s",
                                 kDeviceIface) < 0) {
        failFetch("failed to enqueue UPower DisplayDevice query");
    }
}

// NOLINTNEXTLINE(misc-no-recursion) D-Bus callbacks resume on the event loop.
void BatteryBackend::endFetch(bool retry) {
    fetchInFlight_ = false;
    if (pendingFetch_) {
        pendingFetch_ = false;
        fetchInitial();
        return;
    }
    if (retry) {
        retry_.schedule();
    } else {
        retry_.reset();
    }
}

void BatteryBackend::failFetch(const char* message) {  // NOLINT(misc-no-recursion) async path
    std::fprintf(stderr, "qypr: UPower query failed (%s); retrying\n", message);
    devicePath_.clear();
    snap_ = {};
    notifyReady();
    endFetch(/*retry=*/true);
}

int BatteryBackend::onGetAllDisplay(sd_bus_message* reply, void* userdata,
                                    sd_bus_error* /*unused*/) {
    auto* self = static_cast<BatteryBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        // DisplayDevice unavailable — try EnumerateDevices fallback.
        if (sd_bus_call_method_async(self->bus_.get(), nullptr, kUPower, kUPowerPath, kUPower,
                                     "EnumerateDevices", &BatteryBackend::onEnumerateDevices, self,
                                     "") < 0) {
            self->failFetch("failed to enqueue EnumerateDevices query");
        }
        return 0;
    }

    if (self->parseProps(reply) && self->snap_.present) {
        self->devicePath_ = kDisplayDevice;
        self->subscribeSignal();
        self->notifyReady();
        self->endFetch();
        return 0;
    }

    // DisplayDevice says not present — try EnumerateDevices fallback.
    if (sd_bus_call_method_async(self->bus_.get(), nullptr, kUPower, kUPowerPath, kUPower,
                                 "EnumerateDevices", &BatteryBackend::onEnumerateDevices, self,
                                 "") < 0) {
        self->failFetch("failed to enqueue EnumerateDevices query");
    }
    return 0;
}

int BatteryBackend::onEnumerateDevices(sd_bus_message* reply, void* userdata,
                                       sd_bus_error* /*unused*/) {
    auto* self = static_cast<BatteryBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        self->failFetch("EnumerateDevices returned an error");
        return 0;
    }

    // A valid empty enumeration is definitive; DeviceAdded/Removed signals
    // below handle later hotplug without polling.
    if (sd_bus_message_enter_container(reply, 'a', "o") < 0) {
        self->failFetch("malformed EnumerateDevices reply");
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
        self->devicePath_.clear();
        self->snap_ = {};
        self->notifyReady();  // hide the placeholder
        self->endFetch();
        return 0;
    }

    self->devicePath_ = found;
    if (sd_bus_call_method_async(self->bus_.get(), nullptr, kUPower, found.c_str(), kPropsIface,
                                 "GetAll", &BatteryBackend::onGetAllDevice, self, "s",
                                 kDeviceIface) < 0) {
        self->failFetch("failed to enqueue battery-device query");
    }
    return 0;
}

int BatteryBackend::onGetAllDevice(sd_bus_message* reply, void* userdata,
                                   sd_bus_error* /*unused*/) {
    auto* self = static_cast<BatteryBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        self->failFetch("battery-device query returned an error");
        return 0;
    }

    if (!self->parseProps(reply)) {
        self->failFetch("malformed battery-device reply");
        return 0;
    }
    if (!self->snap_.present) {
        self->devicePath_.clear();
        self->snap_ = {};
        self->notifyReady();  // hide the placeholder
        self->endFetch();
        return 0;
    }

    self->subscribeSignal();
    self->notifyReady();
    self->endFetch();
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
    addedSlot_ = bus_.addMatch(
        "type='signal',sender='org.freedesktop.UPower',path='/org/freedesktop/UPower',"
        "interface='org.freedesktop.UPower',member='DeviceAdded'",
        &BatteryBackend::onDeviceAdded, this);
    removedSlot_ = bus_.addMatch(
        "type='signal',sender='org.freedesktop.UPower',path='/org/freedesktop/UPower',"
        "interface='org.freedesktop.UPower',member='DeviceRemoved'",
        &BatteryBackend::onDeviceRemoved, this);
}

int BatteryBackend::onDeviceAdded(sd_bus_message* /*message*/, void* userdata,
                                  sd_bus_error* /*unused*/) {
    static_cast<BatteryBackend*>(userdata)->fetchInitial();
    return 0;
}

int BatteryBackend::onDeviceRemoved(sd_bus_message* /*message*/, void* userdata,
                                    sd_bus_error* /*unused*/) {
    static_cast<BatteryBackend*>(userdata)->fetchInitial();
    return 0;
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
    if (newOwner == nullptr || *newOwner == '\0') {
        self->devicePath_.clear();
        self->snap_ = {};
        self->notifyReady();
        self->retry_.schedule();
        return 0;
    }
    self->fetchInitial();  // (re)appeared: refresh immediately
    return 0;
}

}  // namespace qypr
