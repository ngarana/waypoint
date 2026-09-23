// NotificationParser.cpp - See the header for the design.

#include "notifications/NotificationParser.hpp"

#include <systemd/sd-bus.h>

#include <cstring>

#include "core/Types.hpp"

namespace qypr {

namespace {

// Read a variant holding any integer/boolean type (1) or a string type (2)
// into out parameters; consumes the variant.
int readVariant(sd_bus_message* m, uint64_t& numOut, std::string& strOut) {
    const char* contents = nullptr;
    if (sd_bus_message_peek_type(m, nullptr, &contents) < 0 || (contents == nullptr)) {
        sd_bus_message_skip(m, "v");
        return 0;
    }
    if (contents[1] == '\0' && (std::strchr("ybnqiuxt", contents[0]) != nullptr)) {
        sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, contents);
        bool ok = true;
        switch (contents[0]) {
            case 'y': {
                uint8_t v = 0;
                ok = sd_bus_message_read(m, "y", &v) >= 0;
                numOut = v;
                break;
            }
            case 'b': {
                int v = 0;
                ok = sd_bus_message_read(m, "b", &v) >= 0;
                numOut = static_cast<uint64_t>(v != 0);
                break;
            }
            case 'n': {
                int16_t v = 0;
                ok = sd_bus_message_read(m, "n", &v) >= 0;
                numOut = static_cast<uint64_t>(v);
                break;
            }
            case 'q': {
                uint16_t v = 0;
                ok = sd_bus_message_read(m, "q", &v) >= 0;
                numOut = v;
                break;
            }
            case 'i': {
                int32_t v = 0;
                ok = sd_bus_message_read(m, "i", &v) >= 0;
                numOut = static_cast<uint64_t>(v);
                break;
            }
            case 'u': {
                uint32_t v = 0;
                ok = sd_bus_message_read(m, "u", &v) >= 0;
                numOut = v;
                break;
            }
            case 'x': {
                int64_t v = 0;
                ok = sd_bus_message_read(m, "x", &v) >= 0;
                numOut = static_cast<uint64_t>(v);
                break;
            }
            case 't':
                ok = sd_bus_message_read(m, "t", &numOut) >= 0;
                break;
            default:
                break;
        }
        sd_bus_message_exit_container(m);
        return ok ? 1 : 0;
    }
    if (std::strcmp(contents, "s") == 0) {
        sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s");
        const char* val = nullptr;
        bool const ok = sd_bus_message_read(m, "s", &val) >= 0;
        if (ok && (val != nullptr)) { strOut = val; }
        sd_bus_message_exit_container(m);
        return ok ? 2 : 0;
    }
    sd_bus_message_skip(m, "v");
    return 0;
}

}  // namespace

std::optional<NotifyEvent> parseNotify(sd_bus_message* m) {
    NotifyEvent e;
    const char* app = nullptr;
    const char* icon = nullptr;
    const char* summary = nullptr;
    const char* body = nullptr;
    if (sd_bus_message_read(m, "susss", &app, &e.replacesId, &icon, &summary, &body) < 0) {
        return std::nullopt;
    }
    e.app = (app != nullptr) ? app : "";
    e.icon = (icon != nullptr) ? icon : "";
    e.summary = (summary != nullptr) ? summary : "";
    e.body = (body != nullptr) ? body : "";
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s") >= 0) {
        const char* actKey = nullptr;
        while (sd_bus_message_read(m, "s", &actKey) > 0) {
            const char* actLabel = nullptr;
            if (sd_bus_message_read(m, "s", &actLabel) > 0) {
                e.actions.emplace_back((actKey != nullptr) ? actKey : "",
                                       (actLabel != nullptr) ? actLabel : "");
            } else {
                break;
            }
        }
        sd_bus_message_exit_container(m);
    } else {
        if (sd_bus_message_skip(m, "as") < 0) { return std::nullopt; }
    }

    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}") > 0) {
        while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
            const char* key = nullptr;
            if (sd_bus_message_read(m, "s", &key) < 0) { break; }
            uint64_t valNum = 0;
            std::string valStr;
            int const type = readVariant(m, valNum, valStr);
            e.hints = applyHint((key != nullptr) ? key : "", type == 1, valNum, valStr,
                                std::move(e.hints));
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
    }
    e.postedAt = nowMs();
    e.haveCookie = sd_bus_message_get_cookie(m, &e.cookie) >= 0;
    const char* sender = sd_bus_message_get_sender(m);
    e.sender = (sender != nullptr) ? sender : "?";
    return e;
}

std::optional<ClosedEvent> parseClosed(sd_bus_message* m) {
    ClosedEvent e;
    if (sd_bus_message_read(m, "uu", &e.id, &e.reason) < 0 || e.id == 0) { return std::nullopt; }
    return e;
}

std::optional<ReturnEvent> parseReturn(sd_bus_message* m) {
    ReturnEvent e;
    if (sd_bus_message_get_reply_cookie(m, &e.replyCookie) < 0) { return std::nullopt; }
    const char* dest = sd_bus_message_get_destination(m);
    e.destination = (dest != nullptr) ? dest : "?";
    const char* sig = sd_bus_message_get_signature(m, 1);
    if ((sig == nullptr) || std::strcmp(sig, "u") != 0 ||
        sd_bus_message_read(m, "u", &e.daemonId) < 0 || e.daemonId == 0) {
        return std::nullopt;
    }
    return e;
}

}  // namespace qypr
