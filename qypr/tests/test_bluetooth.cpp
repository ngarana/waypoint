// test_bluetooth.cpp - Unit tests for the pure Bluetooth snapshot reducer.

#include "test_framework.hpp"

#include <functional>
#include <utility>

#include "system/BluetoothOperations.hpp"
#include "system/BluetoothSnapshotReducer.hpp"

TEST(BluetoothReducerBuildsSnapshot) {
    qypr::BluetoothManagedObjects m;
    m.ok = true;
    m.adapters.push_back(qypr::BluetoothAdapterReading{.path = "/a/0", .powered = true});
    m.devices.push_back(qypr::BluetoothDeviceReading{
        .path = "/d/1", .alias = "Phone", .name = "Phone-xyz", .connected = true, .paired = true});
    m.devices.push_back(qypr::BluetoothDeviceReading{.path = "/d/2", .name = "Headset"});

    qypr::ManagedObjectsResult r = qypr::reduceManagedObjects(m);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.snapshot.available);
    EXPECT_TRUE(r.snapshot.powered);
    EXPECT_EQ(r.snapshot.connectedCount, 1);
    EXPECT_EQ(r.snapshot.firstDevice, std::string("Phone"));
    EXPECT_EQ(static_cast<int>(r.snapshot.devices.size()), 2);
    EXPECT_EQ(r.adapter, std::string("/a/0"));

    // No adapter: unavailable, no adapter path.
    qypr::ManagedObjectsResult none = qypr::reduceManagedObjects(qypr::BluetoothManagedObjects{});
    EXPECT_FALSE(none.ok);
    EXPECT_FALSE(none.snapshot.available);
    EXPECT_TRUE(none.adapter.empty());

    qypr::BluetoothManagedObjects noAdapter;
    noAdapter.ok = true;
    noAdapter.devices.push_back(qypr::BluetoothDeviceReading{.path = "/d/9"});
    qypr::ManagedObjectsResult orphan = qypr::reduceManagedObjects(noAdapter);
    EXPECT_TRUE(orphan.ok);
    EXPECT_FALSE(orphan.snapshot.available);
    EXPECT_TRUE(orphan.adapter.empty());
    EXPECT_EQ(static_cast<int>(orphan.snapshot.devices.size()), 1);
}

TEST(BluetoothReducerAliasBeatsName) {
    auto reduceName = [](const std::string& alias, const std::string& name) {
        qypr::BluetoothManagedObjects m;
        m.ok = true;
        m.adapters.push_back(qypr::BluetoothAdapterReading{.path = "/a/0"});
        m.devices.push_back(
            qypr::BluetoothDeviceReading{.path = "/d/1", .alias = alias, .name = name});
        return qypr::reduceManagedObjects(m).snapshot.devices.at(0).name;
    };
    // Alias wins regardless of which key BlueZ serialized first.
    EXPECT_EQ(reduceName("Phone", "Phone-xyz"), std::string("Phone"));
    EXPECT_EQ(reduceName("", "Headset"), std::string("Headset"));
    EXPECT_EQ(reduceName("", ""), std::string(""));
}

TEST(BluetoothReducerPreservesTransientState) {
    qypr::BluetoothSnapshot prev;
    prev.available = true;
    prev.busy = "/d/1";
    prev.error = "Connect failed: timeout";
    prev.pairing.kind = qypr::BtPairRequest::Kind::Confirm;

    qypr::BluetoothSnapshot next;
    next.available = true;
    next = qypr::preserveTransient(std::move(next), prev);
    EXPECT_EQ(next.busy, std::string("/d/1"));
    EXPECT_EQ(next.error, std::string("Connect failed: timeout"));
    EXPECT_TRUE(next.pairing.active());
}

