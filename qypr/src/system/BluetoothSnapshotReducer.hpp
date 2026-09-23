// BluetoothSnapshotReducer.hpp - Pure Bluetooth snapshot logic (bus-free).
//
// Every function here is a verbatim extraction of a BluetoothBackend shaping
// step: no sd-bus types, no I/O, no backend state. The facade walks
// GetManagedObjects into BluetoothManagedObjects, reduces, preserves
// transient op state across the refetch, and publishes.

#pragma once

#include <string>

#include "system/BluetoothModel.hpp"

namespace qypr {

// parseManagedObjects() body, minus the facade's adapter-path side effect
// (returned separately so the reducer stays pure).
struct ManagedObjectsResult {
    bool ok = false;
    BluetoothSnapshot snapshot;
    std::string adapter;  // first adapter path, "" when none
};
ManagedObjectsResult reduceManagedObjects(const BluetoothManagedObjects& m);

// onGetManagedObjects() transient carry: a refetch must not wipe the
// picker's busy row, error line, or pending pairing prompt.
BluetoothSnapshot preserveTransient(BluetoothSnapshot next, const BluetoothSnapshot& prev);

// sendSetPowered() optimistic write: answers the click immediately; BlueZ's
// PropertiesChanged drives the authoritative refetch.
BluetoothSnapshot withPowered(BluetoothSnapshot s, bool on);

// beginOp()/endOp() rows: mark a device busy (clearing any error), then clear
// the row and record the failure text.
BluetoothSnapshot withOpBusy(BluetoothSnapshot s, const std::string& path);
BluetoothSnapshot withOpEnded(BluetoothSnapshot s, const std::string& error);
// Send-failure rows: the call never left, so clear the busy row but keep the
// previous error text and request no refetch.
BluetoothSnapshot withOpIdle(BluetoothSnapshot s);

// Agent prompt arrival: the pairing request joins the snapshot like any
// other change.
BluetoothSnapshot withPairingRequest(BluetoothSnapshot s, const BtPairRequest& r);

// Agent name resolution: the snapshot is the only place with device names.
std::string deviceNameFor(const BluetoothSnapshot& s, const std::string& path);

}  // namespace qypr
