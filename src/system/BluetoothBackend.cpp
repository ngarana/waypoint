// BluetoothBackend.cpp - BlueZ monitor implementation (async startup, push-driven).
#include "system/BluetoothBackend.hpp"

#include <systemd/sd-bus.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "system/SystemBus.hpp"

namespace qypr {

namespace {
constexpr const char* kBlueZ = "org.bluez";
constexpr const char* kAdapterIface = "org.bluez.Adapter1";
constexpr const char* kDeviceIface = "org.bluez.Device1";
constexpr const char* kBatteryIface = "org.bluez.Battery1";
constexpr const char* kPropsIface = "org.freedesktop.DBus.Properties";
constexpr const char* kObjectManagerIface = "org.freedesktop.DBus.ObjectManager";

// SystemBus caps every call on the shared connection at 5 s so one wedged peer
// cannot freeze the bar. That ceiling is right for property reads and wrong for
// these: pairing waits on a human reading a code off a phone, and a profile
// connect legitimately takes 30-60 s (bluetoothd's own logs show exactly that
// against a phone). Aborting early is worse than waiting — BlueZ carries on
// regardless, so the op "fails", the row still reads unpaired, and the next
// click starts a *second* pairing that prompts for the passkey all over again.
constexpr uint64_t kPairTimeoutUs = 120'000'000;
constexpr uint64_t kConnectTimeoutUs = 60'000'000;
constexpr uint64_t kRemoveTimeoutUs = 15'000'000;

// Variant readers. enter_container returns 1 when it entered, 0 when the
// contents do not match the requested type (nothing consumed) and <0 on error
// — so anything but 1 must fall through to skip("v"). Treating 0 as success
// reads past the variant and then exits a container that was never entered,
// which desyncs the *rest of the property dictionary*: every key after the
// mistyped one is silently lost. That exact mistake emptied the WiFi picker.
bool extractBool(sd_bus_message* m, bool* out) {
    if (sd_bus_message_enter_container(m, 'v', "b") <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    int v = 0;
    const int r = sd_bus_message_read_basic(m, 'b', &v);
    sd_bus_message_exit_container(m);
    if (r < 0) { return false; }
    *out = v != 0;
    return true;
}

bool extractString(sd_bus_message* m, std::string* out) {
    if (sd_bus_message_enter_container(m, 'v', "s") <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    const char* s = nullptr;
    const int r = sd_bus_message_read_basic(m, 's', static_cast<void*>(&s));
    sd_bus_message_exit_container(m);
    if (r < 0 || (s == nullptr)) { return false; }
    *out = s;
    return true;
}

bool extractByte(sd_bus_message* m, uint8_t* out) {
    if (sd_bus_message_enter_container(m, 'v', "y") <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    const int r = sd_bus_message_read_basic(m, 'y', out);
    sd_bus_message_exit_container(m);
    return r >= 0;
}

// Walk one interface's a{sv} property dictionary, dispatching each key to `fn`
// and skipping the variant of anything it does not claim. Entering and exiting
// the dictionary in one place is what keeps the three walkers below symmetric.
template <typename Fn>
void walkProps(sd_bus_message* m, const Fn& fn) {
    if (sd_bus_message_enter_container(m, 'a', "{sv}") <= 0) {
        sd_bus_message_skip(m, "a{sv}");
        return;
    }
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char* key = nullptr;
        sd_bus_message_read(m, "s", &key);
        if (key == nullptr || !fn(key)) { sd_bus_message_skip(m, "v"); }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
}

void parseAdapterProps(sd_bus_message* m, BluetoothSnapshot* snap) {
    walkProps(m, [&](const char* key) {
        if (std::strcmp(key, "Powered") == 0) { return extractBool(m, &snap->powered); }
        if (std::strcmp(key, "Discovering") == 0) { return extractBool(m, &snap->discovering); }
        return false;
    });
}

void parseDeviceProps(sd_bus_message* m, BtDevice* dev) {
    // Alias is the user-facing name and wins; Name is the fallback the adapter
    // reports. Collected separately so the outcome does not depend on the order
    // BlueZ happens to serialize the keys in.
    std::string alias;
    std::string name;
    walkProps(m, [&](const char* key) {
        if (std::strcmp(key, "Connected") == 0) { return extractBool(m, &dev->connected); }
        if (std::strcmp(key, "Paired") == 0) { return extractBool(m, &dev->paired); }
        if (std::strcmp(key, "Alias") == 0) { return extractString(m, &alias); }
        if (std::strcmp(key, "Name") == 0) { return extractString(m, &name); }
        if (std::strcmp(key, "Icon") == 0) { return extractString(m, &dev->icon); }
        return false;
    });
    if (!alias.empty()) {
        dev->name = alias;
    } else if (!name.empty() && dev->name.empty()) {
        dev->name = name;
    }
}

void parseBatteryProps(sd_bus_message* m, BtDevice* dev) {
    walkProps(m, [&](const char* key) {
        if (std::strcmp(key, "Percentage") == 0) {
            uint8_t pct = 0;
            if (!extractByte(m, &pct)) { return false; }
            dev->battery = pct;
            return true;
        }
        return false;
    });
}

bool dictHasKey(sd_bus_message* m, std::initializer_list<const char*> keys) {
    bool found = false;
    walkProps(m, [&](const char* key) {
        for (const char* k : keys) {
            if (std::strcmp(key, k) == 0) { found = true; }
        }
        return false;  // never claims a value: the variant is always skipped
    });
    return found;
}
}  // namespace

BluetoothBackend::BluetoothBackend(SystemBus& bus) : bus_(bus), agent_(bus) {}

BluetoothBackend::~BluetoothBackend() {
    sd_bus_slot_unref(propsSlot_);
    sd_bus_slot_unref(ifacesSlot_);
    sd_bus_slot_unref(ownerSlot_);
}

bool BluetoothBackend::start() {
    if (!bus_.available()) { return false; }

    // Subscribe before the first fetch so no state change can fall in the gap
    // (standard subscribe-then-fetch order).
    subscribeSignals();

    // The agent needs device *names* for its prompts, and the snapshot is the
    // only place that has them.
    agent_.setNameResolver([this](const std::string& path) -> std::string {
        for (const BtDevice& d : snap_.devices) {
            if (d.path == path) { return d.name; }
        }
        return {};
    });
    // A prompt appearing or clearing is a snapshot change like any other.
    agent_.setOnRequest([this] {
        BluetoothSnapshot next = snap_;
        next.pairing = agent_.request();
        publish(std::move(next));
    });
    if (agentEnabled_) { agent_.start(); }  // never on the lock screen (QL-1)

    refetch();
    return true;
}

void BluetoothBackend::respondPairing(bool accept) {
    agent_.respond(accept);
}

void BluetoothBackend::respondPairingInput(const std::string& text) {
    agent_.respondInput(text);
}

// One GetManagedObjects in flight at a time: a signal during a fetch only marks
// pendingFetch_, and the next fetch runs when the current reply lands. This is
// what keeps two replies from arriving out of order and regressing the
// snapshot to stale state.
void BluetoothBackend::refetch() {
    if (!bus_.available()) { return; }
    if (fetchInFlight_) {
        pendingFetch_ = true;
        return;
    }
    fetchInFlight_ = true;
    const int r = sd_bus_call_method_async(bus_.get(), nullptr, kBlueZ, "/", kObjectManagerIface,
                                           "GetManagedObjects",
                                           &BluetoothBackend::onGetManagedObjects, this, "");
    if (r < 0) {
        std::fprintf(stderr, "qypr: failed to enqueue GetManagedObjects: %d\n", -r);
        fetchInFlight_ = false;
        publishUnavailable();  // resolve the placeholder rather than hang on it
    }
}

void BluetoothBackend::endFetch() {
    fetchInFlight_ = false;
    if (pendingFetch_) {
        pendingFetch_ = false;
        refetch();
    }
}

int BluetoothBackend::onGetManagedObjects(sd_bus_message* reply, void* userdata,
                                          sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        const sd_bus_error* e = sd_bus_message_get_error(reply);
        std::fprintf(stderr, "qypr: BlueZ unavailable (%s: %s); bluetooth indicator disabled\n",
                     e != nullptr && e->name != nullptr ? e->name : "unknown",
                     e != nullptr && e->message != nullptr ? e->message : "");
        self->publishUnavailable();
        self->endFetch();
        return 0;
    }

    BluetoothSnapshot next;
    if (!self->parseManagedObjects(reply, &next) || !next.available) {
        if (!self->ready_ || self->snap_.available) {
            std::fprintf(stderr, "qypr: no Bluetooth adapter via BlueZ; indicator disabled\n");
        }
        self->publishUnavailable();
        self->endFetch();
        return 0;
    }

    // BlueZ's object tree says nothing about an operation the bar itself has in
    // flight, so a refetch must not wipe the picker's busy row or error line.
    next.busy = self->snap_.busy;
    next.error = self->snap_.error;
    next.pairing = self->snap_.pairing;
    self->publish(std::move(next));

    // A toggle arrived while the adapter path was unknown; apply it now that
    // the fetch produced one.
    if (self->pendingPowerSet_) {
        self->pendingPowerSet_ = false;
        self->sendSetPowered(self->pendingPowerOn_);
    }
    // A scan requested while the radio was off starts here, once BlueZ reports
    // the adapter powered.
    self->tryStartDiscovery();
    self->endFetch();
    return 0;
}

// Walk the GetManagedObjects reply (a{oa{sa{sv}}}) into `out`. Returns false on
// a malformed reply, in which case `out` is meaningless — the caller must treat
// that as "no adapter" rather than keep the previous snapshot, or adapter_ ends
// up cleared while snapshot().available still reads true and every setPowered()
// takes the defer-and-refetch path forever.
bool BluetoothBackend::parseManagedObjects(sd_bus_message* m, BluetoothSnapshot* out) {
    adapter_.clear();
    if (sd_bus_message_enter_container(m, 'a', "{oa{sa{sv}}}") <= 0) { return false; }

    while (sd_bus_message_enter_container(m, 'e', "oa{sa{sv}}") > 0) {
        const char* path = nullptr;
        sd_bus_message_read(m, "o", &path);

        BtDevice dev;
        if (path != nullptr) { dev.path = path; }
        bool isDeviceObj = false;

        if (sd_bus_message_enter_container(m, 'a', "{sa{sv}}") > 0) {
            while (sd_bus_message_enter_container(m, 'e', "sa{sv}") > 0) {
                const char* iface = nullptr;
                sd_bus_message_read(m, "s", &iface);
                // One object carries several interfaces; Device1 and Battery1
                // both contribute to the same BtDevice.
                const bool isAdapter = (iface != nullptr) && std::strcmp(iface, kAdapterIface) == 0;
                const bool isDevice = (iface != nullptr) && std::strcmp(iface, kDeviceIface) == 0;
                const bool isBattery = (iface != nullptr) && std::strcmp(iface, kBatteryIface) == 0;
                if (isAdapter) {
                    parseAdapterProps(m, out);
                    out->available = true;
                    if (adapter_.empty() && (path != nullptr)) { adapter_ = path; }
                } else if (isDevice) {
                    isDeviceObj = true;
                    parseDeviceProps(m, &dev);
                } else if (isBattery) {
                    parseBatteryProps(m, &dev);
                } else {
                    sd_bus_message_skip(m, "a{sv}");
                }
                sd_bus_message_exit_container(m);
            }
            sd_bus_message_exit_container(m);
        }

        if (isDeviceObj) {
            if (dev.connected) {
                ++out->connectedCount;
                if (out->firstDevice.empty()) { out->firstDevice = dev.name; }
            }
            out->devices.push_back(std::move(dev));
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return true;
}

// The single seam every result path publishes through, mirroring
// WifiBackend::publish(): an unchanged snapshot must not wake the compositor.
// BlueZ re-emits InterfacesAdded/Removed for every device that drifts in and
// out of range, and each one drove a full repaint before this guard.
void BluetoothBackend::publish(BluetoothSnapshot&& next) {
    if (ready_ && next == snap_) { return; }  // no change: skip repaint
    snap_ = std::move(next);
    notifyReady();
}

// Definitive "no adapter": BlueZ is absent, answered with an error, or sent a
// reply we could not walk. Publishes an empty snapshot so the indicator
// resolves its placeholder instead of hanging on stale data.
void BluetoothBackend::publishUnavailable() {
    adapter_.clear();
    // A toggle deferred for an adapter that never appeared is dropped rather
    // than applied to some future adapter the user did not ask about.
    pendingPowerSet_ = false;
    publish({});
}

void BluetoothBackend::subscribeSignals() {
    if (subscribed_) { return; }
    subscribed_ = true;
    propsSlot_ = bus_.addMatch(
        "type='signal',sender='org.bluez',interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged'",
        &BluetoothBackend::onPropsChanged, this);
    ifacesSlot_ = bus_.addMatch("type='signal',sender='org.bluez',"
                                "interface='org.freedesktop.DBus.ObjectManager'",
                                &BluetoothBackend::onInterfacesChanged, this);
    // BlueZ may not be up when the bar starts; re-fetch when it (re)appears.
    ownerSlot_ = bus_.addMatch(
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',arg0='org.bluez'",
        &BluetoothBackend::onNameOwnerChanged, this);
}

int BluetoothBackend::onPropsChanged(sd_bus_message* m, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothBackend*>(userdata);
    const char* iface = nullptr;
    if (sd_bus_message_read(m, "s", &iface) < 0 || (iface == nullptr)) { return 0; }

    if (std::strcmp(iface, kAdapterIface) == 0) {
        if (!dictHasKey(m, {"Powered", "PowerState", "Discovering"})) { return 0; }
    } else if (std::strcmp(iface, kDeviceIface) == 0) {
        if (!dictHasKey(m, {"Connected", "Paired"})) { return 0; }
    } else if (std::strcmp(iface, kBatteryIface) == 0) {
        if (!dictHasKey(m, {"Percentage"})) { return 0; }
    } else {
        return 0;
    }

    // Re-fetch: async GetManagedObjects (serialized against any in-flight one).
    self->refetch();
    return 0;
}

int BluetoothBackend::onInterfacesChanged(sd_bus_message* /*unused*/, void* userdata,
                                          sd_bus_error* /*unused*/) {
    // Re-fetch: async GetManagedObjects (serialized against any in-flight one).
    static_cast<BluetoothBackend*>(userdata)->refetch();
    return 0;
}

int BluetoothBackend::onNameOwnerChanged(sd_bus_message* m, void* userdata,
                                         sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothBackend*>(userdata);
    const char* name = nullptr;
    const char* oldOwner = nullptr;
    const char* newOwner = nullptr;
    if (sd_bus_message_read(m, "sss", &name, &oldOwner, &newOwner) < 0 || (name == nullptr)) {
        return 0;
    }
    if (newOwner == nullptr || *newOwner == '\0') {
        self->agent_.onBluezLost();  // the registration died with the daemon
        return 0;                    // gone: keep last state
    }
    if (self->agentEnabled_) {
        self->agent_.start();  // (re)appeared: re-register the pairing agent
    }
    self->refetch();
    return 0;
}

void BluetoothBackend::setPowered(bool on) {
    if (!bus_.available()) { return; }
    if (adapter_.empty()) {
        // The adapter path is not known yet (startup fetch still in flight or a
        // previous fetch failed). Defer the toggle and re-fetch instead of
        // silently dropping the user's click.
        std::fprintf(stderr,
                     "qypr: Bluetooth adapter unknown; deferring power toggle, refetching\n");
        pendingPowerSet_ = true;
        pendingPowerOn_ = on;
        refetch();
        return;
    }
    sendSetPowered(on);
}

void BluetoothBackend::sendSetPowered(bool on) {
    // Optimistic write so the switch answers the click immediately; BlueZ's
    // PropertiesChanged drives the authoritative refetch, and onSetPowerReply
    // re-fetches if the Set was rejected.
    BluetoothSnapshot next = snap_;
    next.powered = on;
    if (!on) {
        next.connectedCount = 0;
        next.firstDevice.clear();
        next.discovering = false;  // the adapter drops discovery with the radio
        for (BtDevice& d : next.devices) { d.connected = false; }
    }
    publish(std::move(next));

    sd_bus_message* msg = nullptr;
    if (sd_bus_message_new_method_call(bus_.get(), &msg, kBlueZ, adapter_.c_str(), kPropsIface,
                                       "Set") < 0) {
        return;
    }
    sd_bus_message_append(msg, "ss", kAdapterIface, "Powered");
    sd_bus_message_open_container(msg, 'v', "b");
    sd_bus_message_append(msg, "b", on ? 1 : 0);
    sd_bus_message_close_container(msg);
    const int r =
        sd_bus_call_async(bus_.get(), nullptr, msg, &BluetoothBackend::onSetPowerReply, this, 0);
    if (r < 0) {
        std::fprintf(stderr, "qypr: failed to send BlueZ setPowered(%s): %d\n", on ? "on" : "off",
                     -r);
    }
    sd_bus_message_unref(msg);
}

int BluetoothBackend::onSetPowerReply(sd_bus_message* reply, void* userdata,
                                      sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        const sd_bus_error* e = sd_bus_message_get_error(reply);
        std::fprintf(stderr, "qypr: BlueZ setPowered failed (%s: %s)\n",
                     e != nullptr && e->name != nullptr ? e->name : "unknown",
                     e != nullptr && e->message != nullptr ? e->message : "");
        // The optimistic snapshot no longer matches reality; re-fetch to
        // converge on the true state.
        self->refetch();
        return 0;
    }
    // Success: the PropertiesChanged signal will drive the next fetch.
    return 0;
}

// ── Device operations ─────────────────────────────────────────────────────
// Every one of these is a single async call whose reply clears the busy row and
// records any failure. One operation is in flight at a time (opPath_), which is
// all the picker can express: exactly one row shows progress.

void BluetoothBackend::beginOp(const std::string& path) {
    opPath_ = path;
    BluetoothSnapshot next = snap_;
    next.busy = path;
    next.error.clear();
    publish(std::move(next));
}

// Terminal step of every device operation: clear the busy row, keep the BlueZ
// error text if it failed, and re-fetch so the snapshot converges on truth.
void BluetoothBackend::endOp(const char* what, sd_bus_message* reply) {
    std::string err;
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        const sd_bus_error* e = sd_bus_message_get_error(reply);
        const char* msg = (e != nullptr && e->message != nullptr) ? e->message : "failed";
        std::fprintf(stderr, "qypr: BlueZ %s failed: %s\n", what, msg);
        err = std::string(what) + " failed: " + msg;
    }
    opPath_.clear();
    BluetoothSnapshot next = snap_;
    next.busy.clear();
    next.error = err;
    publish(std::move(next));
    refetch();
}

void BluetoothBackend::callDevice(const std::string& path, const char* iface, const char* member,
                                  ReplyHandler cb, uint64_t timeoutUs) {
    if (!bus_.available() || path.empty()) { return; }
    beginOp(path);
    // Built by hand rather than sd_bus_call_method_async so this call can carry
    // its own timeout instead of the connection default (see the constants).
    sd_bus_message* msg = nullptr;
    int r = sd_bus_message_new_method_call(bus_.get(), &msg, kBlueZ, path.c_str(), iface, member);
    if (r >= 0) { r = sd_bus_call_async(bus_.get(), nullptr, msg, cb, this, timeoutUs); }
    sd_bus_message_unref(msg);
    if (r < 0) {
        std::fprintf(stderr, "qypr: failed to enqueue BlueZ %s: %d\n", member, -r);
        opPath_.clear();
        BluetoothSnapshot next = snap_;
        next.busy.clear();
        next.error = std::string(member) + " could not be sent";
        publish(std::move(next));
    }
}

int BluetoothBackend::onDeviceOpReply(sd_bus_message* reply, void* userdata,
                                      sd_bus_error* /*unused*/) {
    static_cast<BluetoothBackend*>(userdata)->endOp("device operation", reply);
    return 0;
}

void BluetoothBackend::connectDevice(const std::string& path) {
    callDevice(path, kDeviceIface, "Connect", &BluetoothBackend::onDeviceOpReply,
               kConnectTimeoutUs);
}

void BluetoothBackend::disconnectDevice(const std::string& path) {
    callDevice(path, kDeviceIface, "Disconnect", &BluetoothBackend::onDeviceOpReply,
               kConnectTimeoutUs);
}

void BluetoothBackend::pairDevice(const std::string& path) {
    callDevice(path, kDeviceIface, "Pair", &BluetoothBackend::onPairReply, kPairTimeoutUs);
}

// Pair succeeded: trust the device so it may reconnect unattended, then connect
// it — the same sequence bluetoothctl and the GNOME/Windows panels perform.
int BluetoothBackend::onPairReply(sd_bus_message* reply, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        self->endOp("pairing", reply);
        return 0;
    }
    const std::string path = self->opPath_;
    self->setTrusted(path);
    // Keep the row busy across the follow-on Connect rather than blinking idle.
    self->connectDevice(path);
    return 0;
}

void BluetoothBackend::setTrusted(const std::string& path) {
    if (!bus_.available() || path.empty()) { return; }
    sd_bus_message* msg = nullptr;
    if (sd_bus_message_new_method_call(bus_.get(), &msg, kBlueZ, path.c_str(), kPropsIface, "Set") <
        0) {
        return;
    }
    sd_bus_message_append(msg, "ss", kDeviceIface, "Trusted");
    sd_bus_message_open_container(msg, 'v', "b");
    sd_bus_message_append(msg, "b", 1);
    sd_bus_message_close_container(msg);
    sd_bus_call_async(bus_.get(), nullptr, msg, nullptr, nullptr, 0);
    sd_bus_message_unref(msg);
}

// RemoveDevice lives on the *adapter*, taking the device path as an argument —
// unlike every other device operation here.
void BluetoothBackend::forgetDevice(const std::string& path) {
    if (!bus_.available() || path.empty()) { return; }
    if (adapter_.empty()) {
        // Only reachable in the window between a seeded snapshot and the first
        // fetch. Say so rather than swallowing the click in silence.
        std::fprintf(stderr, "qypr: Bluetooth adapter unknown; cannot forget device\n");
        return;
    }
    beginOp(path);
    sd_bus_message* msg = nullptr;
    int r = sd_bus_message_new_method_call(bus_.get(), &msg, kBlueZ, adapter_.c_str(),
                                           kAdapterIface, "RemoveDevice");
    if (r >= 0) { r = sd_bus_message_append(msg, "o", path.c_str()); }
    if (r >= 0) {
        r = sd_bus_call_async(bus_.get(), nullptr, msg, &BluetoothBackend::onDeviceOpReply, this,
                              kRemoveTimeoutUs);
    }
    sd_bus_message_unref(msg);
    if (r < 0) {
        std::fprintf(stderr, "qypr: failed to enqueue BlueZ RemoveDevice: %d\n", -r);
        opPath_.clear();
        BluetoothSnapshot next = snap_;
        next.busy.clear();
        publish(std::move(next));
    }
}

// ── Discovery ─────────────────────────────────────────────────────────────

void BluetoothBackend::callAdapter(const char* member) {
    if (!bus_.available() || adapter_.empty()) { return; }
    sd_bus_call_method_async(bus_.get(), nullptr, kBlueZ, adapter_.c_str(), kAdapterIface, member,
                             &BluetoothBackend::onAdapterOpReply, this, "");
}

int BluetoothBackend::onAdapterOpReply(sd_bus_message* reply, void* userdata,
                                       sd_bus_error* /*unused*/) {
    auto* self = static_cast<BluetoothBackend*>(userdata);
    self->discoveryCallInFlight_ = false;
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        const sd_bus_error* e = sd_bus_message_get_error(reply);
        const char* msg = (e != nullptr && e->message != nullptr) ? e->message : "unknown";
        std::fprintf(stderr, "qypr: BlueZ discovery call failed: %s\n", msg);
        // Stop wanting it: retrying on every fetch would spin forever against
        // an adapter that keeps refusing.
        self->wantDiscovery_ = false;
        BluetoothSnapshot next = self->snap_;
        next.error = std::string("Scan failed: ") + msg;
        self->publish(std::move(next));
    }
    // Either way the adapter's Discovering property is the truth; re-read it.
    self->refetch();
    return 0;
}

void BluetoothBackend::tryStartDiscovery() {
    if (!wantDiscovery_ || discoveryCallInFlight_) { return; }
    if (!snap_.powered || snap_.discovering || adapter_.empty()) { return; }
    discoveryCallInFlight_ = true;
    callAdapter("StartDiscovery");
}

void BluetoothBackend::startDiscovery() {
    wantDiscovery_ = true;
    tryStartDiscovery();
}

void BluetoothBackend::stopDiscovery() {
    wantDiscovery_ = false;
    if (snap_.discovering) { callAdapter("StopDiscovery"); }
}

}  // namespace qypr
