// DbusRead.hpp - Readers for D-Bus property maps (a{sv}).
//
// Property replies (GetAll, GetManagedObjects, PropertiesChanged) carry each
// value in a variant whose type the reader knows from the key. Each reader
// here consumes exactly one variant whether or not it has the expected type,
// so a daemon that changes a property's type costs that one value, not the
// rest of the message.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <systemd/sd-bus.h>

namespace qypr::dbus {

// 's' or 'o'.
inline bool readString(sd_bus_message* m, char type, std::string* out) {
    const std::array<char, 2> sig{type, '\0'};
    if (sd_bus_message_enter_container(m, 'v', sig.data()) <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    const char* s = nullptr;
    const int r = sd_bus_message_read_basic(m, type, static_cast<void*>(&s));
    sd_bus_message_exit_container(m);
    if (r < 0 || s == nullptr) { return false; }
    *out = s;
    return true;
}

inline bool readBool(sd_bus_message* m, bool* out) {
    int v = 0;
    if (sd_bus_message_read(m, "v", "b", &v) < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    *out = v != 0;
    return true;
}

inline bool readInt(sd_bus_message* m, int* out) {
    int32_t v = 0;
    if (sd_bus_message_read(m, "v", "i", &v) < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    *out = v;
    return true;
}

inline bool readU64(sd_bus_message* m, uint64_t* out) {
    if (sd_bus_message_read(m, "v", "t", out) < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    return true;
}

inline bool readStrings(sd_bus_message* m, std::vector<std::string>* out) {
    if (sd_bus_message_enter_container(m, 'v', "as") <= 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    out->clear();
    if (sd_bus_message_enter_container(m, 'a', "s") > 0) {
        const char* s = nullptr;
        while (sd_bus_message_read(m, "s", &s) > 0) {
            if (s != nullptr) { out->emplace_back(s); }
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return true;
}

// Walk an a{sv}: `onKey(key)` must consume the value (a reader above, or
// sd_bus_message_skip(m, "v")).
template <typename Fn>
void forEachProperty(sd_bus_message* m, const Fn& onKey) {
    if (sd_bus_message_enter_container(m, 'a', "{sv}") <= 0) { return; }
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char* key = nullptr;
        if (sd_bus_message_read(m, "s", &key) < 0 || key == nullptr) {
            sd_bus_message_exit_container(m);
            break;
        }
        onKey(std::string_view(key));
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
}

}  // namespace qypr::dbus
