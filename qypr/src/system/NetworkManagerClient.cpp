// NetworkManagerClient.cpp - See the header for the design.

#include "system/NetworkManagerClient.hpp"

#include <systemd/sd-bus.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "system/SystemBus.hpp"

namespace qypr {

NetworkManagerClient::NetworkManagerClient(SystemBus& bus) : bus_(bus) {}

bool NetworkManagerClient::available() const {
    return bus_.available();
}

void NetworkManagerClient::getDevices(AsyncReply cb, void* userdata) {
    // NetworkManager 1.58 removed org.freedesktop.DBus.ObjectManager, so
    // enumerate with GetDevices and read the properties per object instead of
    // one GetManagedObjects reply.
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, kNMPath, kNM, "GetDevices", cb, userdata,
                             "");
}

void NetworkManagerClient::getAll(const std::string& path, const char* iface, AsyncReply cb,
                                  void* userdata) {
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, path.c_str(), kPropsIface, "GetAll", cb,
                             userdata, "s", iface);
}

void NetworkManagerClient::listConnections(AsyncReply cb, void* userdata) {
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, kSettingsPath, kSettingsIface,
                             "ListConnections", cb, userdata, "");
}

void NetworkManagerClient::getConnectionSettings(const std::string& conn, AsyncReply cb,
                                                 void* userdata) {
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, conn.c_str(), kSettingsConnIface,
                             "GetSettings", cb, userdata, "");
}

void NetworkManagerClient::getAccessPointProps(const std::string& ap, AsyncReply cb,
                                               void* userdata) {
    getAll(ap, kApIface, cb, userdata);
}

