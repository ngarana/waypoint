// test_wifi.cpp - Unit tests for the pure WiFi snapshot reducer.

#include "test_framework.hpp"

#include <algorithm>

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
