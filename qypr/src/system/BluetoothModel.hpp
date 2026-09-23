// BluetoothModel.hpp - Bluetooth data model (bus-free).
//
// BtDevice/BluetoothSnapshot/BtPairRequest moved verbatim (BtPairRequest from
// BluetoothAgent.hpp, which now includes this header, so its users are
// unchanged). Readings are one decoded GetManagedObjects reply, as produced
// by BluezClient and reduced by BluetoothSnapshotReducer.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qypr {

struct BtDevice {
    std::string path;
    std::string name;
    std::string icon;
    bool connected = false;
    bool paired = false;
    int battery = -1;

    bool operator==(const BtDevice&) const = default;
};

// A pending user decision. Mirrors the Agent1 method that raised it; the picker
// renders one prompt card per kind.
struct BtPairRequest {
    enum class Kind : uint8_t {
        None,
        // RequestConfirmation: both ends show the same 6 digits; the user says
        // whether they match. The common flow for phones and modern headsets.
        Confirm,
        // RequestAuthorization: "Just Works" pairing that still wants a yes.
        Authorize,
        // DisplayPasskey / DisplayPinCode: we show a code for the user to type
        // on the *remote* device. Nothing to accept — only cancel.
        Display,
        // RequestPasskey (6-digit) / RequestPinCode (string): the user types
        // the code shown on the remote device.
        Entry,
        // AuthorizeService: a paired-but-untrusted device wants a profile.
        Service,
    };

    Kind kind = Kind::None;
    std::string devicePath;
    std::string deviceName;
    std::string passkey;  // Confirm/Display: the formatted 6-digit code
    std::string service;  // Service: the requested profile UUID
    // Entry: RequestPasskey wants a number ("u"), RequestPinCode a string.
    bool numericEntry = false;
    // Display: how many digits the remote has typed so far, as BlueZ reports
    // progress through repeated DisplayPasskey calls.
    uint16_t entered = 0;

    [[nodiscard]] bool active() const { return kind != Kind::None; }

    bool operator==(const BtPairRequest&) const = default;
};

struct BluetoothSnapshot {
    bool available = false;
    bool powered = false;
    bool discovering = false;  // a scan is running (spinner in the picker)
    int connectedCount = 0;
    std::string firstDevice;
    std::vector<BtDevice> devices;  // every known device, paired or not
    // Transient operation state, owned by the op callbacks rather than by
    // BlueZ's object tree: the device path of an operation in flight, and the
    // last failure. Both drive picker feedback ("Pairing…", an error line) and
    // are carried across refetches.
    std::string busy;
    std::string error;
    // A pairing prompt BlueZ is waiting on. The picker renders it and answers
    // through respondPairing()/respondPairingInput().
    BtPairRequest pairing;

    bool operator==(const BluetoothSnapshot&) const = default;
};

// One decoded Adapter1 property block.
struct BluetoothAdapterReading {
    std::string path;
    bool powered = false;
    bool discovering = false;
};

// One decoded object carrying Device1 and/or Battery1 properties. Alias and
// name arrive separately so the alias-wins rule does not depend on the order
// BlueZ serializes the keys in.
struct BluetoothDeviceReading {
    std::string path;
    std::string alias;
    std::string name;
    bool connected = false;
    bool paired = false;
    std::string icon;
    int battery = -1;
};

// One decoded GetManagedObjects reply: every adapter and device object in walk
// order. `ok` is false on a malformed reply (caller publishes "unavailable").
struct BluetoothManagedObjects {
    bool ok = false;
    std::vector<BluetoothAdapterReading> adapters;
    std::vector<BluetoothDeviceReading> devices;
};

}  // namespace qypr