TEST(BluetoothReducerPowerOffClears) {
    qypr::BluetoothSnapshot s;
    s.available = true;
    s.powered = true;
    s.discovering = true;
    s.connectedCount = 2;
    s.firstDevice = "Phone";
    s.devices.push_back(qypr::BtDevice{.path = "/d/1", .name = "Phone", .connected = true});

    s = qypr::withPowered(std::move(s), false);
    EXPECT_FALSE(s.powered);
    EXPECT_EQ(s.connectedCount, 0);
    EXPECT_TRUE(s.firstDevice.empty());
    EXPECT_FALSE(s.discovering);
    EXPECT_FALSE(s.devices.at(0).connected);

    s = qypr::withPowered(std::move(s), true);
    EXPECT_TRUE(s.powered);
    EXPECT_EQ(s.connectedCount, 0);  // on-toggle only sets the flag
}

TEST(BluetoothReducerOpLifecycle) {
    qypr::BluetoothSnapshot s;
    s.available = true;

    s = qypr::withOpBusy(std::move(s), "/d/3");
    EXPECT_EQ(s.busy, std::string("/d/3"));
    EXPECT_TRUE(s.error.empty());

    s = qypr::withOpEnded(std::move(s), "Pairing failed: rejected");
    EXPECT_TRUE(s.busy.empty());
    EXPECT_EQ(s.error, std::string("Pairing failed: rejected"));

    EXPECT_EQ(qypr::deviceNameFor(s, "/missing"), std::string(""));
    s.devices.push_back(qypr::BtDevice{.path = "/d/3", .name = "Keyboard"});
    EXPECT_EQ(qypr::deviceNameFor(s, "/d/3"), std::string("Keyboard"));

    // A call that never leaves clears the busy row but keeps the error text.
    s.busy = "/d/3";
    s.error = "old";
    s = qypr::withOpIdle(std::move(s));
    EXPECT_TRUE(s.busy.empty());
    EXPECT_EQ(s.error, std::string("old"));
}

namespace {

struct FakeBluez : qypr::BluezCommandPort {
    using Reply = qypr::BluezCommandPort::Reply;
    bool up = true;
    std::string adapter = "/a/0";
    std::vector<std::string> calls;
    // Pending async replies, driven synchronously by the test.
    Reply lastReply;
    std::string lastCall;

    [[nodiscard]] bool available() const override { return up; }
    [[nodiscard]] std::string adapterPath() const override { return adapter; }
    bool setPowered(bool on, Reply onReply) override {
        calls.emplace_back(on ? "setPowered:on" : "setPowered:off");
        lastReply = std::move(onReply);
        lastCall = "setPowered";
        return true;
    }
    bool callDevice(const std::string& path, const char* member, uint64_t /*timeoutUs*/,
                    Reply onReply) override {
        calls.push_back(std::string("device:") + member + ":" + path);
        lastReply = std::move(onReply);
        lastCall = member;
        return true;
    }
    void callAdapter(const char* member, Reply onReply) override {
        calls.push_back(std::string("adapter:") + member);
        lastReply = std::move(onReply);
        lastCall = member;
    }
    bool removeDevice(const std::string& path, Reply onReply) override {
        calls.push_back("remove:" + path);
        lastReply = std::move(onReply);
        lastCall = "remove";
        return true;
    }
    void setTrusted(const std::string& path) override { calls.push_back("trust:" + path); }

    void reply(bool ok, const std::string& error = "") {
        if (lastCall.empty()) { throw std::runtime_error("reply with no pending call"); }
        Reply cb = lastReply;
        lastReply = nullptr;
        lastCall.clear();
        cb(ok, error);
    }
};

struct OpsHarness {
    FakeBluez port;
    qypr::BluetoothSnapshot snap;
    int publishes = 0;
    int refetches = 0;
    qypr::BluetoothOperations ops;

    OpsHarness()
        : ops(
              port,
              [this](qypr::BluetoothSnapshot&& s) {
                  snap = std::move(s);
                  publishes++;
              },
              [this]() -> const qypr::BluetoothSnapshot& { return snap; },
              [this]() { refetches++; }) {}
};

}  // namespace

