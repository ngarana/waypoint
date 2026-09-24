// WifiBackend.cpp - NetworkManager monitor facade (async startup, push-driven).
//
// Lifecycle, subscriptions, both serialized fetch chains, and snapshot
// publication live here. D-Bus mechanics live in NetworkManagerClient, pure
// snapshot shaping in WifiSnapshotReducer, and user commands in
// WifiOperations (behind WifiCommandPort, adapted to the facade's device
// path by FacadePort).
#include "system/WifiBackend.hpp"

#include <systemd/sd-bus.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "system/NetworkManagerClient.hpp"
#include "system/SystemBus.hpp"
#include "system/WifiSnapshotReducer.hpp"

namespace qypr {

WifiBackend::WifiBackend(SystemBus& bus)
    : bus_(bus),
      client_(bus),
      port_(client_, device_),
      ops_(port_) {}

WifiBackend::~WifiBackend() {
    sd_bus_slot_unref(propsSlot_);
    sd_bus_slot_unref(addedSlot_);
    sd_bus_slot_unref(removedSlot_);
    sd_bus_slot_unref(ownerSlot_);
}

bool WifiBackend::start() {
    if (!bus_.available()) { return false; }

    // Subscribe before the first fetch: a change landing between the fetch and
    // the subscription would be lost (standard subscribe-then-fetch order).
    subscribeSignals();
    refreshAsync();
    return true;
}

void WifiBackend::subscribeSignals() {
    if (subscribed_) { return; }
    subscribed_ = true;
    // Scoped to the NM object tree; the handler still filters by path.
    propsSlot_ =
        bus_.addMatch("type='signal',sender='org.freedesktop.NetworkManager',"
                      "path_namespace='/org/freedesktop/NetworkManager',"
                      "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
                      &WifiBackend::onPropsChanged, this);
    // Adapters appear/disappear (USB dongles) outside PropertiesChanged.
    addedSlot_ = bus_.addMatch("type='signal',sender='org.freedesktop.NetworkManager',"
                               "interface='org.freedesktop.NetworkManager',member='DeviceAdded'",
                               &WifiBackend::onDeviceAdded, this);
    removedSlot_ =
        bus_.addMatch("type='signal',sender='org.freedesktop.NetworkManager',"
                      "interface='org.freedesktop.NetworkManager',member='DeviceRemoved'",
                      &WifiBackend::onDeviceRemoved, this);
    // NM may not be up when the bar starts; re-enumerate when it (re)appears.
    ownerSlot_ = bus_.addMatch(
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',arg0='org.freedesktop.NetworkManager'",
        &WifiBackend::onNameOwnerChanged, this);
}

// One fetch chain in flight at a time: a signal during the chain only marks
// pendingRefresh_, and the next chain runs when the current one lands. This is
// what keeps replies from different chains from arriving out of order and
// regressing the snapshot to stale state.
void WifiBackend::refreshAsync() {
    if (!bus_.available()) { return; }
    if (fetchInFlight_) {
        pendingRefresh_ = true;  // a change arrived mid-fetch; re-run after
        return;
    }
    fetchInFlight_ = true;
    devices_.clear();
    devIndex_ = 0;
    devState_ = 0;
    apStrength_ = 0;
    apSsid_.clear();
    fetchStep_ = 0;  // waiting for GetDevices
    // NetworkManager 1.58 removed org.freedesktop.DBus.ObjectManager, so
    // enumerate with GetDevices and read the properties we need per object.
    client_.getDevices(&WifiBackend::onFetchStep, this);
}

void WifiBackend::endFetch() {
    fetchInFlight_ = false;
    if (pendingRefresh_) {
        pendingRefresh_ = false;
        refreshAsync();
    }
}

int WifiBackend::onFetchStep(sd_bus_message* reply, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<WifiBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        // Transient failure (NM restarting at bar start): publish a definitive
        // "absent" instead of leaving the placeholder pending forever.
        self->publishWifiFailure("NetworkManager unavailable; wifi indicator disabled");
        self->endFetch();
        return 0;
    }
    // Dispatch to the step the chain is currently waiting on; a reply can only
    // arrive for the request the chain last issued.
    switch (self->fetchStep_) {
        case 0:
            self->stepDevices(reply);
            break;
        case 1:
            self->stepDeviceProps(reply);
            break;
        case 2:
            self->stepWirelessProps(reply);
            break;
        case 3:
            self->stepApProps(reply);
            break;
        default:
            self->publishWifiFailure("internal: unexpected wifi fetch step");
            self->endFetch();
    }
    return 0;
}

