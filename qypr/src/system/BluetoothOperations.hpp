// BluetoothOperations.hpp - Bluetooth user commands behind a narrow port.
//
// Owns the command-side state machine: deferred power toggles, the
// Pair → Trust → Connect continuation, single-operation picker feedback, and
// discovery lifetime. The BluezClient implements the port in production; tests
// use a fake whose std::function replies drive the continuations
// synchronously, so call selection and ordering assert without a bus.
//
// The facade keeps fetch serialization, subscriptions, the agent, and
// publication; it feeds this object the adapter path through the port and a
// refetch channel for reply-driven convergence.

#pragma once

#include <functional>
#include <string>

#include "system/BluetoothModel.hpp"

namespace qypr {

class BluezCommandPort {
public:
    // Reply outcome for user-command calls: ok, plus the BlueZ error text
    // when not. A call that is never sent never invokes the callback.
    using Reply = std::function<void(bool ok, const std::string& error)>;
    virtual ~BluezCommandPort() = default;
    virtual bool available() const = 0;
    virtual std::string adapterPath() const = 0;
    virtual bool setPowered(bool on, Reply onReply) = 0;
    virtual bool callDevice(const std::string& path, const char* member, uint64_t timeoutUs,
                            Reply onReply) = 0;
    virtual void callAdapter(const char* member, Reply onReply) = 0;
    virtual bool removeDevice(const std::string& path, Reply onReply) = 0;
    virtual void setTrusted(const std::string& path) = 0;
};

class BluetoothOperations {
public:
    using Publish = std::function<void(BluetoothSnapshot&&)>;
    using SnapshotFn = std::function<const BluetoothSnapshot&()>;
    using RefetchFn = std::function<void()>;
    BluetoothOperations(BluezCommandPort& port, Publish publish, SnapshotFn current,
                        RefetchFn refetch);

    struct Result {
        bool refetch = false;  // the backend must re-run the fetch chain
    };

    // Power toggle. Deferred with a refetch request while the adapter path is
    // still unknown; applied (optimistically) once it is known.
    Result setPowered(bool on);
    // Applies a deferred toggle after a fetch produced an adapter. True when
    // a toggle was pending (and is now sent).
    bool applyPendingPower();
    // Drops a deferred toggle (adapter never appeared).
    void dropPendingPower();

    void connectDevice(const std::string& path);
    void disconnectDevice(const std::string& path);
    // Pair → on ok: Trust, then Connect. On failure: error publish + refetch.
    void pairDevice(const std::string& path);
    // RemoveDevice on the *adapter*. Without an adapter: silent no-op.
    void forgetDevice(const std::string& path);

    // Discovery: one call in flight, wantDiscovery survives power-off → on; a
    // refused StartDiscovery clears the intent (never spins).
    bool startDiscovery();
    void stopDiscovery();
    // Issue StartDiscovery if the picker still wants one and the adapter is in
    // a state to accept it. Re-run after every fetch.
    bool tryStartDiscovery();
    bool discoveryCallInFlight() const;

private:
    void beginOp(const std::string& path);
    void endOp(const char* what, bool ok, const std::string& error);
    void onDiscoveryReply(bool ok, const std::string& error);

    BluezCommandPort& port_;
    Publish publish_;
    SnapshotFn current_;
    RefetchFn refetch_;
    // A toggle requested while the adapter path was still unknown: applied once
    // a successful fetch (or BlueZ (re)appearance) has produced an adapter.
    bool pendingPowerSet_ = false;
    bool pendingPowerOn_ = false;
    // The picker asked for a scan and has not closed yet. Held across the
    // power-off → power-on edge; cleared when BlueZ refuses to start one, so a
    // failing adapter cannot spin StartDiscovery forever.
    bool wantDiscovery_ = false;
    bool discoveryCallInFlight_ = false;
};

}  // namespace qypr
