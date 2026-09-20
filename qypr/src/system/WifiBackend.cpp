// WifiBackend.cpp - NetworkManager monitor implementation (async startup, push-driven).
#include "system/WifiBackend.hpp"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "system/SystemBus.hpp"

namespace qypr {

namespace {
constexpr const char* kNM = "org.freedesktop.NetworkManager";
constexpr const char* kNMPath = "/org/freedesktop/NetworkManager";
constexpr const char* kDeviceIface = "org.freedesktop.NetworkManager.Device";
constexpr const char* kWirelessIface = "org.freedesktop.NetworkManager.Device.Wireless";
constexpr const char* kApIface = "org.freedesktop.NetworkManager.AccessPoint";
constexpr const char* kPropsIface = "org.freedesktop.DBus.Properties";
constexpr const char* kObjectManagerIface = "org.freedesktop.DBus.ObjectManager";
constexpr const char* kSettingsPath = "/org/freedesktop/NetworkManager/Settings";
constexpr const char* kSettingsIface = "org.freedesktop.NetworkManager.Settings";
constexpr const char* kSettingsConnIface = "org.freedesktop.NetworkManager.Settings.Connection";

constexpr uint32_t kApFlagPrivacy = 0x1;
constexpr uint32_t kWifiDeviceType = 2;          // NM_DEVICE_TYPE_WIFI
constexpr uint32_t kDeviceStateActivated = 100;  // NM_DEVICE_STATE_ACTIVATED

// Parse a GetSettings reply (a{sa{sv}}) and return the wireless SSID, if any.
std::string parseConnectionSsid(sd_bus_message* r) {
    std::string ssid;
    if (sd_bus_message_enter_container(r, 'a', "{sa{sv}}") >= 0) {
        while (sd_bus_message_enter_container(r, 'e', "sa{sv}") > 0) {
            const char* group = nullptr;
            sd_bus_message_read(r, "s", &group);
            if ((group != nullptr) && std::strcmp(group, "802-11-wireless") == 0 &&
                sd_bus_message_enter_container(r, 'a', "{sv}") >= 0) {
                while (sd_bus_message_enter_container(r, 'e', "sv") > 0) {
                    const char* key = nullptr;
                    sd_bus_message_read(r, "s", &key);
                    if ((key != nullptr) && std::strcmp(key, "ssid") == 0 &&
                        sd_bus_message_enter_container(r, 'v', "ay") >= 0) {
                        const void* data = nullptr;
                        size_t len = 0;
                        if (sd_bus_message_read_array(r, 'y', &data, &len) >= 0 &&
                            (data != nullptr) && len > 0) {
                            ssid.assign(static_cast<const char*>(data), len);
                        }
                        sd_bus_message_exit_container(r);
                    } else {
                        sd_bus_message_skip(r, "v");
                    }
                    sd_bus_message_exit_container(r);
                }
                sd_bus_message_exit_container(r);
            } else {
                sd_bus_message_skip(r, "a{sv}");
            }
            sd_bus_message_exit_container(r);
        }
        sd_bus_message_exit_container(r);
    }
    return ssid;
}

std::string connectionSsid(sd_bus* bus, const char* conn) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* r = nullptr;
    if (sd_bus_call_method(bus, kNM, conn, kSettingsConnIface, "GetSettings", &err, &r, "") < 0 ||
        (r == nullptr)) {
        sd_bus_error_free(&err);
        return "";
    }
    sd_bus_error_free(&err);
    std::string const ssid = parseConnectionSsid(r);
    sd_bus_message_unref(r);
    return ssid;
}

std::vector<std::pair<std::string, std::string>> savedConnections(sd_bus* bus) {
    std::vector<std::pair<std::string, std::string>> out;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    if (sd_bus_call_method(bus, kNM, kSettingsPath, kSettingsIface, "ListConnections", &err, &reply,
                           "") < 0 ||
        (reply == nullptr)) {
        sd_bus_error_free(&err);
        return out;
    }
    sd_bus_error_free(&err);
    if (sd_bus_message_enter_container(reply, 'a', "o") >= 0) {
        const char* conn = nullptr;
        while (sd_bus_message_read(reply, "o", &conn) > 0 && (conn != nullptr)) {
            std::string const ssid = connectionSsid(bus, conn);
            if (!ssid.empty()) { out.emplace_back(ssid, conn); }
        }
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);
    return out;
}

// Helpers to extract values from a Get property reply (v{type} container).
bool extractBool(sd_bus_message* m, bool* out) {
    if (sd_bus_message_enter_container(m, 'v', "b") < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    int v = 0;
    int const r = sd_bus_message_read_basic(m, 'b', &v);
    sd_bus_message_exit_container(m);
    if (r < 0) { return false; }
    *out = v != 0;
    return true;
}

bool extractObjPath(sd_bus_message* m, std::string* out) {
    if (sd_bus_message_enter_container(m, 'v', "o") < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    const char* s = nullptr;
    int const r = sd_bus_message_read_basic(m, 'o', static_cast<void*>(&s));
    sd_bus_message_exit_container(m);
    if (r < 0 || (s == nullptr)) { return false; }
    *out = s;
    return true;
}

// Object-path array from a property variant ("ao"). Entering the variant only
// exposes the array itself: its elements need a *second* enter_container.
// Reading "o" straight after the variant enter fails and — because the failed
// read leaves the parser mid-container — desyncs the enclosing a{sv} walk, so
// every property after this one is silently lost too.
bool extractObjPathArray(sd_bus_message* m, std::vector<std::string>* out) {
    if (sd_bus_message_enter_container(m, 'v', "ao") <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    if (sd_bus_message_enter_container(m, 'a', "o") > 0) {
        const char* p = nullptr;
        while (sd_bus_message_read(m, "o", &p) > 0 && (p != nullptr)) { out->emplace_back(p); }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return true;
}

bool extractByteArray(sd_bus_message* m, std::string* out) {
    if (sd_bus_message_enter_container(m, 'v', "ay") < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    const void* data = nullptr;
    size_t len = 0;
    int const r = sd_bus_message_read_array(m, 'y', &data, &len);
    sd_bus_message_exit_container(m);
    if (r < 0 || (data == nullptr) || len == 0) { return false; }
    out->assign(static_cast<const char*>(data), len);
    return true;
}

bool extractByte(sd_bus_message* m, uint8_t* out) {
    if (sd_bus_message_enter_container(m, 'v', "y") < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    int const r = sd_bus_message_read_basic(m, 'y', out);
    sd_bus_message_exit_container(m);
    return r >= 0;
}

bool extractU32(sd_bus_message* m, uint32_t* out) {
    if (sd_bus_message_enter_container(m, 'v', "u") < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    int const r = sd_bus_message_read_basic(m, 'u', out);
    sd_bus_message_exit_container(m);
    return r >= 0;
}
}  // namespace

WifiBackend::WifiBackend(SystemBus& bus) : bus_(bus) {}

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
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, kNMPath, kNM, "GetDevices",
                             &WifiBackend::onFetchStep, this, "");
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
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, kNMPath, kPropsIface, "GetAll",
                             &WifiBackend::onFetchStep, this, "s", kNM);
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
                extractBool(reply, &wirelessEnabled_);
                sawWirelessEnabled = true;
            } else if (std::strcmp(key, "DeviceType") == 0) {
                extractU32(reply, &devType);
            } else if (std::strcmp(key, "State") == 0) {
                extractU32(reply, &state);
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
    if (devType != kWifiDeviceType) {
        fetchDeviceAt(devIndex_ + 1);
        return;
    }
    // Found the WiFi device: remember it and its state, then read the active
    // access point.
    device_ = devices_.at(devIndex_);
    devState_ = state;
    fetchStep_ = 2;  // waiting for Wireless GetAll
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, device_.c_str(), kPropsIface, "GetAll",
                             &WifiBackend::onFetchStep, this, "s", kWirelessIface);
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
                extractObjPath(reply, &ap);
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
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, ap.c_str(), kPropsIface, "GetAll",
                             &WifiBackend::onFetchStep, this, "s", kApIface);
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
                extractByteArray(reply, &apSsid_);
            } else if (std::strcmp(key, "Strength") == 0) {
                extractByte(reply, &apStrength_);
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
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, devices_.at(devIndex_).c_str(), kPropsIface,
                             "GetAll", &WifiBackend::onFetchStep, this, "s", kDeviceIface);
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
    WifiSnapshot next;
    next.available = !device_.empty();
    next.enabled = wirelessEnabled_ && next.available;
    next.scanning = scanning_;
    next.networks = netResults_;
    if (!next.enabled) {
        next.networks.clear();  // radio off: the picker shows the off-state
        next.scanning = false;
    }
    if (next.available) {
        // Connection truth is the device state (100 = activated), not the mere
        // presence of an access point.
        next.connected = std::cmp_equal(devState_, kDeviceStateActivated);
        if (next.connected && !apSsid_.empty()) {
            next.ssid = apSsid_;
            next.strength = apStrength_;
        }
    }
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
    if (std::strcmp(path, kNMPath) != 0 && self->device_ != path && self->activeAp_ != path) {
        return 0;
    }
    self->refreshAsync();
    // A PropertiesChanged on the wireless device is also how NM reports scan
    // completion (the AccessPoints list changed) — refresh the picker's list.
    if (self->device_ == path || self->scanning_) { self->refreshNetworksAsync(); }
    // Connection profiles changed (new one saved, one forgotten): the next
    // network chain re-walks the saved SSIDs.
    if (std::strcmp(path, kSettingsPath) == 0) { self->savedDirty_ = true; }
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
    if (!bus_.available()) { return; }

    snap_.enabled = on;
    if (!on) {
        snap_.connected = false;
        snap_.ssid.clear();
        snap_.strength = 0;
        snap_.scanning = false;
        snap_.networks.clear();
        activeAp_.clear();
        netResults_.clear();
        scanning_ = false;
    }
    notifyReady();

    sd_bus_message* msg = nullptr;
    if (sd_bus_message_new_method_call(bus_.get(), &msg, kNM, kNMPath, kPropsIface, "Set") < 0) {
        return;
    }
    sd_bus_message_append(msg, "ss", kNM, "WirelessEnabled");
    sd_bus_message_open_container(msg, 'v', "b");
    sd_bus_message_append(msg, "b", on ? 1 : 0);
    sd_bus_message_close_container(msg);
    sd_bus_call_async(bus_.get(), nullptr, msg, nullptr, nullptr, 0);
    sd_bus_message_unref(msg);

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
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, device_.c_str(), kPropsIface, "GetAll",
                             &WifiBackend::onNetStep, this, "s", kWirelessIface);
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
                extractObjPathArray(reply, &netAps_);
            } else if (std::strcmp(key, "ActiveAccessPoint") == 0) {
                extractObjPath(reply, &netActiveAp_);
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
    uint32_t flags = 0, wpa = 0, rsn = 0;
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
                extractByteArray(reply, &ssid);
            } else if (std::strcmp(key, "Strength") == 0) {
                extractByte(reply, &strength);
            } else if (std::strcmp(key, "Flags") == 0) {
                extractU32(reply, &flags);
            } else if (std::strcmp(key, "WpaFlags") == 0) {
                extractU32(reply, &wpa);
            } else if (std::strcmp(key, "RsnFlags") == 0) {
                extractU32(reply, &rsn);
            } else {
                sd_bus_message_skip(reply, "v");
            }
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply);
    }

    if (!ssid.empty()) {
        const bool secured = (flags & kApFlagPrivacy) != 0U || wpa != 0U || rsn != 0U;
        const bool active = netAps_.at(static_cast<size_t>(netIndex_ - 1)) == netActiveAp_;
        auto it =
            std::ranges::find_if(netResults_, [&](const WifiAp& e) { return e.ssid == ssid; });
        if (it != netResults_.end()) {
            it->strength = std::max(it->strength, static_cast<int>(strength));
            it->active = it->active || active;
            it->secured = it->secured || secured;
        } else {
            netResults_.push_back({.ssid = ssid,
                                   .strength = strength,
                                   .secured = secured,
                                   .active = active,
                                   .saved = false});
        }
    }
    netFetchNext();
}

