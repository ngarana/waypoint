// BluetoothBackend.cpp - BlueZ monitor facade (async startup, push-driven).
//
// Lifecycle, subscriptions, fetch serialization, agent wiring, and snapshot
// publication live here. D-Bus mechanics live in BluezClient, pure snapshot
// shaping in BluetoothSnapshotReducer, and user commands in
// BluetoothOperations (behind BluezCommandPort, adapted to the facade's
// adapter path by FacadePort).
#include "system/BluetoothBackend.hpp"

#include <systemd/sd-bus.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "system/BluetoothSnapshotReducer.hpp"
#include "system/BluezClient.hpp"
#include "system/SystemBus.hpp"

namespace qypr {

BluetoothBackend::BluetoothBackend(SystemBus& bus)
    : bus_(bus),
      client_(bus),
      port_(client_, adapter_),
      ops_(
          port_, [this](BluetoothSnapshot&& s) { publish(std::move(s)); },
          [this]() -> const BluetoothSnapshot& { return snap_; }, [this]() { refetch(); }),
      agent_(bus) {}

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
    agent_.setNameResolver([this](const std::string& path) { return deviceNameFor(snap_, path); });
    // A prompt appearing or clearing is a snapshot change like any other.
    agent_.setOnRequest([this] { publish(withPairingRequest(snap_, agent_.request())); });
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
    if (!client_.getManagedObjects(&BluetoothBackend::onGetManagedObjects, this)) {
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

    BluetoothManagedObjects readings;
    bool parsed = BluezClient::parseManagedObjects(reply, &readings);
    ManagedObjectsResult result = parsed ? reduceManagedObjects(readings) : ManagedObjectsResult{};
    if (!result.ok || !result.snapshot.available) {
        if (!self->ready_ || self->snap_.available) {
            std::fprintf(stderr, "qypr: no Bluetooth adapter via BlueZ; indicator disabled\n");
        }
        self->publishUnavailable();
        self->endFetch();
        return 0;
    }
    self->adapter_ = result.adapter;

    // BlueZ's object tree says nothing about an operation the bar itself has in
    // flight, so a refetch must not wipe the picker's busy row or error line.
    self->publish(preserveTransient(std::move(result.snapshot), self->snap_));

    // A toggle arrived while the adapter path was unknown; apply it now that
    // the fetch produced one.
    self->ops_.applyPendingPower();
    // A scan requested while the radio was off starts here, once BlueZ reports
    // the adapter powered.
    self->ops_.tryStartDiscovery();
    self->endFetch();
    return 0;
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
    ops_.dropPendingPower();
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

    if (std::strcmp(iface, BluezClient::kAdapterIface) == 0) {
        if (!BluezClient::dictHasKey(m, {"Powered", "PowerState", "Discovering"})) { return 0; }
    } else if (std::strcmp(iface, BluezClient::kDeviceIface) == 0) {
        if (!BluezClient::dictHasKey(m, {"Connected", "Paired"})) { return 0; }
    } else if (std::strcmp(iface, BluezClient::kBatteryIface) == 0) {
        if (!BluezClient::dictHasKey(m, {"Percentage"})) { return 0; }
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
    if (ops_.setPowered(on).refetch) { refetch(); }
}

void BluetoothBackend::connectDevice(const std::string& path) {
    ops_.connectDevice(path);
}

void BluetoothBackend::disconnectDevice(const std::string& path) {
    ops_.disconnectDevice(path);
}

void BluetoothBackend::pairDevice(const std::string& path) {
    ops_.pairDevice(path);
}

void BluetoothBackend::forgetDevice(const std::string& path) {
    ops_.forgetDevice(path);
}

void BluetoothBackend::startDiscovery() {
    ops_.startDiscovery();
}

void BluetoothBackend::stopDiscovery() {
    ops_.stopDiscovery();
}

}  // namespace qypr