// GetDevices → ao: collect the device paths; then GetAll on the NM root to
// learn WirelessEnabled, then walk the devices to find the WiFi one.
void WifiBackend::stepDevices(sd_bus_message* reply) {
    devices_.clear();
    if (sd_bus_message_enter_container(reply, 'a', "o") >= 0) {
        const char* path = nullptr;
        while (sd_bus_message_read(reply, "o", &path) > 0 && (path != nullptr)) {
            devices_.emplace_back(path);
        }
        sd_bus_message_exit_container(reply);
    }

    if (devices_.empty()) {
        finishNoWifi();
        return;
    }
    client_.getAll(NetworkManagerClient::kNMPath, NetworkManagerClient::kNM,
                   &WifiBackend::onFetchStep, this);
    fetchStep_ = 1;  // waiting for root GetAll
}

// GetAll on a device → DeviceType/State; the root object's GetAll carries
// WirelessEnabled (GetAll returns every property, so the reply shape is
// identical regardless of object).
void WifiBackend::stepDeviceProps(sd_bus_message* reply) {
    uint32_t devType = 0;
    uint32_t state = 0;
    bool sawWirelessEnabled = false;
    if (sd_bus_message_enter_container(reply, 'a', "{sv}") >= 0) {
        while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
            const char* key = nullptr;
            sd_bus_message_read(reply, "s", &key);
            if (key == nullptr) {
                sd_bus_message_skip(reply, "v");
                sd_bus_message_exit_container(reply);
                continue;
            }
            if (std::strcmp(key, "WirelessEnabled") == 0) {
                NetworkManagerClient::extractBool(reply, &wirelessEnabled_);
                sawWirelessEnabled = true;
            } else if (std::strcmp(key, "DeviceType") == 0) {
                NetworkManagerClient::extractU32(reply, &devType);
            } else if (std::strcmp(key, "State") == 0) {
                NetworkManagerClient::extractU32(reply, &state);
            } else {
                sd_bus_message_skip(reply, "v");
            }
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply);
    }

    if (sawWirelessEnabled) {
        // Root GetAll: only WirelessEnabled is interesting; start walking the
        // devices.
        fetchDeviceAt(0);
        return;
    }
    if (devType != NetworkManagerClient::kWifiDeviceType) {
        fetchDeviceAt(devIndex_ + 1);
        return;
    }
    // Found the WiFi device: remember it and its state, then read the active
    // access point.
    device_ = devices_.at(devIndex_);
    devState_ = state;
    fetchStep_ = 2;  // waiting for Wireless GetAll
    client_.getAll(device_, NetworkManagerClient::kWirelessIface, &WifiBackend::onFetchStep, this);
}

// GetAll on the WiFi device (Wireless interface) → ActiveAccessPoint.
void WifiBackend::stepWirelessProps(sd_bus_message* reply) {
    std::string ap;
    if (sd_bus_message_enter_container(reply, 'a', "{sv}") >= 0) {
        while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
            const char* key = nullptr;
            sd_bus_message_read(reply, "s", &key);
            if (key == nullptr) {
                sd_bus_message_skip(reply, "v");
                sd_bus_message_exit_container(reply);
                continue;
            }
            if (std::strcmp(key, "ActiveAccessPoint") == 0) {
                NetworkManagerClient::extractObjPath(reply, &ap);
            } else {
                sd_bus_message_skip(reply, "v");
            }
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply);
    }

    if (ap.empty() || ap == "/") {
        // Not connected: publish the enabled/available state immediately.
        activeAp_.clear();
        apSsid_.clear();
        apStrength_ = 0;
        publish();
        endFetch();
        return;
    }
    activeAp_ = ap;
    fetchStep_ = 3;  // waiting for AP GetAll
    client_.getAccessPointProps(ap, &WifiBackend::onFetchStep, this);
}

// GetAll on the active AP → Ssid/Strength; terminal step of the chain.
void WifiBackend::stepApProps(sd_bus_message* reply) {
    if (sd_bus_message_enter_container(reply, 'a', "{sv}") >= 0) {
        while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
            const char* key = nullptr;
            sd_bus_message_read(reply, "s", &key);
            if (key == nullptr) {
                sd_bus_message_skip(reply, "v");
                sd_bus_message_exit_container(reply);
                continue;
            }
            if (std::strcmp(key, "Ssid") == 0) {
                NetworkManagerClient::extractByteArray(reply, &apSsid_);
            } else if (std::strcmp(key, "Strength") == 0) {
                NetworkManagerClient::extractByte(reply, &apStrength_);
            } else {
                sd_bus_message_skip(reply, "v");
            }
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply);
    }
    publish();
    endFetch();
}

