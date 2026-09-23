// NetworkManagerClient.hpp - NetworkManager D-Bus calls and reply walkers.
//
// Owns every sd-bus use of the Wi-Fi stack: protocol constants, property
// walkers, settings parsing, async call issuance, and fire-and-forget user
// commands. Chain *state* stays in WifiBackend; this class is stateless apart
// from the shared SystemBus reference, so the walkers are static and every
// async issuer takes the facade's callback and userdata straight through.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct sd_bus_message;
struct sd_bus_slot;
#include <systemd/sd-bus.h>  // sd_bus_error is a typedef here, not a struct

namespace qypr {

class SystemBus;

class NetworkManagerClient {
public:
    explicit NetworkManagerClient(SystemBus& bus);

    NetworkManagerClient(const NetworkManagerClient&) = delete;
    NetworkManagerClient& operator=(const NetworkManagerClient&) = delete;

    // Protocol constants (service, paths, interfaces, well-known values).
    static constexpr const char* kNM = "org.freedesktop.NetworkManager";
    static constexpr const char* kNMPath = "/org/freedesktop/NetworkManager";
    static constexpr const char* kDeviceIface = "org.freedesktop.NetworkManager.Device";
    static constexpr const char* kWirelessIface = "org.freedesktop.NetworkManager.Device.Wireless";
    static constexpr const char* kApIface = "org.freedesktop.NetworkManager.AccessPoint";
    static constexpr const char* kPropsIface = "org.freedesktop.DBus.Properties";
    static constexpr const char* kSettingsPath = "/org/freedesktop/NetworkManager/Settings";
    static constexpr const char* kSettingsIface = "org.freedesktop.NetworkManager.Settings";
    static constexpr const char* kSettingsConnIface =
        "org.freedesktop.NetworkManager.Settings.Connection";
    static constexpr uint32_t kWifiDeviceType = 2;  // NM_DEVICE_TYPE_WIFI
    static constexpr uint32_t kApFlagPrivacy = 0x1;

    bool available() const;

    // Async issuers: thin wrappers over sd_bus_call_method_async that keep the
    // facade's callback shape, so chain replies keep landing on the facade.
    using AsyncReply = int (*)(sd_bus_message*, void*, sd_bus_error*);
    void getDevices(AsyncReply cb, void* userdata);
    void getAll(const std::string& path, const char* iface, AsyncReply cb, void* userdata);
    void listConnections(AsyncReply cb, void* userdata);
    void getConnectionSettings(const std::string& conn, AsyncReply cb, void* userdata);
    void getAccessPointProps(const std::string& ap, AsyncReply cb, void* userdata);

    // Reply walkers: parse a Get property reply (v{type} container). False
    // when the variant holds another type (nothing consumed past the skip).
    static bool extractBool(sd_bus_message* m, bool* out);
    static bool extractObjPath(sd_bus_message* m, std::string* out);
    static bool extractObjPathArray(sd_bus_message* m, std::vector<std::string>* out);
    static bool extractByteArray(sd_bus_message* m, std::string* out);
    static bool extractByte(sd_bus_message* m, uint8_t* out);
    static bool extractU32(sd_bus_message* m, uint32_t* out);

    // Parse a GetSettings reply (a{sa{sv}}) and return the wireless SSID.
    static std::string parseConnectionSsid(sd_bus_message* reply);

    // Synchronous saved-profile walk (ListConnections + per-connection
    // GetSettings); ssid → connection path. Used as the cold fallback when no
    // network-list chain has warmed the cache yet.
    std::vector<std::pair<std::string, std::string>> savedConnections();

    // Fire-and-forget user commands (sd_bus_call_async, no reply tracking).
    void setWirelessEnabled(bool on);
    void requestScan(const std::string& device);
    void activateConnection(const std::string& conn, const std::string& device);
    void addAndActivate(const std::string& device, const std::string& ssid, const std::string* psk);
    void deleteConnection(const std::string& conn);
    void disconnectDevice(const std::string& device);

private:
    std::string connectionSsid(const std::string& conn);

    SystemBus& bus_;
};

}  // namespace qypr
