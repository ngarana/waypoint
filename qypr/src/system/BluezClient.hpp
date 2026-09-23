// BluezClient.hpp - BlueZ D-Bus calls and reply walkers.
//
// Owns every sd-bus use of the Bluetooth stack: protocol constants, property
// walkers, GetManagedObjects decoding, async call issuance, and the op-call
// timeouts. Chain and lifecycle state stays in BluetoothBackend; user-command
// orchestration lives in BluetoothOperations, which drives the reply handlers
// below through std::function callbacks.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct sd_bus_message;
struct sd_bus_slot;
#include <systemd/sd-bus.h>  // sd_bus_error is a typedef here, not a struct

#include "system/BluetoothModel.hpp"

namespace qypr {

class SystemBus;

class BluezClient {
public:
    explicit BluezClient(SystemBus& bus);

    BluezClient(const BluezClient&) = delete;
    BluezClient& operator=(const BluezClient&) = delete;

    // Protocol constants (service, interfaces).
    static constexpr const char* kBlueZ = "org.bluez";
    static constexpr const char* kAdapterIface = "org.bluez.Adapter1";
    static constexpr const char* kDeviceIface = "org.bluez.Device1";
    static constexpr const char* kBatteryIface = "org.bluez.Battery1";
    static constexpr const char* kPropsIface = "org.freedesktop.DBus.Properties";
    static constexpr const char* kObjectManagerIface = "org.freedesktop.DBus.ObjectManager";

    // Call ceilings. SystemBus caps every call at 5 s, which is right for
    // property reads and wrong for these: pairing waits on a human reading a
    // code off a phone, and a profile connect legitimately takes 30-60 s
    // (bluetoothd's own logs show exactly that against a phone). Aborting
    // early is worse than waiting — BlueZ carries on regardless, so the op
    // "fails", the row still reads unpaired, and the next click starts a
    // *second* pairing that prompts for the passkey all over again.
    static constexpr uint64_t kPairTimeoutUs = 120'000'000;
    static constexpr uint64_t kConnectTimeoutUs = 60'000'000;
    static constexpr uint64_t kRemoveTimeoutUs = 15'000'000;

    // Reply outcome for user-command calls: ok, plus the BlueZ error text
    // when not.
    using ReplyCb = std::function<void(bool ok, const std::string& error)>;

    bool available() const;

    // Async GetManagedObjects issuer; the facade's chain callback keeps the
    // sd-bus function-pointer shape. False when the call was not enqueued.
    using AsyncReply = int (*)(sd_bus_message*, void*, sd_bus_error*);
    bool getManagedObjects(AsyncReply cb, void* userdata);

    // Walk a GetManagedObjects reply (a{oa{sa{sv}}}) into readings. False on
    // a malformed reply; `out` is then meaningless (the caller publishes
    // "unavailable" rather than keeping stale data). Only objects carrying
    // the Device1 interface become device readings; a Battery1-only object
    // contributes nothing — matching the facade this replaces, where such an
    // object's battery died with its discarded row.
    static bool parseManagedObjects(sd_bus_message* m, BluetoothManagedObjects* out);

    // True when the reply's property dictionary carries any of the keys
    // (values always skipped): the signal filter for PropertiesChanged.
    static bool dictHasKey(sd_bus_message* m, std::initializer_list<const char*> keys);

    // User-command calls. The reply arrives on `cb`; false means the call was
    // never sent (the callback is not invoked then).
    bool setAdapterPowered(const std::string& adapter, bool on, ReplyCb cb);
    bool callDevice(const std::string& path, const char* member, uint64_t timeoutUs, ReplyCb cb);
    bool removeDevice(const std::string& adapter, const std::string& path, ReplyCb cb);
    // Fire-and-forget adapter call (discovery start/stop): the reply still
    // arrives on `cb`, but a send failure is silent — mirroring the facade,
    // which never checked it.
    void callAdapter(const std::string& adapter, const char* member, ReplyCb cb);
    // Fire-and-forget Trust write after a successful Pair (no reply tracking).
    void setTrusted(const std::string& path);

private:
    struct ReplyThunk {
        ReplyCb cb;
    };
    static int onReplyThunk(sd_bus_message* reply, void* userdata, sd_bus_error* err);

    SystemBus& bus_;
};

}  // namespace qypr
