// BluetoothBackend.hpp - Bluetooth state via BlueZ on the shared system bus.
//
// Async startup: GetManagedObjects is issued via sd_bus_call_method_async so
// the event loop is never blocked. Push-only afterwards via PropertiesChanged.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "system/BluetoothAgent.hpp"
#include "system/BluetoothModel.hpp"
#include "system/BluetoothOperations.hpp"
#include "system/BluezClient.hpp"

struct sd_bus_message;
struct sd_bus_slot;
#include <systemd/sd-bus.h>  // sd_bus_error is a typedef here, not a struct

namespace qypr {

class SystemBus;

class BluetoothBackend {
public:
    explicit BluetoothBackend(SystemBus& bus);
    ~BluetoothBackend();

    BluetoothBackend(const BluetoothBackend&) = delete;
    BluetoothBackend& operator=(const BluetoothBackend&) = delete;

    bool start();

    const BluetoothSnapshot& snapshot() const { return snap_; }

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
    void seed(const BluetoothSnapshot& s) {
        if (ready_) { return; }
        snap_ = s;
        ready_ = true;
    }

    void setPowered(bool on);
    void connectDevice(const std::string& path);
    void disconnectDevice(const std::string& path);
    // Whether this backend exports an org.bluez.Agent1. On by default (the
    // unlocked bar wants BlueZ to raise its pairing prompts in the picker); the
    // lock screen turns it off, so nothing in a locked session can answer, or be
    // asked for, pairing credentials. See QL-1 in docs/LOCK_SECURITY_REVIEW.md.
    void setPairingAgentEnabled(bool on) { agentEnabled_ = on; }
    // True while this host is willing to answer Pairing1/Pairing2 from a
    // peripheral. Read by the Wi-Fi/Bluetooth picker gating (QL-1) and by the
    // lock path, which must always observe false.
    bool pairingAgentEnabled() const { return agentEnabled_; }

    // Device discovery. BlueZ only reports devices it has already seen, so a
    // picker that wants to onboard a *new* device has to run a scan: the
    // popover starts one while it is open and stops it on close (discovery is
    // expensive — it keeps the radio busy and drains battery).
    void startDiscovery();
    void stopDiscovery();
    // Pair → Trust → Connect, BlueZ's standard onboarding sequence. Trusting is
    // what lets the device reconnect on its own afterwards.
    //
    // Whether this can complete without the user seeing anything depends on the
    // agent: this backend *does* export org.bluez.Agent1 (BluetoothAgent), so
    // devices that want a passkey or a numeric comparison prompt render inside
    // the device picker. The lock screen disables the agent
    // (setPairingAgentEnabled(false)), which is why it must also deny the picker
    // itself — see QL-1 in docs/LOCK_SECURITY_REVIEW.md.
    void pairDevice(const std::string& path);
    // Adapter1.RemoveDevice: drop the pairing so the device onboards afresh.
    void forgetDevice(const std::string& path);

    // Answer the pending pairing prompt (snapshot().pairing). `accept` covers
    // the confirm/authorize kinds; respondPairingInput supplies a typed passkey
    // or PIN, and an empty string there declines.
    void respondPairing(bool accept);
    void respondPairingInput(const std::string& text);

private:
    static int onGetManagedObjects(sd_bus_message* reply, void* userdata, sd_bus_error* err);
    // Sole publication seam: skips the repaint when nothing actually changed.
    void publish(BluetoothSnapshot&& next);
    // Publish a definitive "no adapter" and drop any deferred power toggle.
    void publishUnavailable();
    void refetch();
    void endFetch();
    void subscribeSignals();

    // Port adapter: user commands need the facade's current adapter path, but
    // fetch and lifecycle state never enters the client — so the port is a
    // thin forwarder over the client plus an adapter reference, owned by the
    // facade.
    struct FacadePort : BluezCommandPort {
        FacadePort(BluezClient& client, const std::string& adapter)
            : client(client),
              adapter(adapter) {}
        bool available() const override { return client.available(); }
        std::string adapterPath() const override { return adapter; }
        bool setPowered(bool on, Reply onReply) override {
            return client.setAdapterPowered(adapter, on, std::move(onReply));
        }
        bool callDevice(const std::string& path, const char* member, uint64_t timeoutUs,
                        Reply onReply) override {
            return client.callDevice(path, member, timeoutUs, std::move(onReply));
        }
        void callAdapter(const char* member, Reply onReply) override {
            client.callAdapter(adapter, member, std::move(onReply));
        }
        bool removeDevice(const std::string& path, Reply onReply) override {
            return client.removeDevice(adapter, path, std::move(onReply));
        }
        void setTrusted(const std::string& path) override { client.setTrusted(path); }
        BluezClient& client;
        const std::string& adapter;
    };

    static int onPropsChanged(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onInterfacesChanged(sd_bus_message* m, void* userdata, sd_bus_error* err);
    static int onNameOwnerChanged(sd_bus_message* m, void* userdata, sd_bus_error* err);

    SystemBus& bus_;
    BluezClient client_;
    FacadePort port_;
    BluetoothOperations ops_;
    sd_bus_slot* propsSlot_ = nullptr;
    sd_bus_slot* ifacesSlot_ = nullptr;
    sd_bus_slot* ownerSlot_ = nullptr;  // BlueZ service (re)appearance
    std::string adapter_;
    // Refetch serialization: one GetManagedObjects in flight at a time; a
    // signal during a fetch only marks pendingFetch_ (interleaved replies used
    // to regress the snapshot to stale state).
    bool fetchInFlight_ = false;
    bool pendingFetch_ = false;
    bool subscribed_ = false;
    BluetoothSnapshot snap_;
    std::function<void()> onChange_;
    // Every result path calls this instead of onChange_ directly, so ready()
    // flips true exactly when the first real snapshot is published.
    void notifyReady() {
        ready_ = true;
        if (onChange_) onChange_();
    }
    bool ready_ = false;
    // False on the lock screen: never register an Agent1, so BlueZ has no one to
    // prompt and this process cannot be made to render pairing UI (QL-1).
    bool agentEnabled_ = true;
    // Exported org.bluez.Agent1. Owned here because pairing prompts belong to
    // the same snapshot the picker already reads, and because BlueZ coming and
    // going has to re-register it alongside everything else.
    //
    // Declared last so it is destroyed *first*: its teardown observes the state
    // above, which must therefore still be alive.
    BluetoothAgent agent_;
};

}  // namespace qypr