TEST(BluetoothOperationsPairSequence) {
    OpsHarness h;
    h.snap.available = true;
    h.ops.pairDevice("/d/7");
    EXPECT_EQ(h.port.calls.at(0), std::string("device:Pair:/d/7"));
    EXPECT_EQ(h.snap.busy, std::string("/d/7"));

    // Pair ok: Trust, then Connect, row still busy.
    h.port.reply(true);
    EXPECT_EQ(h.port.calls.at(1), std::string("trust:/d/7"));
    EXPECT_EQ(h.port.calls.at(2), std::string("device:Connect:/d/7"));
    EXPECT_EQ(h.snap.busy, std::string("/d/7"));

    // Connect ok: row clears, snapshot converges.
    h.port.reply(true);
    EXPECT_TRUE(h.snap.busy.empty());
    EXPECT_EQ(h.refetches, 1);

    // Failed Pair: no Trust, no Connect, error published, refetch requested.
    h.port.calls.clear();
    h.ops.pairDevice("/d/8");
    h.port.reply(false, "rejected");
    EXPECT_EQ(static_cast<int>(h.port.calls.size()), 1);
    EXPECT_TRUE(h.snap.busy.empty());
    EXPECT_EQ(h.snap.error, std::string("pairing failed: rejected"));
    EXPECT_EQ(h.refetches, 2);
}

TEST(BluetoothOperationsPowerDefer) {
    OpsHarness h;
    h.snap.available = true;

    // No adapter: no Set, refetch requested, toggle pending.
    h.port.adapter.clear();
    qypr::BluetoothOperations::Result r = h.ops.setPowered(true);
    EXPECT_TRUE(r.refetch);
    EXPECT_TRUE(h.port.calls.empty());

    // The next fetch produced an adapter: the pending toggle applies.
    h.port.adapter = "/a/0";
    EXPECT_TRUE(h.ops.applyPendingPower());
    EXPECT_EQ(h.port.calls.at(0), std::string("setPowered:on"));
    EXPECT_TRUE(h.snap.powered);
    EXPECT_FALSE(h.ops.applyPendingPower());  // consumed

    // Adapter known: Set immediately.
    h.port.calls.clear();
    r = h.ops.setPowered(false);
    EXPECT_FALSE(r.refetch);
    EXPECT_EQ(h.port.calls.at(0), std::string("setPowered:off"));
    EXPECT_FALSE(h.snap.powered);
}

TEST(BluetoothOperationsForgetUsesAdapter) {
    OpsHarness h;
    h.snap.available = true;

    h.ops.forgetDevice("/d/4");
    EXPECT_EQ(h.port.calls.at(0), std::string("remove:/d/4"));
    EXPECT_EQ(h.snap.busy, std::string("/d/4"));
    h.port.reply(true);
    EXPECT_TRUE(h.snap.busy.empty());

    // Without an adapter: no call, no error recorded.
    h.port.adapter.clear();
    h.port.calls.clear();
    h.ops.forgetDevice("/d/5");
    EXPECT_TRUE(h.port.calls.empty());
    EXPECT_TRUE(h.snap.error.empty());
}

TEST(BluetoothOperationsDiscoveryLifetime) {
    OpsHarness h;
    h.snap.available = true;
    h.snap.powered = true;

    // Start twice issues one Start.
    EXPECT_TRUE(h.ops.startDiscovery());
    EXPECT_FALSE(h.ops.startDiscovery());
    EXPECT_EQ(static_cast<int>(h.port.calls.size()), 1);
    EXPECT_TRUE(h.ops.discoveryCallInFlight());

    // A refusal clears the intent (no retry) and records the error.
    h.port.reply(false, "refused");
    EXPECT_TRUE(h.snap.error == std::string("Scan failed: refused"));
    EXPECT_FALSE(h.ops.discoveryCallInFlight());
    EXPECT_FALSE(h.ops.tryStartDiscovery());
    EXPECT_EQ(static_cast<int>(h.port.calls.size()), 1);

    // Stop while not discovering issues nothing.
    h.snap.discovering = false;
    h.ops.stopDiscovery();
    EXPECT_EQ(static_cast<int>(h.port.calls.size()), 1);

    // Stop while discovering issues Stop.
    h.snap.discovering = true;
    h.ops.stopDiscovery();
    EXPECT_EQ(h.port.calls.at(1), std::string("adapter:StopDiscovery"));
}
