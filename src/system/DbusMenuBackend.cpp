// DbusMenuBackend.cpp - com.canonical.dbusmenu client implementation.
#include "system/DbusMenuBackend.hpp"

#include <systemd/sd-bus.h>

#include <cstring>

#include "system/SystemBus.hpp"

namespace qypr {

namespace {
constexpr const char* kMenuIface = "com.canonical.dbusmenu";

// GTK labels carry a mnemonic marker: a single '_' before the accelerator char,
// and "__" for a literal underscore. Strip to plain display text.
std::string stripMnemonic(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '_') {
            if (i + 1 < in.size() && in[i + 1] == '_') {  // "__" → literal "_"
                out += '_';
                ++i;
            }
            // otherwise: a mnemonic marker, drop it
        } else {
            out += in[i];
        }
    }
    return out;
}

// Read one property variant into the node. The dict-entry's key is already
// consumed; this consumes the variant.
void readProp(sd_bus_message* m, const char* key, MenuNode& n) {
    if (std::strcmp(key, "label") == 0) {
        const char* s = nullptr;
        if (sd_bus_message_enter_container(m, 'v', "s") >= 0) {
            if (sd_bus_message_read_basic(m, 's', &s) >= 0 && s) n.label = stripMnemonic(s);
            sd_bus_message_exit_container(m);
        } else {
            sd_bus_message_skip(m, "v");
        }
    } else if (std::strcmp(key, "enabled") == 0 || std::strcmp(key, "visible") == 0) {
        int b = 1;
        if (sd_bus_message_enter_container(m, 'v', "b") >= 0) {
            sd_bus_message_read_basic(m, 'b', &b);
            sd_bus_message_exit_container(m);
        } else {
            sd_bus_message_skip(m, "v");
        }
        (key[0] == 'e' ? n.enabled : n.visible) = b != 0;
    } else if (std::strcmp(key, "type") == 0) {
        const char* s = nullptr;
        if (sd_bus_message_enter_container(m, 'v', "s") >= 0) {
            if (sd_bus_message_read_basic(m, 's', &s) >= 0 && s)
                n.separator = std::strcmp(s, "separator") == 0;
            sd_bus_message_exit_container(m);
        } else {
            sd_bus_message_skip(m, "v");
        }
    } else if (std::strcmp(key, "toggle-type") == 0) {
        const char* s = nullptr;
        if (sd_bus_message_enter_container(m, 'v', "s") >= 0) {
            if (sd_bus_message_read_basic(m, 's', &s) >= 0 && s) n.toggleType = s;
            sd_bus_message_exit_container(m);
        } else {
            sd_bus_message_skip(m, "v");
        }
    } else if (std::strcmp(key, "toggle-state") == 0) {
        int32_t v = -1;
        if (sd_bus_message_enter_container(m, 'v', "i") >= 0) {
            sd_bus_message_read_basic(m, 'i', &v);
            sd_bus_message_exit_container(m);
        } else {
            sd_bus_message_skip(m, "v");
        }
        n.toggleState = v;
    } else if (std::strcmp(key, "children-display") == 0) {
        const char* s = nullptr;
        if (sd_bus_message_enter_container(m, 'v', "s") >= 0) {
            if (sd_bus_message_read_basic(m, 's', &s) >= 0 && s)
                n.hasSubmenu = std::strcmp(s, "submenu") == 0;
            sd_bus_message_exit_container(m);
        } else {
            sd_bus_message_skip(m, "v");
        }
    } else {
        sd_bus_message_skip(m, "v");  // icon-data, accessible-desc, shortcut, …
    }
}

// Parse one (ia{sv}av) item, recursing into children.
MenuNode parseItem(sd_bus_message* m) {
    MenuNode n;
    if (sd_bus_message_enter_container(m, 'r', "ia{sv}av") <= 0) return n;

    sd_bus_message_read_basic(m, 'i', &n.id);

    if (sd_bus_message_enter_container(m, 'a', "{sv}") >= 0) {
        while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
            const char* key = nullptr;
            if (sd_bus_message_read_basic(m, 's', &key) >= 0 && key) {
                readProp(m, key, n);
            } else {
                sd_bus_message_skip(m, "v");
            }
            sd_bus_message_exit_container(m);  // dict entry
        }
        sd_bus_message_exit_container(m);  // a{sv}
    }

    if (sd_bus_message_enter_container(m, 'a', "v") >= 0) {
        while (sd_bus_message_enter_container(m, 'v', "(ia{sv}av)") > 0) {
            n.children.push_back(parseItem(m));
            sd_bus_message_exit_container(m);  // variant
        }
        sd_bus_message_exit_container(m);  // av
    }

    sd_bus_message_exit_container(m);  // struct
    return n;
}
}  // namespace

std::vector<MenuNode> DbusMenuBackend::fetch(const std::string& service,
                                             const std::string& menuPath, int32_t parentId) {
    std::vector<MenuNode> out;
    sd_bus* bus = bus_.get();
    if (!bus || service.empty() || menuPath.empty()) return out;

    // Nudge the app to (re)populate this level before we read it. Best-effort.
    {
        sd_bus_error e = SD_BUS_ERROR_NULL;
        sd_bus_message* r = nullptr;
        sd_bus_call_method(bus, service.c_str(), menuPath.c_str(), kMenuIface, "AboutToShow", &e,
                           &r, "i", parentId);
        sd_bus_error_free(&e);
        if (r) sd_bus_message_unref(r);
    }

    // GetLayout(parentId, depth=1, propertyNames=[]) → u(ia{sv}av).
    sd_bus_message* req = nullptr;
    if (sd_bus_message_new_method_call(bus, &req, service.c_str(), menuPath.c_str(), kMenuIface,
                                       "GetLayout") < 0)
        return out;
    sd_bus_message_append(req, "ii", parentId, 1);
    sd_bus_message_open_container(req, 'a', "s");  // empty property filter
    sd_bus_message_close_container(req);

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    int r = sd_bus_call(bus, req, 0, &err, &reply);
    sd_bus_message_unref(req);
    sd_bus_error_free(&err);
    if (r < 0 || !reply) {
        if (reply) sd_bus_message_unref(reply);
        return out;
    }

    uint32_t revision = 0;
    sd_bus_message_read_basic(reply, 'u', &revision);
    MenuNode root = parseItem(reply);
    sd_bus_message_unref(reply);
    return std::move(root.children);
}

void DbusMenuBackend::clicked(const std::string& service, const std::string& menuPath, int32_t id) {
    sd_bus* bus = bus_.get();
    if (!bus || service.empty() || menuPath.empty()) return;
    // Event(id, "clicked", data=variant(int 0), timestamp=0). The data is
    // ignored for "clicked"; an empty int variant is the conventional filler.
    sd_bus_call_method_async(bus, nullptr, service.c_str(), menuPath.c_str(), kMenuIface, "Event",
                             nullptr, nullptr, "isvu", id, "clicked", "i", 0, uint32_t{0});
}

}  // namespace qypr