void WifiBackend::fetchDeviceAt(size_t index) {
    devIndex_ = index;
    if (devIndex_ >= devices_.size()) {
        finishNoWifi();
        return;
    }
    fetchStep_ = 1;  // waiting for device GetAll
    client_.getAll(devices_.at(devIndex_), NetworkManagerClient::kDeviceIface,
                   &WifiBackend::onFetchStep, this);
}

void WifiBackend::finishNoWifi() {
    if (!ready_) {
        std::fprintf(stderr, "qypr: no WiFi device via NetworkManager; wifi indicator disabled\n");
    }
    device_.clear();
    activeAp_.clear();
    apSsid_.clear();
    apStrength_ = 0;
    netResults_.clear();
    scanning_ = false;
    publish();
    endFetch();
}

void WifiBackend::publishWifiFailure(const char* what) {
    std::fprintf(stderr, "qypr: %s\n", what);
    device_.clear();
    activeAp_.clear();
    apSsid_.clear();
    apStrength_ = 0;
    netResults_.clear();
    scanning_ = false;
    publish();
}

void WifiBackend::publish() {
    WifiFetchState fetch;
    fetch.haveDevice = !device_.empty();
    fetch.wirelessEnabled = wirelessEnabled_;
    fetch.scanning = scanning_;
    fetch.deviceState = devState_;
    fetch.apSsid = apSsid_;
    fetch.apStrength = static_cast<int>(apStrength_);
    fetch.networks = netResults_;
    WifiSnapshot next = reducePublished(fetch);
    if (ready_ && next == snap_) { return; }  // no change: skip repaint
    snap_ = next;
    notifyReady();
}

int WifiBackend::onPropsChanged(sd_bus_message* m, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<WifiBackend*>(userdata);
    const char* path = sd_bus_message_get_path(m);
    if (path == nullptr) { return 0; }

    // Only NM-wide and our-device/AP changes refresh; everything else is
    // irrelevant to the module (serialized by refreshAsync anyway).
    if (std::strcmp(path, NetworkManagerClient::kNMPath) != 0 && self->device_ != path &&
        self->activeAp_ != path) {
        return 0;
    }
    self->refreshAsync();
    // A PropertiesChanged on the wireless device is also how NM reports scan
    // completion (the AccessPoints list changed) — refresh the picker's list.
    if (self->device_ == path || self->scanning_) { self->refreshNetworksAsync(); }
    // Connection profiles changed (new one saved, one forgotten): the next
    // network chain re-walks the saved SSIDs.
    if (std::strcmp(path, NetworkManagerClient::kSettingsPath) == 0) { self->savedDirty_ = true; }
    return 0;
}

int WifiBackend::onDeviceAdded(sd_bus_message* /*unused*/, void* userdata,
                               sd_bus_error* /*unused*/) {
    static_cast<WifiBackend*>(userdata)->refreshAsync();
    return 0;
}

int WifiBackend::onDeviceRemoved(sd_bus_message* /*unused*/, void* userdata,
                                 sd_bus_error* /*unused*/) {
    static_cast<WifiBackend*>(userdata)->refreshAsync();
    return 0;
}

int WifiBackend::onNameOwnerChanged(sd_bus_message* m, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<WifiBackend*>(userdata);
    const char* name = nullptr;
    const char* oldOwner = nullptr;
    const char* newOwner = nullptr;
    if (sd_bus_message_read(m, "sss", &name, &oldOwner, &newOwner) < 0 || (name == nullptr)) {
        return 0;
    }
    if (newOwner == nullptr || *newOwner == '\0') { return 0; }  // gone: keep last state
    self->refreshAsync();                                        // (re)appeared: re-enumerate
    return 0;
}

void WifiBackend::setEnabled(bool on) {
    // Update the UI-facing snapshot first. This is intentionally optimistic:
    // seeded/headless state must still respond to a toggle when NetworkManager
    // is unavailable, while the live command below remains best-effort.
    snap_ = withRadioState(std::move(snap_), on);
    if (!on) {
        activeAp_.clear();
        netResults_.clear();
        scanning_ = false;
    }
    notifyReady();

    if (!bus_.available()) { return; }

    ops_.setEnabled(on);

    // Turning the radio on: populate the picker as soon as NM answers (the
    // first list lands before the scan completes; NM pushes the fresh one).
    if (on) { refreshNetworksAsync(); }
}

