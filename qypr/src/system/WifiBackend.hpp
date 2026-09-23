// WifiBackend.hpp - WiFi state via NetworkManager on the shared system bus.
//
// Async startup: all D-Bus calls use sd_bus_call_method_async so the event
// loop is never blocked. Push-only afterwards via PropertiesChanged.
//
// The backend also owns the *network list*: requestScan() kicks a NM scan and
// an async enumeration chain that publishes the visible APs into the snapshot
// (snapshot().networks). The chain re-runs whenever NM reports AP changes, so
// an open network picker live-updates without any polling. Connecting uses
// ActivateConnection for saved networks and AddAndActivateConnection for new
// ones — open networks join directly; secured ones take a PSK collected by the
// UI (connectPsk), which NM then persists as a saved profile.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "system/WifiModel.hpp"

struct sd_bus_message;
struct sd_bus_slot;
#include <systemd/sd-bus.h>  // sd_bus_error is a typedef here, not a struct

namespace qypr {

class SystemBus;

class WifiBackend {
public:
    explicit WifiBackend(SystemBus& bus);
    ~WifiBackend();

    WifiBackend(const WifiBackend&) = delete;
    WifiBackend& operator=(const WifiBackend&) = delete;

    bool start();

    const WifiSnapshot& snapshot() const { return snap_; }

    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    // True once the backend has produced its first result — real data or a
    // definitive "absent". Indicators show a neutral placeholder until then, so
    // an unrelated backend's push cannot prematurely mark this one loaded.
    bool ready() const { return ready_; }

    // Seed from the previous session's persisted snapshot (see StateCache).
    // The daemon that owns this state is often not running yet when the bar
    // starts — UPower in particular is D-Bus-activated and comes up *after* it
    // — so without a seed the indicator sits on its neutral "unknown" glyph for
    // seconds. Seeding marks the backend ready() so the very first frame
    // carries real values; the first live reply overwrites both the snapshot
    // and this flag. A no-op once a live reply has landed.
    void seed(const WifiSnapshot& s) {
        if (ready_) { return; }
        snap_ = s;
        // The picker list lives in the chain staging until the first chain
        // lands; seed it too or the next publish() would wipe the persisted
        // list and the picker would blink empty on every login.
        netResults_ = s.networks;
        ready_ = true;
    }

    void setEnabled(bool on);
    void requestScan();
    // Join a network: saved → ActivateConnection; unsaved open →
    // AddAndActivateConnection with just the SSID. A secured unsaved network
    // needs connectPsk() instead (the UI collects the password first).
    void connectAp(const WifiAp& ap);
    // Join a secured network with a passphrase (AddAndActivateConnection with
    // a wpa-psk profile; NM saves it, so the network becomes "saved").
    void connectPsk(const std::string& ssid, const std::string& psk);
    void connectSsid(const std::string& ssid);
    // Delete the saved profile for `ssid` (NM Settings.Connection.Delete) so
    // the network joins like a new one next time. No-op when unsaved.
    void forgetSsid(const std::string& ssid);
    void disconnect();

private:
    // Standard NM-client lifecycle: signals are subscribed *before* the initial
    // fetch, refreshes are serialized (never overlapping), and every terminal
    // path publishes a definitive snapshot so the placeholder always resolves.
    // NetworkManager 1.58 dropped the org.freedesktop.DBus.ObjectManager
    // interface, so enumeration is a GetDevices → per-device GetAll chain
    // (DeviceType/State → Wireless.ActiveAccessPoint → AP Ssid/Strength)
    // instead of one GetManagedObjects reply.
    static int onFetchStep(sd_bus_message* reply, void* userdata, sd_bus_error* err);
    void stepDevices(sd_bus_message* reply);
    void stepDeviceProps(sd_bus_message* reply);
    void stepWirelessProps(sd_bus_message* reply);
    void stepApProps(sd_bus_message* reply);
    void fetchDeviceAt(size_t index);
    void refreshAsync();
    void publish();
    void publishWifiFailure(const char* what);
    void finishNoWifi();
    void endFetch();
    void subscribeSignals();
    std::string findSavedConnection(const std::string& ssid) const;

