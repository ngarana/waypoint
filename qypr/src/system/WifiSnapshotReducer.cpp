// WifiSnapshotReducer.cpp - See the header for the design.

#include "system/WifiSnapshotReducer.hpp"

#include <algorithm>

namespace qypr {

namespace {

constexpr uint32_t kDeviceStateActivated = 100;  // NM_DEVICE_STATE_ACTIVATED

}  // namespace

WifiSnapshot reducePublished(const WifiFetchState& s) {
    WifiSnapshot next;
    next.available = s.haveDevice;
    next.enabled = s.wirelessEnabled && next.available;
    next.scanning = s.scanning;
    next.networks = s.networks;
    if (!next.enabled) {
        next.networks.clear();  // radio off: the picker shows the off-state
        next.scanning = false;
    }
    if (next.available) {
        // Connection truth is the device state (100 = activated), not the mere
        // presence of an access point.
        next.connected = s.deviceState == kDeviceStateActivated;
        if (next.connected && !s.apSsid.empty()) {
            next.ssid = s.apSsid;
            next.strength = s.apStrength;
        }
    }
    return next;
}

WifiSnapshot withRadioState(WifiSnapshot s, bool on) {
    s.enabled = on;
    if (!on) {
        s.connected = false;
        s.ssid.clear();
        s.strength = 0;
        s.scanning = false;
        s.networks.clear();
    }
    return s;
}

std::vector<WifiAp> mergeApReading(std::vector<WifiAp> current, const WifiApReading& r) {
    if (r.ssid.empty()) { return current; }
    auto it = std::ranges::find_if(current, [&](const WifiAp& e) { return e.ssid == r.ssid; });
    if (it != current.end()) {
        it->strength = std::max(it->strength, r.strength);
        it->active = it->active || r.active;
        it->secured = it->secured || r.secured;
    } else {
        current.push_back(
            {.ssid = r.ssid, .strength = r.strength, .secured = r.secured, .active = r.active});
    }
    return current;
}

void applySavedFlags(std::vector<WifiAp>& aps,
                     const std::vector<std::pair<std::string, std::string>>& saved) {
    for (const auto& [ssid, conn] : saved) {
        (void)conn;
        auto it = std::ranges::find_if(aps, [&](const WifiAp& e) { return e.ssid == ssid; });
        if (it != aps.end()) { it->saved = true; }
    }
}

void sortForPicker(std::vector<WifiAp>& aps) {
    std::ranges::stable_sort(aps, [](const WifiAp& a, const WifiAp& b) {
        if (a.active != b.active) { return a.active > b.active; }
        return a.strength > b.strength;
    });
}

}  // namespace qypr
