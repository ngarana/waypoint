// WifiOperations.hpp - WiFi user commands behind a narrow port.
//
// Pure logic over WifiCommandPort: saved/open join selection, the saved-cache
// with its cold bus fallback, forget, disconnect, radio toggle, and scan.
// The NetworkManagerClient implements the port in production; tests use a
// fake that records calls, so command selection and ordering assert without
// a bus. The facade owns chain state and snapshot publication; operations
// never touch them.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "system/WifiModel.hpp"

namespace qypr {

class WifiCommandPort {
public:
    virtual ~WifiCommandPort() = default;
    virtual bool available() const = 0;
    virtual std::string devicePath() const = 0;
    virtual std::vector<std::pair<std::string, std::string>> savedConnections() = 0;
    virtual void setWirelessEnabled(bool on) = 0;
    virtual void requestScan(const std::string& device) = 0;
    virtual void activateConnection(const std::string& conn, const std::string& device) = 0;
    virtual void addAndActivate(const std::string& device, const std::string& ssid,
                                const std::string* psk) = 0;
    virtual void deleteConnection(const std::string& conn) = 0;
    virtual void disconnectDevice(const std::string& device) = 0;
};

class WifiOperations {
public:
    struct Result {
        bool refreshNetworks = false;  // backend re-runs the network chain
    };
    explicit WifiOperations(WifiCommandPort& port);

    // Saved-profile cache, fed by the facade after each network chain lands.
    // Empty-but-never-set reads as cold (consult the bus once); set (even to
    // empty) reads as warm (never walk).
    void setSavedCache(std::vector<std::pair<std::string, std::string>> pairs);
    bool savedCacheWarm() const;

    // Join a network: saved → ActivateConnection; unsaved open →
    // AddAndActivateConnection with just the SSID. Active or empty: no-op.
    Result connectAp(const WifiAp& ap);
    // Join a secured network with a passphrase; empty SSID or PSK: no-op.
    Result connectPsk(const std::string& ssid, const std::string& psk);
    // Join by SSID through the saved cache (cold sync walk as fallback).
    Result connectSsid(const std::string& ssid);
    // Delete the saved profile; unknown SSID: no-op. Known: the backend must
    // re-walk the saved set and refresh the picker.
    Result forgetSsid(const std::string& ssid);
    void disconnect();
    // Radio toggle (bus call only; the snapshot mutation stays in the facade
    // via the reducer). False when no bus: the caller changes nothing.
    bool setEnabled(bool on);
    // Scan request (bus call only; spinner/publication stay in the facade).
    // True when a scan was started.
    bool requestScan();

private:
    std::string findSavedConnection(const std::string& ssid);

    WifiCommandPort& port_;
    std::vector<std::pair<std::string, std::string>> savedCache_;
    bool savedCacheWarm_ = false;
};

}  // namespace qypr
