// BluezClient.cpp - See the header for the design.

#include "system/BluezClient.hpp"

#include <systemd/sd-bus.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "system/SystemBus.hpp"

namespace qypr {

BluezClient::BluezClient(SystemBus& bus) : bus_(bus) {}

bool BluezClient::available() const {
    return bus_.available();
}

namespace {

// Variant readers. enter_container returns 1 when it entered, 0 when the
// contents do not match the requested type (nothing consumed) and <0 on error
// — so anything but 1 must fall through to skip("v"). Treating 0 as success
// reads past the variant and then exits a container that was never entered,
// which desyncs the *rest of the property dictionary*: every key after the
// mistyped one is silently lost. That exact mistake emptied the WiFi picker.
bool extractBool(sd_bus_message* m, bool* out) {
    if (sd_bus_message_enter_container(m, 'v', "b") <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    int v = 0;
    const int r = sd_bus_message_read_basic(m, 'b', &v);
    sd_bus_message_exit_container(m);
    if (r < 0) { return false; }
    *out = v != 0;
    return true;
}

bool extractString(sd_bus_message* m, std::string* out) {
    if (sd_bus_message_enter_container(m, 'v', "s") <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    const char* s = nullptr;
    const int r = sd_bus_message_read_basic(m, 's', static_cast<void*>(&s));
    sd_bus_message_exit_container(m);
    if (r < 0 || (s == nullptr)) { return false; }
    *out = s;
    return true;
}

bool extractByte(sd_bus_message* m, uint8_t* out) {
    if (sd_bus_message_enter_container(m, 'v', "y") <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    const int r = sd_bus_message_read_basic(m, 'y', out);
    sd_bus_message_exit_container(m);
    return r >= 0;
}

// Walk one interface's a{sv} property dictionary, dispatching each key to `fn`
// and skipping the variant of anything it does not claim. Entering and exiting
// the dictionary in one place is what keeps the walkers below symmetric.
template <typename Fn>
void walkProps(sd_bus_message* m, const Fn& fn) {
    if (sd_bus_message_enter_container(m, 'a', "{sv}") <= 0) {
        sd_bus_message_skip(m, "a{sv}");
        return;
    }
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char* key = nullptr;
        sd_bus_message_read(m, "s", &key);
        if (key == nullptr || !fn(key)) { sd_bus_message_skip(m, "v"); }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
}

void parseAdapterReading(sd_bus_message* m, BluetoothAdapterReading* reading) {
    walkProps(m, [&](const char* key) {
        if (std::strcmp(key, "Powered") == 0) { return extractBool(m, &reading->powered); }
        if (std::strcmp(key, "Discovering") == 0) { return extractBool(m, &reading->discovering); }
        return false;
    });
}

void parseDeviceReading(sd_bus_message* m, BluetoothDeviceReading* reading) {
    // Alias is the user-facing name and wins; Name is the fallback the adapter
    // reports. Collected separately so the outcome does not depend on the order
    // BlueZ happens to serialize the keys in.
    walkProps(m, [&](const char* key) {
        if (std::strcmp(key, "Connected") == 0) { return extractBool(m, &reading->connected); }
        if (std::strcmp(key, "Paired") == 0) { return extractBool(m, &reading->paired); }
        if (std::strcmp(key, "Alias") == 0) { return extractString(m, &reading->alias); }
        if (std::strcmp(key, "Name") == 0) { return extractString(m, &reading->name); }
        if (std::strcmp(key, "Icon") == 0) { return extractString(m, &reading->icon); }
        return false;
    });
}

void parseBatteryReading(sd_bus_message* m, BluetoothDeviceReading* reading) {
    walkProps(m, [&](const char* key) {
        if (std::strcmp(key, "Percentage") == 0) {
            uint8_t pct = 0;
            if (!extractByte(m, &pct)) { return false; }
            reading->battery = pct;
            return true;
        }
        return false;
    });
}

}  // namespace

int BluezClient::onReplyThunk(sd_bus_message* reply, void* userdata, sd_bus_error* /*unused*/) {
    auto* thunk = static_cast<ReplyThunk*>(userdata);
    std::string error;
    bool ok = true;
    if (sd_bus_message_is_method_error(reply, nullptr) != 0) {
        ok = false;
        const sd_bus_error* e = sd_bus_message_get_error(reply);
        const char* msg = (e != nullptr && e->message != nullptr) ? e->message : "failed";
        error = msg;
    }
    thunk->cb(ok, error);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // sd-bus userdata; freed on reply
    delete thunk;
    return 0;
}

bool BluezClient::getManagedObjects(AsyncReply cb, void* userdata) {
    const int r = sd_bus_call_method_async(bus_.get(), nullptr, kBlueZ, "/", kObjectManagerIface,
                                           "GetManagedObjects", cb, userdata, "");
    if (r < 0) {
        std::fprintf(stderr, "qypr: failed to enqueue GetManagedObjects: %d\n", -r);
        return false;
    }
    return true;
}

bool BluezClient::parseManagedObjects(sd_bus_message* m, BluetoothManagedObjects* out) {
    out->adapters.clear();
    out->devices.clear();
    if (sd_bus_message_enter_container(m, 'a', "{oa{sa{sv}}}") <= 0) { return false; }

    while (sd_bus_message_enter_container(m, 'e', "oa{sa{sv}}") > 0) {
        const char* path = nullptr;
        sd_bus_message_read(m, "o", &path);

        BluetoothDeviceReading device;
        if (path != nullptr) { device.path = path; }
        bool isDeviceObj = false;

        if (sd_bus_message_enter_container(m, 'a', "{sa{sv}}") > 0) {
            while (sd_bus_message_enter_container(m, 'e', "sa{sv}") > 0) {
                const char* iface = nullptr;
                sd_bus_message_read(m, "s", &iface);
                // One object carries several interfaces; Device1 and Battery1
                // both contribute to the same reading.
                const bool isAdapter = (iface != nullptr) && std::strcmp(iface, kAdapterIface) == 0;
                const bool isDevice = (iface != nullptr) && std::strcmp(iface, kDeviceIface) == 0;
                const bool isBattery = (iface != nullptr) && std::strcmp(iface, kBatteryIface) == 0;
                if (isAdapter) {
                    BluetoothAdapterReading adapter;
                    if (path != nullptr) { adapter.path = path; }
                    parseAdapterReading(m, &adapter);
                    out->adapters.push_back(std::move(adapter));
                } else if (isDevice) {
                    isDeviceObj = true;
                    parseDeviceReading(m, &device);
                } else if (isBattery) {
                    parseBatteryReading(m, &device);
                } else {
                    sd_bus_message_skip(m, "a{sv}");
                }
                sd_bus_message_exit_container(m);
            }
            sd_bus_message_exit_container(m);
        }

        if (isDeviceObj) { out->devices.push_back(std::move(device)); }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return true;
}

bool BluezClient::dictHasKey(sd_bus_message* m, std::initializer_list<const char*> keys) {
    bool found = false;
    walkProps(m, [&](const char* key) {
        for (const char* k : keys) {
            if (std::strcmp(key, k) == 0) { found = true; }
        }
        return false;  // never claims a value: the variant is always skipped
    });
    return found;
}

bool BluezClient::setAdapterPowered(const std::string& adapter, bool on, ReplyCb cb) {
    sd_bus* bus = bus_.get();
    sd_bus_message* msg = nullptr;
    if (sd_bus_message_new_method_call(bus, &msg, kBlueZ, adapter.c_str(), kPropsIface, "Set") <
        0) {
        return false;
    }
    sd_bus_message_append(msg, "ss", kAdapterIface, "Powered");
    sd_bus_message_open_container(msg, 'v', "b");
    sd_bus_message_append(msg, "b", on ? 1 : 0);
    sd_bus_message_close_container(msg);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // sd-bus userdata; freed on reply
    auto* thunk = new ReplyThunk{std::move(cb)};
    const int r = sd_bus_call_async(bus, nullptr, msg, &onReplyThunk, thunk, 0);
    sd_bus_message_unref(msg);
    if (r < 0) {
        std::fprintf(stderr, "qypr: failed to send BlueZ setPowered(%s): %d\n", on ? "on" : "off",
                     -r);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // sd-bus userdata; freed on reply
        delete thunk;
        return false;
    }
    return true;
}

bool BluezClient::callDevice(const std::string& path, const char* member, uint64_t timeoutUs,
                             ReplyCb cb) {
    sd_bus* bus = bus_.get();
    // Built by hand rather than sd_bus_call_method_async so this call can carry
    // its own timeout instead of the connection default.
    sd_bus_message* msg = nullptr;
    int r = sd_bus_message_new_method_call(bus, &msg, kBlueZ, path.c_str(), kDeviceIface, member);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // sd-bus userdata; freed on reply
    auto* thunk = new ReplyThunk{std::move(cb)};
    if (r >= 0) { r = sd_bus_call_async(bus, nullptr, msg, &onReplyThunk, thunk, timeoutUs); }
    sd_bus_message_unref(msg);
    if (r < 0) {
        std::fprintf(stderr, "qypr: failed to enqueue BlueZ %s: %d\n", member, -r);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // sd-bus userdata; freed on reply
        delete thunk;
        return false;
    }
    return true;
}

bool BluezClient::removeDevice(const std::string& adapter, const std::string& path, ReplyCb cb) {
    sd_bus* bus = bus_.get();
    sd_bus_message* msg = nullptr;
    int r = sd_bus_message_new_method_call(bus, &msg, kBlueZ, adapter.c_str(), kAdapterIface,
                                           "RemoveDevice");
    if (r >= 0) { r = sd_bus_message_append(msg, "o", path.c_str()); }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // sd-bus userdata; freed on reply
    auto* thunk = new ReplyThunk{std::move(cb)};
    if (r >= 0) {
        r = sd_bus_call_async(bus, nullptr, msg, &onReplyThunk, thunk, kRemoveTimeoutUs);
    }
    sd_bus_message_unref(msg);
    if (r < 0) {
        std::fprintf(stderr, "qypr: failed to enqueue BlueZ RemoveDevice: %d\n", -r);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // sd-bus userdata; freed on reply
        delete thunk;
        return false;
    }
    return true;
}

void BluezClient::callAdapter(const std::string& adapter, const char* member, ReplyCb cb) {
    sd_bus* bus = bus_.get();
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // sd-bus userdata; freed on reply
    auto* thunk = new ReplyThunk{std::move(cb)};
    const int r = sd_bus_call_method_async(bus, nullptr, kBlueZ, adapter.c_str(), kAdapterIface,
                                           member, &onReplyThunk, thunk, "");
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) // sd-bus userdata; freed on reply
    if (r < 0) { delete thunk; }
}

void BluezClient::setTrusted(const std::string& path) {
    sd_bus* bus = bus_.get();
    sd_bus_message* msg = nullptr;
    if (sd_bus_message_new_method_call(bus, &msg, kBlueZ, path.c_str(), kPropsIface, "Set") < 0) {
        return;
    }
    sd_bus_message_append(msg, "ss", kDeviceIface, "Trusted");
    sd_bus_message_open_container(msg, 'v', "b");
    sd_bus_message_append(msg, "b", 1);
    sd_bus_message_close_container(msg);
    sd_bus_call_async(bus, nullptr, msg, nullptr, nullptr, 0);
    sd_bus_message_unref(msg);
}

}  // namespace qypr
