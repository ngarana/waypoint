// BluetoothSnapshotReducer.cpp - See the header for the design.

#include "system/BluetoothSnapshotReducer.hpp"

namespace qypr {

ManagedObjectsResult reduceManagedObjects(const BluetoothManagedObjects& m) {
    ManagedObjectsResult result;
    if (!m.ok) { return result; }
    result.ok = true;
    BluetoothSnapshot& out = result.snapshot;
    for (const auto& adapter : m.adapters) {
        out.powered = adapter.powered;
        out.discovering = adapter.discovering;
        out.available = true;
        if (result.adapter.empty() && !adapter.path.empty()) { result.adapter = adapter.path; }
    }
    for (const auto& reading : m.devices) {
        BtDevice dev;
        dev.path = reading.path;
        dev.connected = reading.connected;
        dev.paired = reading.paired;
        dev.icon = reading.icon;
        dev.battery = reading.battery;
        if (!reading.alias.empty()) {
            dev.name = reading.alias;
        } else if (!reading.name.empty() && dev.name.empty()) {
            dev.name = reading.name;
        }
        if (dev.connected) {
            ++out.connectedCount;
            if (out.firstDevice.empty()) { out.firstDevice = dev.name; }
        }
        out.devices.push_back(std::move(dev));
    }
    return result;
}

BluetoothSnapshot preserveTransient(BluetoothSnapshot next, const BluetoothSnapshot& prev) {
    next.busy = prev.busy;
    next.error = prev.error;
    next.pairing = prev.pairing;
    return next;
}

BluetoothSnapshot withPowered(BluetoothSnapshot s, bool on) {
    s.powered = on;
    if (!on) {
        s.connectedCount = 0;
        s.firstDevice.clear();
        s.discovering = false;  // the adapter drops discovery with the radio
        for (BtDevice& d : s.devices) { d.connected = false; }
    }
    return s;
}

BluetoothSnapshot withOpBusy(BluetoothSnapshot s, const std::string& path) {
    s.busy = path;
    s.error.clear();
    return s;
}

BluetoothSnapshot withOpEnded(BluetoothSnapshot s, const std::string& error) {
    s.busy.clear();
    s.error = error;
    return s;
}

BluetoothSnapshot withOpIdle(BluetoothSnapshot s) {
    s.busy.clear();
    return s;
}

BluetoothSnapshot withPairingRequest(BluetoothSnapshot s, const BtPairRequest& r) {
    s.pairing = r;
    return s;
}

std::string deviceNameFor(const BluetoothSnapshot& s, const std::string& path) {
    for (const BtDevice& d : s.devices) {
        if (d.path == path) { return d.name; }
    }
    return {};
}

}  // namespace qypr
