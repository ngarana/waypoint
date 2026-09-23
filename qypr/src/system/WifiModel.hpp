// WifiModel.hpp - WiFi data model (bus-free).
//
// WifiAp/WifiSnapshot moved verbatim from WifiBackend.hpp so every existing
// include site (StateCacheCodec, WifiIndicator, QSTileFactory, tests) keeps
// compiling through that header. WifiApReading is one decoded AccessPoint
// GetAll reply, as produced by NetworkManagerClient and merged by
// WifiSnapshotReducer.

#pragma once

#include <string>
#include <vector>

namespace qypr {

struct WifiAp {
    std::string ssid;
    int strength = 0;
    bool secured = false;
    bool active = false;
    bool saved = false;

    bool operator==(const WifiAp&) const = default;
};

struct WifiSnapshot {
    bool available = false;
    bool enabled = false;
    bool connected = false;
    std::string ssid;
    int strength = 0;
    bool scanning = false;         // a scan is in flight (spinner in the UI)
    std::vector<WifiAp> networks;  // visible APs, strongest first (picker list)

    bool operator==(const WifiSnapshot&) const = default;
};

// One decoded AccessPoint GetAll reply (Ssid/Strength/Flags/WpaFlags/RsnFlags
// folded to secured/active by the caller, which knows the active AP path).
struct WifiApReading {
    std::string ssid;
    int strength = 0;
    bool secured = false;
    bool active = false;
};

}  // namespace qypr