// ── Network-list chain ────────────────────────────────────────────────────
// Serialized exactly like the status chain: one chain in flight, a concurrent
// request only marks pendingNetRefresh_. Steps:
//   Device             device GetAll(Wireless)    → AccessPoints + ActiveAccessPoint
//   Ap                 per-AP GetAll(AccessPoint) → Ssid/Strength/Flags/WpaFlags/RsnFlags
//   Connections        Settings.ListConnections   → connection paths
//   ConnectionSettings per-conn GetSettings       → saved SSIDs
// The saved-network walk is skipped when the cache is warm (savedDirty_).

int WifiBackend::onNetStep(sd_bus_message* reply, void* userdata, sd_bus_error* /*unused*/) {
    auto* self = static_cast<WifiBackend*>(userdata);
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        // A failed step must not wedge the spinner: publish whatever the chain
        // gathered so far and drop the scanning flag.
        self->finishNetFetch();
        self->endNetFetch();
        return 0;
    }
    switch (self->netPhase_) {
        case NetPhase::Device:
            self->netStepDevice(reply);
            break;
        case NetPhase::Ap:
            self->netStepAp(reply);
            break;
        case NetPhase::Connections:
            self->netStepConnections(reply);
            break;
        case NetPhase::ConnectionSettings:
            self->netStepConnectionSettings(reply);
            break;
    }
    return 0;
}

void WifiBackend::refreshNetworksAsync() {
    if (!bus_.available()) { return; }
    if (device_.empty()) { return; }
    if (netInFlight_) {
        pendingNetRefresh_ = true;
        return;
    }
    netInFlight_ = true;
    netAps_.clear();
    netActiveAp_.clear();
    netIndex_ = 0;
    netPhase_ = NetPhase::Device;
    client_.getAll(device_, NetworkManagerClient::kWirelessIface, &WifiBackend::onNetStep, this);
}

void WifiBackend::endNetFetch() {
    netInFlight_ = false;
    if (pendingNetRefresh_) {
        pendingNetRefresh_ = false;
        refreshNetworksAsync();
    }
}

// Device GetAll(Wireless) → AccessPoints (ao) + ActiveAccessPoint (o).
void WifiBackend::netStepDevice(sd_bus_message* reply) {
    netAps_.clear();
    netActiveAp_.clear();
    if (sd_bus_message_enter_container(reply, 'a', "{sv}") >= 0) {
        while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
            const char* key = nullptr;
            sd_bus_message_read(reply, "s", &key);
            if (key == nullptr) {
                sd_bus_message_skip(reply, "v");
                sd_bus_message_exit_container(reply);
                continue;
            }
            if (std::strcmp(key, "AccessPoints") == 0) {
                NetworkManagerClient::extractObjPathArray(reply, &netAps_);
            } else if (std::strcmp(key, "ActiveAccessPoint") == 0) {
                NetworkManagerClient::extractObjPath(reply, &netActiveAp_);
            } else {
                sd_bus_message_skip(reply, "v");
            }
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply);
    }

    netResults_.clear();
    netIndex_ = 0;
    netFetchNext();
}

// Per-AP GetAll(AccessPoint) → one WifiAp entry; BSSIDs of the same SSID are
// merged keeping the strongest signal (mesh/repeaters show as one row).
void WifiBackend::netStepAp(sd_bus_message* reply) {
    std::string ssid;
    uint8_t strength = 0;
    uint32_t flags = 0;
    uint32_t wpa = 0;
    uint32_t rsn = 0;
    if (sd_bus_message_enter_container(reply, 'a', "{sv}") >= 0) {
        while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
            const char* key = nullptr;
            sd_bus_message_read(reply, "s", &key);
            if (key == nullptr) {
                sd_bus_message_skip(reply, "v");
                sd_bus_message_exit_container(reply);
                continue;
            }
            if (std::strcmp(key, "Ssid") == 0) {
                NetworkManagerClient::extractByteArray(reply, &ssid);
            } else if (std::strcmp(key, "Strength") == 0) {
                NetworkManagerClient::extractByte(reply, &strength);
            } else if (std::strcmp(key, "Flags") == 0) {
                NetworkManagerClient::extractU32(reply, &flags);
            } else if (std::strcmp(key, "WpaFlags") == 0) {
                NetworkManagerClient::extractU32(reply, &wpa);
            } else if (std::strcmp(key, "RsnFlags") == 0) {
                NetworkManagerClient::extractU32(reply, &rsn);
            } else {
                sd_bus_message_skip(reply, "v");
            }
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply);
    }

    if (!ssid.empty()) {
        const bool secured =
            (flags & NetworkManagerClient::kApFlagPrivacy) != 0U || wpa != 0U || rsn != 0U;
        const bool active = netAps_.at(static_cast<size_t>(netIndex_ - 1)) == netActiveAp_;
        netResults_ =
            mergeApReading(std::move(netResults_), {.ssid = ssid,
                                                    .strength = static_cast<int>(strength),
                                                    .secured = secured,
                                                    .active = active});
    }
    netFetchNext();
}