void WifiBackend::netFetchNext() {
    if (netIndex_ < netAps_.size()) {
        netPhase_ = NetPhase::Ap;
        sd_bus_call_method_async(bus_.get(), nullptr, kNM, netAps_.at(netIndex_).c_str(),
                                 kPropsIface, "GetAll", &WifiBackend::onNetStep, this, "s",
                                 kApIface);
        ++netIndex_;
        return;
    }
    // APs done. Saved-network flags come from the settings walk (cached).
    if (savedDirty_) {
        netPhase_ = NetPhase::Connections;
        sd_bus_call_method_async(bus_.get(), nullptr, kNM, kSettingsPath, kSettingsIface,
                                 "ListConnections", &WifiBackend::onNetStep, this, "");
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
    const std::string ssid = parseConnectionSsid(reply);
    if (!ssid.empty()) { savedPairs_.emplace_back(ssid, savedConns_.at(savedIndex_ - 1)); }
    netFetchNextSaved();
}

void WifiBackend::netFetchNextSaved() {
    if (savedIndex_ < savedConns_.size()) {
        netPhase_ = NetPhase::ConnectionSettings;
        sd_bus_call_method_async(bus_.get(), nullptr, kNM, savedConns_.at(savedIndex_).c_str(),
                                 kSettingsConnIface, "GetSettings", &WifiBackend::onNetStep, this,
                                 "");
        ++savedIndex_;
        return;
    }
    savedDirty_ = false;
    finishNetFetch();
}

// Terminal step: merge saved flags, sort, publish. Also runs on chain errors,
// so it must tolerate partial results.
void WifiBackend::finishNetFetch() {
    for (const auto& [ssid, conn] : savedPairs_) {
        (void)conn;
        auto it =
            std::ranges::find_if(netResults_, [&](const WifiAp& e) { return e.ssid == ssid; });
        if (it != netResults_.end()) { it->saved = true; }
    }
    std::ranges::stable_sort(netResults_, [](const WifiAp& a, const WifiAp& b) {
        if (a.active != b.active) { return a.active > b.active; }
        return a.strength > b.strength;
    });
    scanning_ = false;
    publish();
    endNetFetch();
}

void WifiBackend::requestScan() {
    sd_bus* bus = bus_.get();
    if ((bus == nullptr) || device_.empty()) { return; }
    scanning_ = true;
    publish();  // spinner on immediately
    sd_bus_message* msg = nullptr;
    if (sd_bus_message_new_method_call(bus, &msg, kNM, device_.c_str(), kWirelessIface,
                                       "RequestScan") < 0) {
        return;
    }
    sd_bus_message_open_container(msg, 'a', "{sv}");
    sd_bus_message_close_container(msg);
    sd_bus_call_async(bus, nullptr, msg, nullptr, nullptr, 0);
    sd_bus_message_unref(msg);
    // Publish the current list right away (it may be stale or empty); NM
    // signals the fresh one as a PropertiesChanged on the device, which
    // re-enters refreshNetworksAsync().
    refreshNetworksAsync();
}

// AddAndActivateConnection(connection dict, device, specific_object): NM's
// one-shot join for networks with no saved profile. The dictionary carries the
// SSID and — for secured networks — the wpa-psk security block; NM persists
// the profile, so the network shows as saved afterwards.

namespace {

// Append one dict entry "s → variant(ay)" into an open a{sv}.
int appendSvBytes(sd_bus_message* msg, const char* key, const std::string& bytes) {
    int r = sd_bus_message_open_container(msg, SD_BUS_TYPE_DICT_ENTRY_BEGIN, "sv");
    if (r >= 0) { r = sd_bus_message_append(msg, "s", key); }
    if (r >= 0) { r = sd_bus_message_open_container(msg, SD_BUS_TYPE_VARIANT, "ay"); }
    if (r >= 0) { r = sd_bus_message_append_array(msg, 'y', bytes.data(), bytes.size()); }
    if (r >= 0) { r = sd_bus_message_close_container(msg); }  // v
    if (r >= 0) { r = sd_bus_message_close_container(msg); }  // dict entry
    return r;
}

// Append one dict entry "s → variant(s)" into an open a{sv}.
int appendSvString(sd_bus_message* msg, const char* key, const char* value) {
    int r = sd_bus_message_open_container(msg, SD_BUS_TYPE_DICT_ENTRY_BEGIN, "sv");
    if (r >= 0) { r = sd_bus_message_append(msg, "ssv", key, "s", value); }
    if (r >= 0) { r = sd_bus_message_close_container(msg); }
    return r;
}

// Append one "group" entry (group-name → variant(a{sv})) into an open a{sa{sv}}.
int appendGroup(sd_bus_message* msg, const char* group,
                std::initializer_list<std::pair<const char*, const char*>> strings,
                const std::string* ssidBytes) {
    int r = sd_bus_message_open_container(msg, SD_BUS_TYPE_DICT_ENTRY_BEGIN, "sa{sv}");
    if (r >= 0) { r = sd_bus_message_append(msg, "s", group); }
    if (r >= 0) { r = sd_bus_message_open_container(msg, SD_BUS_TYPE_VARIANT, "a{sv}"); }
    if (r >= 0) { r = sd_bus_message_open_container(msg, SD_BUS_TYPE_ARRAY, "{sv}"); }
    if (r >= 0 && ssidBytes != nullptr) { r = appendSvBytes(msg, "ssid", *ssidBytes); }
    for (const auto& kv : strings) {
        if (r < 0) { break; }
        r = appendSvString(msg, kv.first, kv.second);
    }
    if (r >= 0) { r = sd_bus_message_close_container(msg); }  // a{sv}
    if (r >= 0) { r = sd_bus_message_close_container(msg); }  // v
    if (r >= 0) { r = sd_bus_message_close_container(msg); }  // dict entry
    return r;
}
}  // namespace

void WifiBackend::addAndActivate(const std::string& ssid, const std::string* psk) {
    sd_bus* bus = bus_.get();
    if ((bus == nullptr) || ssid.empty() || device_.empty()) { return; }
    sd_bus_message* msg = nullptr;
    if (sd_bus_message_new_method_call(bus, &msg, kNM, kNMPath, kNM, "AddAndActivateConnection") <
        0) {
        return;
    }
    int r = sd_bus_message_open_container(msg, SD_BUS_TYPE_ARRAY, "{sa{sv}");
    if (r >= 0) { r = appendGroup(msg, "802-11-wireless", {}, &ssid); }
    if (r >= 0 && psk != nullptr && !psk->empty()) {
        r = appendGroup(msg, "802-11-wireless-security",
                        {{"key-mgmt", "wpa-psk"}, {"psk", psk->c_str()}}, nullptr);
    }
    if (r >= 0) { r = sd_bus_message_close_container(msg); }
    if (r >= 0) { r = sd_bus_message_append(msg, "oo", device_.c_str(), "/"); }
    if (r < 0) {
        std::fprintf(stderr, "qypr: failed to build AddAndActivateConnection message\n");
    } else {
        sd_bus_call_async(bus, nullptr, msg, nullptr, nullptr, 0);
    }
    sd_bus_message_unref(msg);
}

std::string WifiBackend::findSavedConnection(const std::string& ssid) const {
    // Cache first (warm after any network-list chain); sync bus walk only as a
    // cold fallback.
    for (const auto& [s, conn] : savedPairs_) {
        if (s == ssid) { return conn; }
    }
    if (!savedPairs_.empty()) { return {}; }
    sd_bus* bus = bus_.get();
    if (bus == nullptr) { return {}; }
    for (auto& p : savedConnections(bus)) {
        if (p.first == ssid) { return p.second; }
    }
    return {};
}

void WifiBackend::connectAp(const WifiAp& ap) {
    if (ap.active || ap.ssid.empty()) { return; }
    if (ap.saved) {
        connectSsid(ap.ssid);
        return;
    }
    addAndActivate(ap.ssid, nullptr);  // open network: join with just the SSID
}

void WifiBackend::connectPsk(const std::string& ssid, const std::string& psk) {
    if (psk.empty()) { return; }
    addAndActivate(ssid, &psk);
}

void WifiBackend::connectSsid(const std::string& ssid) {
    sd_bus* bus = bus_.get();
    if ((bus == nullptr) || ssid.empty() || device_.empty()) { return; }
    const std::string conn = findSavedConnection(ssid);
    if (conn.empty()) { return; }
    sd_bus_call_method_async(bus, nullptr, kNM, kNMPath, kNM, "ActivateConnection", nullptr,
                             nullptr, "ooo", conn.c_str(), device_.c_str(), "/");
}

void WifiBackend::forgetSsid(const std::string& ssid) {
    sd_bus* bus = bus_.get();
    if (bus == nullptr) { return; }
    const std::string conn = findSavedConnection(ssid);
    if (conn.empty()) { return; }
    sd_bus_call_method_async(bus, nullptr, kNM, conn.c_str(), kSettingsConnIface, "Delete", nullptr,
                             nullptr, "");
    // NM signals the removal, but the next chain should re-walk the saved set
    // regardless — and the picker's row drops its "saved" flag right away.
    savedDirty_ = true;
    refreshNetworksAsync();
}

void WifiBackend::disconnect() {
    sd_bus* bus = bus_.get();
    if ((bus == nullptr) || device_.empty()) { return; }
    sd_bus_call_method_async(bus, nullptr, kNM, device_.c_str(), kDeviceIface, "Disconnect",
                             nullptr, nullptr, "");
}

}  // namespace qypr
