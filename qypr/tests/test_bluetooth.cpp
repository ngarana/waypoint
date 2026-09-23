// test_bluetooth.cpp - Unit tests for the pure Bluetooth snapshot reducer.

#include "test_framework.hpp"

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
}
