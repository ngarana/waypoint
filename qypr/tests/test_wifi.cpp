// test_wifi.cpp - Unit tests for the pure WiFi snapshot reducer.

#include "test_framework.hpp"

#include <algorithm>

#include "system/WifiOperations.hpp"
#include "system/WifiSnapshotReducer.hpp"

TEST(WifiReducerBuildsSnapshot) {
    // No device: everything unavailable.
    qypr::WifiSnapshot s = qypr::reducePublished(qypr::WifiFetchState{});
    EXPECT_FALSE(s.available);
    EXPECT_FALSE(s.enabled);
    EXPECT_FALSE(s.connected);
    EXPECT_TRUE(s.networks.empty());

    // Radio off with a device: available but disabled, list cleared.
    qypr::WifiFetchState off;
    off.haveDevice = true;
    off.wirelessEnabled = false;
    off.networks.push_back(qypr::WifiAp{.ssid = "x", .strength = 70});
    s = qypr::reducePublished(off);
    EXPECT_TRUE(s.available);
    EXPECT_FALSE(s.enabled);
    EXPECT_FALSE(s.connected);
    EXPECT_TRUE(s.networks.empty());
    EXPECT_FALSE(s.scanning);

    // Activated device with an AP: connected with identity.
    qypr::WifiFetchState up;
    up.haveDevice = true;
    up.wirelessEnabled = true;
    up.scanning = true;
    up.deviceState = 100;
    up.apSsid = "Home";
    up.apStrength = 72;
    s = qypr::reducePublished(up);
    EXPECT_TRUE(s.connected);
    EXPECT_EQ(s.ssid, std::string("Home"));
    EXPECT_EQ(s.strength, 72);
    EXPECT_TRUE(s.scanning);

    // Non-activated state with an AP: connected stays false, no identity.
    up.deviceState = 30;
    s = qypr::reducePublished(up);
    EXPECT_FALSE(s.connected);
    EXPECT_TRUE(s.ssid.empty());
    EXPECT_EQ(s.strength, 0);
}

TEST(WifiReducerMergesApReadings) {
    std::vector<qypr::WifiAp> aps;
    aps = qypr::mergeApReading(std::move(aps), qypr::WifiApReading{.ssid = "A", .strength = 40});
    aps = qypr::mergeApReading(std::move(aps),
                               qypr::WifiApReading{.ssid = "B", .strength = 60, .secured = true});
    EXPECT_EQ(static_cast<int>(aps.size()), 2);

    // Duplicate SSID strengthens (max) and sticky-ORs flags.
    aps = qypr::mergeApReading(std::move(aps),
                               qypr::WifiApReading{.ssid = "A", .strength = 55, .active = true});
    EXPECT_EQ(static_cast<int>(aps.size()), 2);
    auto it = std::ranges::find_if(aps, [](const qypr::WifiAp& e) { return e.ssid == "A"; });
    EXPECT_TRUE(it != aps.end());
    EXPECT_EQ(it->strength, 55);
    EXPECT_TRUE(it->active);
    EXPECT_FALSE(it->secured);

    // A weaker duplicate never regresses the strength.
    aps = qypr::mergeApReading(std::move(aps), qypr::WifiApReading{.ssid = "A", .strength = 10});
    EXPECT_EQ(it->strength, 55);

    // An empty SSID is ignored.
    aps = qypr::mergeApReading(std::move(aps), qypr::WifiApReading{.ssid = "", .strength = 90});
    EXPECT_EQ(static_cast<int>(aps.size()), 2);
}

TEST(WifiReducerSavedFlagsAndSort) {
    std::vector<qypr::WifiAp> aps = {{.ssid = "Cafe", .strength = 50},
                                     {.ssid = "Home", .strength = 70, .active = true},
                                     {.ssid = "Work", .strength = 70}};
    qypr::applySavedFlags(aps, {{"Home", "/c/1"}, {"Ghost", "/c/9"}});
    auto home = std::ranges::find_if(aps, [](const qypr::WifiAp& e) { return e.ssid == "Home"; });
    auto cafe = std::ranges::find_if(aps, [](const qypr::WifiAp& e) { return e.ssid == "Cafe"; });
    EXPECT_TRUE(home->saved);
    EXPECT_FALSE(cafe->saved);

    qypr::sortForPicker(aps);
    // Active first, then strength-descending, stable for equal strengths.
    EXPECT_EQ(aps.at(0).ssid, std::string("Home"));
    EXPECT_EQ(aps.at(1).ssid, std::string("Work"));
    EXPECT_EQ(aps.at(2).ssid, std::string("Cafe"));
}

TEST(WifiReducerRadioOffClears) {
    qypr::WifiSnapshot s;
    s.available = true;
    s.enabled = true;
    s.connected = true;
    s.ssid = "Home";
    s.strength = 70;
    s.scanning = true;
    s.networks.push_back(qypr::WifiAp{.ssid = "Home"});

    s = qypr::withRadioState(std::move(s), false);
    EXPECT_FALSE(s.enabled);
    EXPECT_FALSE(s.connected);
    EXPECT_TRUE(s.ssid.empty());
    EXPECT_EQ(s.strength, 0);
    EXPECT_FALSE(s.scanning);
    EXPECT_TRUE(s.networks.empty());
    EXPECT_TRUE(s.available);  // the device is still there

    s = qypr::withRadioState(std::move(s), true);
    EXPECT_TRUE(s.enabled);
    EXPECT_FALSE(s.connected);  // on-toggle only sets the flag
}

