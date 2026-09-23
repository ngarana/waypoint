// BluetoothOperations.cpp - See the header for the design.

#include "system/BluetoothOperations.hpp"

#include <cstdio>

#include "system/BluetoothSnapshotReducer.hpp"
#include "system/BluezClient.hpp"

namespace qypr {

BluetoothOperations::BluetoothOperations(BluezCommandPort& port, Publish publish,
                                         SnapshotFn current, RefetchFn refetch)
    : port_(port),
      publish_(std::move(publish)),
      current_(std::move(current)),
      refetch_(std::move(refetch)) {}

BluetoothOperations::Result BluetoothOperations::setPowered(bool on) {
    if (!port_.available()) { return {}; }
    if (port_.adapterPath().empty()) {
        std::fprintf(stderr,
                     "qypr: Bluetooth adapter unknown; deferring power toggle, refetching\n");
        pendingPowerSet_ = true;
        pendingPowerOn_ = on;
        return {.refetch = true};
    }
    // Optimistic write so the switch answers the click immediately; BlueZ's
    // PropertiesChanged drives the authoritative refetch, and a rejection
    // re-fetches to converge on the true state.
    publish_(withPowered(current_(), on));
    port_.setPowered(on, [this](bool ok, const std::string& error) {
        if (!ok) {
            // The optimistic snapshot no longer matches reality; re-fetch to
            // converge on the true state.
            std::fprintf(stderr, "qypr: BlueZ setPowered failed (%s)\n", error.c_str());
            refetch_();
        }
        // Success: the PropertiesChanged signal will drive the next fetch.
    });
    return {};
}

bool BluetoothOperations::applyPendingPower() {
    if (!pendingPowerSet_) { return false; }
    pendingPowerSet_ = false;
    publish_(withPowered(current_(), pendingPowerOn_));
    const bool on = pendingPowerOn_;
    port_.setPowered(on, [this](bool ok, const std::string& error) {
        if (!ok) {
            std::fprintf(stderr, "qypr: BlueZ setPowered failed (%s)\n", error.c_str());
            refetch_();
        }
    });
    return true;
}

void BluetoothOperations::dropPendingPower() {
    pendingPowerSet_ = false;
}

void BluetoothOperations::beginOp(const std::string& path) {
    publish_(withOpBusy(current_(), path));
}

void BluetoothOperations::endOp(const char* what, bool ok, const std::string& error) {
    if (!ok) {
        std::fprintf(stderr, "qypr: BlueZ %s failed: %s\n", what, error.c_str());
        publish_(withOpEnded(current_(), std::string(what) + " failed: " + error));
    } else {
        publish_(withOpEnded(current_(), {}));
    }
    refetch_();
}

void BluetoothOperations::connectDevice(const std::string& path) {
    if (!port_.available() || path.empty()) { return; }
    beginOp(path);
    if (!port_.callDevice(
            path, "Connect", BluezClient::kConnectTimeoutUs,
            [this](bool ok, const std::string& error) { endOp("device operation", ok, error); })) {
        publish_(withOpEnded(current_(), "Connect could not be sent"));
    }
}

void BluetoothOperations::disconnectDevice(const std::string& path) {
    if (!port_.available() || path.empty()) { return; }
    beginOp(path);
    if (!port_.callDevice(
            path, "Disconnect", BluezClient::kConnectTimeoutUs,
            [this](bool ok, const std::string& error) { endOp("device operation", ok, error); })) {
        publish_(withOpEnded(current_(), "Disconnect could not be sent"));
    }
}

void BluetoothOperations::pairDevice(const std::string& path) {
    if (!port_.available() || path.empty()) { return; }
    beginOp(path);
    // Pair succeeded: trust the device so it may reconnect unattended, then
    // connect it — the same sequence bluetoothctl and the GNOME/Windows
    // panels perform. The row stays busy across the follow-on Connect rather
    // than blinking idle.
    if (!port_.callDevice(path, "Pair", BluezClient::kPairTimeoutUs,
                          [this, path](bool ok, const std::string& error) {
                              if (!ok) {
                                  endOp("pairing", ok, error);
                                  return;
                              }
                              port_.setTrusted(path);
                              port_.callDevice(path, "Connect", BluezClient::kConnectTimeoutUs,
                                               [this](bool connOk, const std::string& connError) {
                                                   endOp("device operation", connOk, connError);
                                               });
                          })) {
        publish_(withOpEnded(current_(), "Pair could not be sent"));
    }
}

void BluetoothOperations::forgetDevice(const std::string& path) {
    if (!port_.available() || path.empty()) { return; }
    if (port_.adapterPath().empty()) {
        // Only reachable in the window between a seeded snapshot and the first
        // fetch. Say so rather than swallowing the click in silence.
        return;
    }
    beginOp(path);
    if (!port_.removeDevice(path, [this](bool ok, const std::string& error) {
            endOp("device operation", ok, error);
        })) {
        publish_(withOpIdle(current_()));
    }
}

bool BluetoothOperations::startDiscovery() {
    wantDiscovery_ = true;
    return tryStartDiscovery();
}

void BluetoothOperations::stopDiscovery() {
    wantDiscovery_ = false;
    if (current_().discovering) {
        port_.callAdapter("StopDiscovery", [this](bool ok, const std::string& error) {
            onDiscoveryReply(ok, error);
        });
    }
}

bool BluetoothOperations::tryStartDiscovery() {
    const BluetoothSnapshot& snap = current_();
    if (!wantDiscovery_ || discoveryCallInFlight_) { return false; }
    if (!snap.powered || snap.discovering || port_.adapterPath().empty()) { return false; }
    discoveryCallInFlight_ = true;
    port_.callAdapter("StartDiscovery",
                      [this](bool ok, const std::string& error) { onDiscoveryReply(ok, error); });
    return true;
}

void BluetoothOperations::onDiscoveryReply(bool ok, const std::string& error) {
    discoveryCallInFlight_ = false;
    if (!ok) {
        // Stop wanting it: retrying on every fetch would spin forever
        // against an adapter that keeps refusing.
        wantDiscovery_ = false;
        std::fprintf(stderr, "qypr: BlueZ discovery call failed: %s\n", error.c_str());
        BluetoothSnapshot next = current_();
        next.error = std::string("Scan failed: ") + error;
        publish_(std::move(next));
    }
    // Either way the adapter's Discovering property is the truth; re-read it.
    refetch_();
}

bool BluetoothOperations::discoveryCallInFlight() const {
    return discoveryCallInFlight_;
}

}  // namespace qypr