void WifiBackend::netFetchNext() {
    if (netIndex_ < netAps_.size()) {
        netPhase_ = NetPhase::Ap;
        client_.getAccessPointProps(netAps_.at(netIndex_), &WifiBackend::onNetStep, this);
        ++netIndex_;
        return;
    }
    // APs done. Saved-network flags come from the settings walk (cached).
    if (savedDirty_) {
        netPhase_ = NetPhase::Connections;
        client_.listConnections(&WifiBackend::onNetStep, this);
        return;
    }
    finishNetFetch();
}

void WifiBackend::netStepConnections(sd_bus_message* reply) {
    savedConns_.clear();
    savedPairs_.clear();
    if (sd_bus_message_enter_container(reply, 'a', "o") >= 0) {
        const char* conn = nullptr;
        while (sd_bus_message_read(reply, "o", &conn) > 0 && (conn != nullptr)) {
            savedConns_.emplace_back(conn);
        }
        sd_bus_message_exit_container(reply);
    }
    savedIndex_ = 0;
    netFetchNextSaved();
}

void WifiBackend::netStepConnectionSettings(sd_bus_message* reply) {
    const std::string ssid = NetworkManagerClient::parseConnectionSsid(reply);
    if (!ssid.empty()) { savedPairs_.emplace_back(ssid, savedConns_.at(savedIndex_ - 1)); }
    netFetchNextSaved();
}

void WifiBackend::netFetchNextSaved() {
    if (savedIndex_ < savedConns_.size()) {
        netPhase_ = NetPhase::ConnectionSettings;
        client_.getConnectionSettings(savedConns_.at(savedIndex_), &WifiBackend::onNetStep, this);
        ++savedIndex_;
        return;
    }
    savedDirty_ = false;
    finishNetFetch();
}

// Terminal step: merge saved flags, sort, publish. Also runs on chain errors,
// so it must tolerate partial results.
void WifiBackend::finishNetFetch() {
    applySavedFlags(netResults_, savedPairs_);
    sortForPicker(netResults_);
    ops_.setSavedCache(savedPairs_);
    scanning_ = false;
    publish();
    endNetFetch();
}

void WifiBackend::requestScan() {
    scanning_ = true;

    // Keep the spinner responsive when the snapshot came from the cache or a
    // headless test has no live NetworkManager connection. A real bus request
    // is best-effort, but the UI state must not depend on D-Bus availability.
    if (!bus_.available()) {
        snap_.scanning = true;
        notifyReady();
        return;
    }
    if (device_.empty()) {
        scanning_ = false;
        return;
    }

    publish();  // spinner on immediately
    ops_.requestScan();
    // Publish the current list right away (it may be stale or empty); NM
    // signals the fresh one as a PropertiesChanged on the device, which
    // re-enters refreshNetworksAsync().
    refreshNetworksAsync();
}

void WifiBackend::connectAp(const WifiAp& ap) {
    ops_.connectAp(ap);
}

void WifiBackend::connectPsk(const std::string& ssid, const std::string& psk) {
    ops_.connectPsk(ssid, psk);
}

void WifiBackend::connectSsid(const std::string& ssid) {
    ops_.connectSsid(ssid);
}

void WifiBackend::forgetSsid(const std::string& ssid) {
    if (!bus_.available()) { return; }
    if (ops_.forgetSsid(ssid).refreshNetworks) {
        // NM signals the removal, but the next chain should re-walk the saved
        // set regardless — and the picker's row drops its "saved" flag right
        // away.
        savedDirty_ = true;
        refreshNetworksAsync();
    }
}

void WifiBackend::disconnect() {
    ops_.disconnect();
}

}  // namespace qypr