bool NetworkManagerClient::extractBool(sd_bus_message* m, bool* out) {
    if (sd_bus_message_enter_container(m, 'v', "b") < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    int v = 0;
    int const r = sd_bus_message_read_basic(m, 'b', &v);
    sd_bus_message_exit_container(m);
    if (r < 0) { return false; }
    *out = v != 0;
    return true;
}

bool NetworkManagerClient::extractObjPath(sd_bus_message* m, std::string* out) {
    if (sd_bus_message_enter_container(m, 'v', "o") < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    const char* s = nullptr;
    int const r = sd_bus_message_read_basic(m, 'o', static_cast<void*>(&s));
    sd_bus_message_exit_container(m);
    if (r < 0 || (s == nullptr)) { return false; }
    *out = s;
    return true;
}

// Object-path array from a property variant ("ao"). Entering the variant only
// exposes the array itself: its elements need a *second* enter_container.
// Reading "o" straight after the variant enter fails and — because the failed
// read leaves the parser mid-container — desyncs the enclosing a{sv} walk, so
// every property after this one is silently lost too.
bool NetworkManagerClient::extractObjPathArray(sd_bus_message* m, std::vector<std::string>* out) {
    if (sd_bus_message_enter_container(m, 'v', "ao") <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    if (sd_bus_message_enter_container(m, 'a', "o") > 0) {
        const char* p = nullptr;
        while (sd_bus_message_read(m, "o", &p) > 0 && (p != nullptr)) { out->emplace_back(p); }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return true;
}

bool NetworkManagerClient::extractByteArray(sd_bus_message* m, std::string* out) {
    if (sd_bus_message_enter_container(m, 'v', "ay") < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    const void* data = nullptr;
    size_t len = 0;
    int const r = sd_bus_message_read_array(m, 'y', &data, &len);
    sd_bus_message_exit_container(m);
    if (r < 0 || (data == nullptr) || len == 0) { return false; }
    out->assign(static_cast<const char*>(data), len);
    return true;
}

bool NetworkManagerClient::extractByte(sd_bus_message* m, uint8_t* out) {
    if (sd_bus_message_enter_container(m, 'v', "y") < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    int const r = sd_bus_message_read_basic(m, 'y', out);
    sd_bus_message_exit_container(m);
    return r >= 0;
}

bool NetworkManagerClient::extractU32(sd_bus_message* m, uint32_t* out) {
    if (sd_bus_message_enter_container(m, 'v', "u") < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    int const r = sd_bus_message_read_basic(m, 'u', out);
    sd_bus_message_exit_container(m);
    return r >= 0;
}

std::string NetworkManagerClient::parseConnectionSsid(sd_bus_message* r) {
    std::string ssid;
    if (sd_bus_message_enter_container(r, 'a', "{sa{sv}}") >= 0) {
        while (sd_bus_message_enter_container(r, 'e', "sa{sv}") > 0) {
            const char* group = nullptr;
            sd_bus_message_read(r, "s", &group);
            if ((group != nullptr) && std::strcmp(group, "802-11-wireless") == 0 &&
                sd_bus_message_enter_container(r, 'a', "{sv}") >= 0) {
                while (sd_bus_message_enter_container(r, 'e', "sv") > 0) {
                    const char* key = nullptr;
                    sd_bus_message_read(r, "s", &key);
                    if ((key != nullptr) && std::strcmp(key, "ssid") == 0 &&
                        sd_bus_message_enter_container(r, 'v', "ay") >= 0) {
                        const void* data = nullptr;
                        size_t len = 0;
                        if (sd_bus_message_read_array(r, 'y', &data, &len) >= 0 &&
                            (data != nullptr) && len > 0) {
                            ssid.assign(static_cast<const char*>(data), len);
                        }
                        sd_bus_message_exit_container(r);
                    } else {
                        sd_bus_message_skip(r, "v");
                    }
                    sd_bus_message_exit_container(r);
                }
                sd_bus_message_exit_container(r);
            } else {
                sd_bus_message_skip(r, "a{sv}");
            }
            sd_bus_message_exit_container(r);
        }
        sd_bus_message_exit_container(r);
    }
    return ssid;
}

std::string NetworkManagerClient::connectionSsid(const std::string& conn) {
    sd_bus* bus = bus_.get();
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* r = nullptr;
    if (sd_bus_call_method(bus, kNM, conn.c_str(), kSettingsConnIface, "GetSettings", &err, &r,
                           "") < 0 ||
        (r == nullptr)) {
        sd_bus_error_free(&err);
        return "";
    }
    sd_bus_error_free(&err);
    std::string const ssid = parseConnectionSsid(r);
    sd_bus_message_unref(r);
    return ssid;
}

std::vector<std::pair<std::string, std::string>> NetworkManagerClient::savedConnections() {
    std::vector<std::pair<std::string, std::string>> out;
    sd_bus* bus = bus_.get();
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    if (sd_bus_call_method(bus, kNM, kSettingsPath, kSettingsIface, "ListConnections", &err, &reply,
                           "") < 0 ||
        (reply == nullptr)) {
        sd_bus_error_free(&err);
        return out;
    }
    sd_bus_error_free(&err);
    if (sd_bus_message_enter_container(reply, 'a', "o") >= 0) {
        const char* conn = nullptr;
        while (sd_bus_message_read(reply, "o", &conn) > 0 && (conn != nullptr)) {
            std::string const ssid = connectionSsid(conn);
            if (!ssid.empty()) { out.emplace_back(ssid, conn); }
        }
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);
    return out;
}

void NetworkManagerClient::setWirelessEnabled(bool on) {
    sd_bus* bus = bus_.get();
    sd_bus_message* msg = nullptr;
    if (sd_bus_message_new_method_call(bus, &msg, kNM, kNMPath, kPropsIface, "Set") < 0) { return; }
    sd_bus_message_append(msg, "ss", kNM, "WirelessEnabled");
    sd_bus_message_open_container(msg, 'v', "b");
    sd_bus_message_append(msg, "b", on ? 1 : 0);
    sd_bus_message_close_container(msg);
    sd_bus_call_async(bus, nullptr, msg, nullptr, nullptr, 0);
    sd_bus_message_unref(msg);
}

void NetworkManagerClient::requestScan(const std::string& device) {
    sd_bus* bus = bus_.get();
    sd_bus_message* msg = nullptr;
    if (sd_bus_message_new_method_call(bus, &msg, kNM, device.c_str(), kWirelessIface,
                                       "RequestScan") < 0) {
        return;
    }
    sd_bus_message_open_container(msg, 'a', "{sv}");
    sd_bus_message_close_container(msg);
    sd_bus_call_async(bus, nullptr, msg, nullptr, nullptr, 0);
    sd_bus_message_unref(msg);
}

void NetworkManagerClient::activateConnection(const std::string& conn, const std::string& device) {
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, kNMPath, kNM, "ActivateConnection", nullptr,
                             nullptr, "ooo", conn.c_str(), device.c_str(), "/");
}

void NetworkManagerClient::deleteConnection(const std::string& conn) {
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, conn.c_str(), kSettingsConnIface, "Delete",
                             nullptr, nullptr, "");
}

void NetworkManagerClient::disconnectDevice(const std::string& device) {
    sd_bus_call_method_async(bus_.get(), nullptr, kNM, device.c_str(), kDeviceIface, "Disconnect",
                             nullptr, nullptr, "");
}

namespace {

// Append one dict entry "s → variant(ay)" into an open a{sv}.
int appendSvBytes(sd_bus_message* msg, const char* key, const std::string& bytes) {
    int r = sd_bus_message_open_container(msg, SD_BUS_TYPE_DICT_ENTRY_BEGIN, "sv");
    if (r >= 0) { r = sd_bus_message_append(msg, "s", key); }
    if (r >= 0) { r = sd_bus_message_open_container(msg, SD_BUS_TYPE_VARIANT, "ay"); }
    if (r >= 0) { r = sd_bus_message_append_array(msg, 'y', bytes.data(), bytes.size()); }
    if (r >= 0) { r = sd_bus_message_close_container(msg); }  // v
    if (r >= 0) { r = sd_bus_message_close_container(msg); }  // dict entry
    return r;
}

// Append one dict entry "s → variant(s)" into an open a{sv}.
int appendSvString(sd_bus_message* msg, const char* key, const char* value) {
    int r = sd_bus_message_open_container(msg, SD_BUS_TYPE_DICT_ENTRY_BEGIN, "sv");
    if (r >= 0) { r = sd_bus_message_append(msg, "ssv", key, "s", value); }
    if (r >= 0) { r = sd_bus_message_close_container(msg); }
    return r;
}

// Append one "group" entry (group-name → variant(a{sv})) into an open a{sa{sv}}.
int appendGroup(sd_bus_message* msg, const char* group,
                std::initializer_list<std::pair<const char*, const char*>> strings,
                const std::string* ssidBytes) {
    int r = sd_bus_message_open_container(msg, SD_BUS_TYPE_DICT_ENTRY_BEGIN, "sa{sv}");
    if (r >= 0) { r = sd_bus_message_append(msg, "s", group); }
    if (r >= 0) { r = sd_bus_message_open_container(msg, SD_BUS_TYPE_VARIANT, "a{sv}"); }
    if (r >= 0) { r = sd_bus_message_open_container(msg, SD_BUS_TYPE_ARRAY, "{sv}"); }
    if (r >= 0 && ssidBytes != nullptr) { r = appendSvBytes(msg, "ssid", *ssidBytes); }
    for (const auto& kv : strings) {
        if (r < 0) { break; }
        r = appendSvString(msg, kv.first, kv.second);
    }
    if (r >= 0) { r = sd_bus_message_close_container(msg); }  // a{sv}
    if (r >= 0) { r = sd_bus_message_close_container(msg); }  // v
    if (r >= 0) { r = sd_bus_message_close_container(msg); }  // dict entry
    return r;
}

}  // namespace

// AddAndActivateConnection(connection dict, device, specific_object): NM's
// one-shot join for networks with no saved profile. The dictionary carries the
// SSID and — for secured networks — the wpa-psk security block; NM persists
// the profile, so the network shows as saved afterwards.
void NetworkManagerClient::addAndActivate(const std::string& device, const std::string& ssid,
                                          const std::string* psk) {
    sd_bus* bus = bus_.get();
    sd_bus_message* msg = nullptr;
    if (sd_bus_message_new_method_call(bus, &msg, kNM, kNMPath, kNM, "AddAndActivateConnection") <
        0) {
        return;
    }
    int r = sd_bus_message_open_container(msg, SD_BUS_TYPE_ARRAY, "{sa{sv}");
    if (r >= 0) { r = appendGroup(msg, "802-11-wireless", {}, &ssid); }
    if (r >= 0 && psk != nullptr && !psk->empty()) {
        r = appendGroup(msg, "802-11-wireless-security",
                        {{"key-mgmt", "wpa-psk"}, {"psk", psk->c_str()}}, nullptr);
    }
    if (r >= 0) { r = sd_bus_message_close_container(msg); }
    if (r >= 0) { r = sd_bus_message_append(msg, "oo", device.c_str(), "/"); }
    if (r < 0) {
        std::fprintf(stderr, "qypr: failed to build AddAndActivateConnection message\n");
    } else {
        sd_bus_call_async(bus, nullptr, msg, nullptr, nullptr, 0);
    }
    sd_bus_message_unref(msg);
}

}  // namespace qypr