namespace {

struct FakePort : qypr::WifiCommandPort {
    bool up = true;
    std::string device = "/dev/1";
    std::vector<std::pair<std::string, std::string>> saved;
    std::vector<std::string> calls;

    [[nodiscard]] bool available() const override { return up; }
    [[nodiscard]] std::string devicePath() const override { return device; }
    std::vector<std::pair<std::string, std::string>> savedConnections() override {
        calls.emplace_back("savedConnections");
        return saved;
    }
    void setWirelessEnabled(bool on) override {
        calls.emplace_back(on ? "setWirelessEnabled:on" : "setWirelessEnabled:off");
    }
    void requestScan(const std::string& dev) override { calls.push_back("requestScan:" + dev); }
    void activateConnection(const std::string& conn, const std::string& dev) override {
        calls.push_back("activate:" + conn + "@" + dev);
    }
    void addAndActivate(const std::string& dev, const std::string& ssid,
                        const std::string* psk) override {
        calls.push_back("addAndActivate:" + ssid + "@" + dev + ((psk != nullptr) ? "+psk" : ""));
    }
    void deleteConnection(const std::string& conn) override { calls.push_back("delete:" + conn); }
    void disconnectDevice(const std::string& dev) override { calls.push_back("disconnect:" + dev); }
};

}  // namespace

TEST(WifiOperationsJoinSavedAndOpen) {
    FakePort port;
    port.saved = {{"Home", "/c/1"}};
    qypr::WifiOperations ops(port);
    ops.setSavedCache(port.saved);

    // Saved AP: Activate only, no add, no bus walk.
    ops.connectAp(qypr::WifiAp{.ssid = "Home", .strength = 70, .saved = true});
    EXPECT_EQ(static_cast<int>(port.calls.size()), 1);
    EXPECT_EQ(port.calls.at(0), std::string("activate:/c/1@/dev/1"));

    // Unsaved open AP: AddAndActivate with a null PSK.
    port.calls.clear();
    ops.connectAp(qypr::WifiAp{.ssid = "Cafe", .strength = 50});
    EXPECT_EQ(static_cast<int>(port.calls.size()), 1);
    EXPECT_EQ(port.calls.at(0), std::string("addAndActivate:Cafe@/dev/1"));

    // Secured unsaved network goes through connectPsk with the password.
    port.calls.clear();
    ops.connectPsk("Work", "secret");
    EXPECT_EQ(port.calls.at(0), std::string("addAndActivate:Work@/dev/1+psk"));

    // Empty PSK, empty SSID, and active APs are no-ops.
    port.calls.clear();
    ops.connectPsk("Work", "");
    ops.connectPsk("", "secret");
    ops.connectAp(qypr::WifiAp{.ssid = "Home", .active = true});
    EXPECT_TRUE(port.calls.empty());

    // No device: no calls at all.
    port.device.clear();
    ops.connectAp(qypr::WifiAp{.ssid = "Cafe"});
    ops.disconnect();
    EXPECT_TRUE(port.calls.empty());
}

TEST(WifiOperationsForgetAndDisconnect) {
    FakePort port;
    port.saved = {{"Home", "/c/1"}};
    qypr::WifiOperations ops(port);
    ops.setSavedCache(port.saved);

    // Unknown SSID: no Delete, no refresh.
    qypr::WifiOperations::Result r = ops.forgetSsid("Ghost");
    EXPECT_FALSE(r.refreshNetworks);
    EXPECT_TRUE(port.calls.empty());

    // Known SSID: Delete, then the backend must refresh the picker.
    r = ops.forgetSsid("Home");
    EXPECT_TRUE(r.refreshNetworks);
    EXPECT_EQ(port.calls.at(0), std::string("delete:/c/1"));

    // Disconnect without a device makes no call.
    port.device.clear();
    ops.disconnect();
    EXPECT_EQ(static_cast<int>(port.calls.size()), 1);
    port.device = "/dev/1";
    ops.disconnect();
    EXPECT_EQ(port.calls.at(1), std::string("disconnect:/dev/1"));
}

TEST(WifiOperationsColdSavedFallback) {
    FakePort port;
    port.saved = {{"Home", "/c/1"}};
    qypr::WifiOperations ops(port);

    // Cold cache consults the bus once.
    EXPECT_FALSE(ops.savedCacheWarm());
    ops.connectSsid("Home");
    EXPECT_EQ(static_cast<int>(port.calls.size()), 2);
    EXPECT_EQ(port.calls.at(0), std::string("savedConnections"));
    EXPECT_EQ(port.calls.at(1), std::string("activate:/c/1@/dev/1"));

    // A warm cache that lacks the SSID returns empty without a bus walk.
    port.calls.clear();
    ops.setSavedCache({{"Other", "/c/2"}});
    EXPECT_TRUE(ops.savedCacheWarm());
    ops.connectSsid("Home");
    EXPECT_TRUE(port.calls.empty());
}

TEST(WifiOperationsRadioAndScan) {
    FakePort port;
    qypr::WifiOperations ops(port);

    EXPECT_TRUE(ops.setEnabled(true));
    EXPECT_EQ(port.calls.at(0), std::string("setWirelessEnabled:on"));
    EXPECT_TRUE(ops.requestScan());
    EXPECT_EQ(port.calls.at(1), std::string("requestScan:/dev/1"));

    // No bus: nothing is sent and the caller changes nothing.
    port.up = false;
    EXPECT_FALSE(ops.setEnabled(false));
    EXPECT_FALSE(ops.requestScan());
    EXPECT_EQ(static_cast<int>(port.calls.size()), 2);
}