    // ── Network-list chain (the picker's data source) ──
    // A second serialized chain: device GetAll (AccessPoints) → per-AP GetAll
    // (Ssid/Strength/Flags) → ListConnections → per-connection GetSettings
    // (saved SSIDs). Re-run on RequestScan completion (NM signals it as a
    // PropertiesChanged on the wireless device) and on any AP property change,
    // which is what makes an open picker live-update.
    static int onNetStep(sd_bus_message* reply, void* userdata, sd_bus_error* err);
    void refreshNetworksAsync();
    void endNetFetch();
    void netStepDevice(sd_bus_message* reply);
    void netStepAp(sd_bus_message* reply);
    void netStepConnections(sd_bus_message* reply);
    void netStepConnectionSettings(sd_bus_message* reply);
    void netFetchNext();
    void netFetchNextSaved();
    void finishNetFetch();
    void addAndActivate(const std::string& ssid, const std::string* psk);

    static int onPropsChanged(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onDeviceAdded(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onDeviceRemoved(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onNameOwnerChanged(sd_bus_message* m, void* userdata, sd_bus_error* err);

    SystemBus& bus_;
    sd_bus_slot* propsSlot_ = nullptr;    // PropertiesChanged on the NM object tree
    sd_bus_slot* addedSlot_ = nullptr;    // DeviceAdded (adapter hotplug)
    sd_bus_slot* removedSlot_ = nullptr;  // DeviceRemoved
    sd_bus_slot* ownerSlot_ = nullptr;    // NM service (re)appearance
    std::string device_;                  // WiFi device path ("" = none)
    std::string activeAp_;                // active AP path ("" = not connected)
    // Refresh serialization: while one fetch chain is in flight a change only
    // marks pendingRefresh_, and the next fetch runs when the chain lands.
    // Overlapping chains used to consume each other's replies and die silently.
    bool fetchInFlight_ = false;
    bool pendingRefresh_ = false;
    bool subscribed_ = false;
    // Fetch-chain state (valid between start() and endFetch()).
    std::vector<std::string> devices_;
    size_t devIndex_ = 0;
    uint32_t devState_ = 0;         // Device.State of the WiFi device
    uint8_t apStrength_ = 0;        // Strength of the active AP
    std::string apSsid_;            // Ssid of the active AP
    bool wirelessEnabled_ = false;  // NM root WirelessEnabled
    // Which reply the chain is waiting for next (dispatches onFetchStep).
    int fetchStep_ = 0;  // 0=devices, 1=props, 2=wireless, 3=ap
    WifiSnapshot snap_;
    std::function<void()> onChange_;
    // Every result path calls this instead of onChange_ directly, so ready()
    // flips true exactly when the first real snapshot is published.
    void notifyReady() {
        ready_ = true;
        if (onChange_) onChange_();
    }
    bool ready_ = false;

    // Network-list chain state (mirrors the serialization discipline above).
    bool netInFlight_ = false;
    bool pendingNetRefresh_ = false;
    // Which reply the network chain is waiting for. A phase, not a step index:
    // numbering the per-AP steps and giving the settings walk a fixed offset
    // collided as soon as the adapter saw that many networks, and the AP
    // replies were then handed to the saved-connection parser.
    enum class NetPhase : uint8_t { Device, Ap, Connections, ConnectionSettings };
    NetPhase netPhase_ = NetPhase::Device;
    std::vector<std::string> netAps_;
    std::string netActiveAp_;
    size_t netIndex_ = 0;
    std::vector<WifiAp> netResults_;
    std::vector<std::string> savedConns_;
    std::vector<std::pair<std::string, std::string>> savedPairs_;  // ssid → connection path
    size_t savedIndex_ = 0;
    bool savedDirty_ = true;  // refetch saved networks on the next chain run
    bool scanning_ = false;
};

}  // namespace qypr
