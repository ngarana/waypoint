// WifiOperations.cpp - See the header for the design.

#include "system/WifiOperations.hpp"

namespace qypr {

WifiOperations::WifiOperations(WifiCommandPort& port) : port_(port) {}

void WifiOperations::setSavedCache(std::vector<std::pair<std::string, std::string>> pairs) {
    savedCache_ = std::move(pairs);
    savedCacheWarm_ = true;
}

bool WifiOperations::savedCacheWarm() const {
    return savedCacheWarm_;
}

std::string WifiOperations::findSavedConnection(const std::string& ssid) {
    // Cache first (warm after any network-list chain); sync bus walk only as a
    // cold fallback.
    for (const auto& [s, conn] : savedCache_) {
        if (s == ssid) { return conn; }
    }
    if (savedCacheWarm_) { return {}; }
    if (!port_.available()) { return {}; }
    for (auto& p : port_.savedConnections()) {
        if (p.first == ssid) { return p.second; }
    }
    return {};
}

WifiOperations::Result WifiOperations::connectAp(const WifiAp& ap) {
    if (ap.active || ap.ssid.empty()) { return {}; }
    if (ap.saved) { return connectSsid(ap.ssid); }
    // Open network: join with just the SSID.
    if (port_.devicePath().empty()) { return {}; }
    port_.addAndActivate(port_.devicePath(), ap.ssid, nullptr);
    return {};
}

WifiOperations::Result WifiOperations::connectPsk(const std::string& ssid, const std::string& psk) {
    if (ssid.empty() || psk.empty()) { return {}; }
    if (port_.devicePath().empty()) { return {}; }
    port_.addAndActivate(port_.devicePath(), ssid, &psk);
    return {};
}

WifiOperations::Result WifiOperations::connectSsid(const std::string& ssid) {
    if (ssid.empty() || port_.devicePath().empty()) { return {}; }
    const std::string conn = findSavedConnection(ssid);
    if (conn.empty()) { return {}; }
    port_.activateConnection(conn, port_.devicePath());
    return {};
}

WifiOperations::Result WifiOperations::forgetSsid(const std::string& ssid) {
    const std::string conn = findSavedConnection(ssid);
    if (conn.empty()) { return {}; }
    port_.deleteConnection(conn);
    return {.refreshNetworks = true};
}

void WifiOperations::disconnect() {
    if (port_.devicePath().empty()) { return; }
    port_.disconnectDevice(port_.devicePath());
}

bool WifiOperations::setEnabled(bool on) {
    if (!port_.available()) { return false; }
    port_.setWirelessEnabled(on);
    return true;
}

bool WifiOperations::requestScan() {
    const std::string device = port_.devicePath();
    if (!port_.available() || device.empty()) { return false; }
    port_.requestScan(device);
    return true;
}

}  // namespace qypr
