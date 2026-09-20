// SNIBackend.cpp - StatusNotifierItem host + optional watcher (push-driven).
//
// Dual-mode operation:
//   Host mode: connects to an external StatusNotifierWatcher, mirrors its items.
//   Watcher+Host mode: claims the watcher name when none exists, implements the
//   watcher D-Bus interface, and acts as both watcher and host.
#include "system/SNIBackend.hpp"

#include <systemd/sd-bus.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "system/SystemBus.hpp"

namespace qypr {

namespace {
constexpr const char* kWatcher = "org.kde.StatusNotifierWatcher";
constexpr const char* kWatcherPath = "/StatusNotifierWatcher";
constexpr const char* kWatcherIface = "org.kde.StatusNotifierWatcher";
constexpr const char* kItemIface = "org.kde.StatusNotifierItem";
constexpr const char* kPropsIface = "org.freedesktop.DBus.Properties";
constexpr const char* kDBusIface = "org.freedesktop.DBus";
constexpr const char* kDBusName = "org.freedesktop.DBus";
constexpr int kProtocolVersion = 1;

// ─── Variant readers ────────────────────────────────────────────────────────

bool readVariantString(sd_bus_message* m, std::string* out) {
    if (sd_bus_message_enter_container(m, 'v', "s") < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    const char* s = nullptr;
    if (sd_bus_message_read_basic(m, 's', &s) >= 0 && s) *out = s;
    sd_bus_message_exit_container(m);
    return true;
}

bool readVariantObjectPath(sd_bus_message* m, std::string* out) {
    if (sd_bus_message_enter_container(m, 'v', "o") < 0) {
        sd_bus_message_skip(m, "v");
        return false;
    }
    const char* s = nullptr;
    if (sd_bus_message_read_basic(m, 'o', &s) >= 0 && s) *out = s;
    sd_bus_message_exit_container(m);
    return true;
}

cairo_surface_t* pixmapToSurface(const unsigned char* argb, int w, int h) {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return nullptr;
    }
    cairo_surface_flush(surf);
    unsigned char* dst = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(dst + y * stride);
        const unsigned char* src = argb + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            unsigned a = src[0], r = src[1], g = src[2], b = src[3];
            src += 4;
            r = r * a / 255;
            g = g * a / 255;
            b = b * a / 255;
            row[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    cairo_surface_mark_dirty(surf);
    return surf;
}

cairo_surface_t* readVariantPixmap(sd_bus_message* m) {
    if (sd_bus_message_enter_container(m, 'v', "a(iiay)") < 0) {
        sd_bus_message_skip(m, "v");
        return nullptr;
    }
    cairo_surface_t* best = nullptr;
    int bestW = 0;
    if (sd_bus_message_enter_container(m, 'a', "(iiay)") >= 0) {
        while (sd_bus_message_enter_container(m, 'r', "iiay") > 0) {
            int32_t w = 0, h = 0;
            sd_bus_message_read(m, "ii", &w, &h);
            const void* data = nullptr;
            size_t len = 0;
            sd_bus_message_read_array(m, 'y', &data, &len);
            sd_bus_message_exit_container(m);
            if (w > 0 && h > 0 && data && len >= static_cast<size_t>(w) * h * 4 && w > bestW) {
                cairo_surface_t* s = pixmapToSurface(static_cast<const unsigned char*>(data), w, h);
                if (s) {
                    if (best) cairo_surface_destroy(best);
                    best = s;
                    bestW = w;
                }
            }
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return best;
}

// ─── Watcher vtable callbacks ───────────────────────────────────────────────

int watcherHandleRegisterItem(sd_bus_message* m, void* userdata, sd_bus_error*) {
    return static_cast<SNIBackend*>(userdata)->handleRegisterItem(m);
}

int watcherHandleRegisterHost(sd_bus_message* m, void* userdata, sd_bus_error*) {
    return static_cast<SNIBackend*>(userdata)->handleRegisterHost(m);
}

static const sd_bus_vtable kWatcherVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("RegisterStatusNotifierItem", "s", "", watcherHandleRegisterItem,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RegisterStatusNotifierHost", "s", "", watcherHandleRegisterHost,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("RegisteredStatusNotifierItems", "as", SNIBackend::getRegisteredItems, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("IsStatusNotifierHostRegistered", "b", SNIBackend::getHostRegistered, 0, 0),
    SD_BUS_PROPERTY("ProtocolVersion", "i", SNIBackend::getProtocolVersion, 0, 0),
    SD_BUS_SIGNAL("StatusNotifierItemRegistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierItemUnregistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierHostRegistered", "", 0),
    SD_BUS_SIGNAL("StatusNotifierHostUnregistered", "", 0),
    SD_BUS_VTABLE_END,
};

}  // namespace

// ─── Lifecycle ──────────────────────────────────────────────────────────────

SNIBackend::SNIBackend(SystemBus& bus) : bus_(bus) {}

SNIBackend::~SNIBackend() {
    if (regSlot_) sd_bus_slot_unref(regSlot_);
    if (unregSlot_) sd_bus_slot_unref(unregSlot_);
    if (itemSlot_) sd_bus_slot_unref(itemSlot_);
    if (watcherSlot_) sd_bus_slot_unref(watcherSlot_);
    if (watcherVtableSlot_) sd_bus_slot_unref(watcherVtableSlot_);
    for (auto* s : ownerWatchSlots_) sd_bus_slot_unref(s);
    clearItems();
}

// ─── Public API ─────────────────────────────────────────────────────────────

void SNIBackend::parseItemRef(const std::string& ref, std::string& service, std::string& path) {
    auto slash = ref.find('/');
    if (slash == std::string::npos) {
        service = ref;
        path = "/StatusNotifierItem";
    } else {
        service = ref.substr(0, slash);
        path = ref.substr(slash);
    }
}

// ─── Watcher property getters (public static, called by vtable) ─────────────

int SNIBackend::getRegisteredItems(sd_bus*, const char*, const char*, const char*,
                                   sd_bus_message* reply, void* userdata, sd_bus_error*) {
    auto* self = static_cast<SNIBackend*>(userdata);
    sd_bus_message_open_container(reply, 'a', "s");
    for (const auto& s : self->registeredItems_) sd_bus_message_append(reply, "s", s.c_str());
    sd_bus_message_close_container(reply);
    return 1;
}

int SNIBackend::getHostRegistered(sd_bus*, const char*, const char*, const char*,
                                  sd_bus_message* reply, void* userdata, sd_bus_error*) {
    auto* self = static_cast<SNIBackend*>(userdata);
    sd_bus_message_append(reply, "b", !self->registeredHosts_.empty());
    return 1;
}

int SNIBackend::getProtocolVersion(sd_bus*, const char*, const char*, const char*,
                                   sd_bus_message* reply, void*, sd_bus_error*) {
    sd_bus_message_append(reply, "i", kProtocolVersion);
    return 1;
}

bool SNIBackend::start() {
    if (!bus_.available()) return false;

    hostName_ = "org.kde.StatusNotifierHost-" + std::to_string(getpid()) + "-1";

    if (detectExternalWatcher()) {
        registerHost();
        mode_ = Mode::Host;
    } else {
        switchToWatcherMode();
        mode_ = Mode::WatcherHost;
    }

    itemSlot_ = bus_.addMatch("type='signal',interface='org.kde.StatusNotifierItem'",
                              &SNIBackend::onItemChanged, this);

    refresh();
    return true;
}

void SNIBackend::activate(size_t index, int x, int y) {
    if (!bus_.available() || index >= items_.size()) return;
    const SNIItem& it = items_[index];
    sd_bus_call_method_async(bus_.get(), nullptr, it.service.c_str(), it.path.c_str(), kItemIface,
                             "Activate", nullptr, nullptr, "ii", x, y);
}

void SNIBackend::secondaryActivate(size_t index, int x, int y) {
    if (!bus_.available() || index >= items_.size()) return;
    const SNIItem& it = items_[index];
    sd_bus_call_method_async(bus_.get(), nullptr, it.service.c_str(), it.path.c_str(), kItemIface,
                             "SecondaryActivate", nullptr, nullptr, "ii", x, y);
}

void SNIBackend::scroll(size_t index, int dx, int dy) {
    // The SNI spec's Scroll(dx, dy) takes signed deltas; an item with no
    // scroll handler is a no-op on the bus side (we already fire-and-forget,
    // so there's nothing more to do). xkb scanline scrolls arrive with dy
    // representing pixels — that's fine as a raw pass-through.
    if (!bus_.available() || index >= items_.size()) return;
    const SNIItem& it = items_[index];
    sd_bus_call_method_async(bus_.get(), nullptr, it.service.c_str(), it.path.c_str(), kItemIface,
                             "Scroll", nullptr, nullptr, "ii", dx, dy);
}

// ─── Host mode helpers ──────────────────────────────────────────────────────

void SNIBackend::registerHost() {
    sd_bus* bus = bus_.get();
    if (!bus) return;
    sd_bus_request_name(bus, hostName_.c_str(), 0);
    sd_bus_call_method_async(bus, nullptr, kWatcher, kWatcherPath, kWatcherIface,
                             "RegisterStatusNotifierHost", nullptr, nullptr, "s",
                             hostName_.c_str());
}

int SNIBackend::onItemChanged(sd_bus_message* m, void* ud, sd_bus_error*) {
    auto* self = static_cast<SNIBackend*>(ud);
    const char* sender = sd_bus_message_get_sender(m);
    const char* path = sd_bus_message_get_path(m);
    if (!sender || !path) return 0;

    for (auto& it : self->items_) {
        if (it.service == sender && it.path == path) {
            SNIItem fresh = self->fetchItem(it.service, it.path);
            if (it.pixmap) cairo_surface_destroy(it.pixmap);
            it = std::move(fresh);
            self->notify();
            return 0;
        }
    }
    self->refresh();
    return 0;
}

int SNIBackend::onWatcherOwnerChanged(sd_bus_message* m, void* ud, sd_bus_error*) {
    auto* self = static_cast<SNIBackend*>(ud);
    const char *name = nullptr, *oldOwner = nullptr, *newOwner = nullptr;
    sd_bus_message_read(m, "sss", &name, &oldOwner, &newOwner);
    if (newOwner && *newOwner) self->registerHost();
    self->refresh();
    return 0;
}

// ─── Watcher mode ───────────────────────────────────────────────────────────

bool SNIBackend::detectExternalWatcher() {
    sd_bus* bus = bus_.get();
    if (!bus) return false;

    // Call org.freedesktop.DBus.NameHasOwner (not available in all systemd versions).
    // Bounded by the connection-wide timeout set in SystemBus's constructor.
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    int r = sd_bus_call_method(bus, kDBusName, "/", kDBusIface, "NameHasOwner", &err, &reply, "s",
                               kWatcher);
    sd_bus_error_free(&err);
    if (r < 0 || !reply) {
        if (reply) sd_bus_message_unref(reply);
        return false;
    }
    int has = 0;
    sd_bus_message_read_basic(reply, 'b', &has);
    sd_bus_message_unref(reply);
    return has;
}

void SNIBackend::switchToWatcherMode() {
    sd_bus* bus = bus_.get();
    if (!bus) return;

    int r = sd_bus_request_name(bus, kWatcher, 0);
    if (r < 0) {
        std::fprintf(stderr, "sni: cannot own %s (%d) — another watcher running?\n", kWatcher, r);
        registerHost();
        mode_ = Mode::Host;
        return;
    }

    registerWatcherVtable();
    sd_bus_request_name(bus, hostName_.c_str(), 0);
}

void SNIBackend::registerWatcherVtable() {
    sd_bus* bus = bus_.get();
    if (!bus) return;
    sd_bus_add_object_vtable(bus, &watcherVtableSlot_, kWatcherPath, kWatcherIface, kWatcherVtable,
                             this);
}

int SNIBackend::handleRegisterItem(sd_bus_message* m) {
    const char* service = nullptr;
    if (sd_bus_message_read_basic(m, 's', &service) < 0 || !service) return -EINVAL;

    // Some items (e.g. blueman) register with a path instead of a bus name.
    // Per the SNI spec the argument should be a bus name; if it starts with '/'
    // it is a path and we must use the sender's unique bus name as the service.
    std::string ref;
    if (service[0] == '/') {
        const char* sender = sd_bus_message_get_sender(m);
        if (sender && *sender)
            ref = std::string(sender) + service;
        else
            ref = service;
    } else {
        ref = service;
    }

    if (std::find(registeredItems_.begin(), registeredItems_.end(), ref) ==
        registeredItems_.end()) {
        registeredItems_.push_back(ref);
        watchItemOwnership(ref);
        emitItemRegistered(ref);
        refresh();
    }

    sd_bus* bus = bus_.get();
    if (bus) {
        sd_bus_message* reply = nullptr;
        sd_bus_message_new_method_return(m, &reply);
        sd_bus_send(nullptr, reply, nullptr);
        sd_bus_message_unref(reply);
    }
    return 1;
}

int SNIBackend::handleRegisterHost(sd_bus_message* m) {
    const char* service = nullptr;
    if (sd_bus_message_read_basic(m, 's', &service) < 0 || !service) return -EINVAL;

    if (std::find(registeredHosts_.begin(), registeredHosts_.end(), service) ==
        registeredHosts_.end()) {
        registeredHosts_.push_back(service);
    }

    sd_bus* bus = bus_.get();
    if (bus) {
        sd_bus_message* reply = nullptr;
        sd_bus_message_new_method_return(m, &reply);
        sd_bus_send(nullptr, reply, nullptr);
        sd_bus_message_unref(reply);
    }
    return 1;
}

void SNIBackend::emitItemRegistered(const std::string& service) {
    sd_bus* bus = bus_.get();
    if (!bus) return;
    sd_bus_message* sig = nullptr;
    sd_bus_message_new_signal(bus, &sig, kWatcherPath, kWatcherIface,
                              "StatusNotifierItemRegistered");
    sd_bus_message_append(sig, "s", service.c_str());
    sd_bus_send(nullptr, sig, nullptr);
    sd_bus_message_unref(sig);
}

void SNIBackend::emitItemUnregistered(const std::string& service) {
    sd_bus* bus = bus_.get();
    if (!bus) return;
    sd_bus_message* sig = nullptr;
    sd_bus_message_new_signal(bus, &sig, kWatcherPath, kWatcherIface,
                              "StatusNotifierItemUnregistered");
    sd_bus_message_append(sig, "s", service.c_str());
    sd_bus_send(nullptr, sig, nullptr);
    sd_bus_message_unref(sig);
}

void SNIBackend::watchItemOwnership(const std::string& service) {
    sd_bus* bus = bus_.get();
    if (!bus) return;
    std::string match = "type='signal',sender='" + std::string(kDBusIface) + "',interface='" +
                        std::string(kDBusIface) + "',member='NameOwnerChanged',arg0='" + service +
                        "'";
    sd_bus_slot* slot = bus_.addMatch(match.c_str(), &SNIBackend::onItemOwnerChanged, this);
    if (slot) ownerWatchSlots_.push_back(slot);
}

int SNIBackend::onItemOwnerChanged(sd_bus_message* m, void* ud, sd_bus_error*) {
    auto* self = static_cast<SNIBackend*>(ud);
    const char *name = nullptr, *oldOwner = nullptr, *newOwner = nullptr;
    sd_bus_message_read(m, "sss", &name, &oldOwner, &newOwner);

    if (newOwner && !*newOwner && name) {
        std::string svc = name;
        auto it = std::find(self->registeredItems_.begin(), self->registeredItems_.end(), svc);
        if (it != self->registeredItems_.end()) {
            self->registeredItems_.erase(it);
            self->emitItemUnregistered(svc);
            self->refresh();
        }
    }
    return 0;
}

// ─── Refresh (dual-mode) ───────────────────────────────────────────────────

void SNIBackend::refresh() {
    sd_bus* bus = bus_.get();
    if (!bus) {
        clearItems();
        notify();
        return;
    }

    if (mode_ == Mode::WatcherHost) {
        std::vector<SNIItem> next;
        for (const auto& ref : registeredItems_) {
            std::string service, path;
            parseItemRef(ref, service, path);
            next.push_back(fetchItem(service, path));
        }
        clearItems();
        items_ = std::move(next);
        notify();
        return;
    }

    // Host mode: query the external watcher. Bounded by the connection-wide
    // timeout set in SystemBus's constructor.
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    int r = sd_bus_get_property(bus, kWatcher, kWatcherPath, kWatcherIface,
                                "RegisteredStatusNotifierItems", &err, &reply, "as");
    sd_bus_error_free(&err);
    if (r < 0 || !reply) {
        if (reply) sd_bus_message_unref(reply);
        clearItems();
        notify();
        return;
    }

    std::vector<SNIItem> next;
    if (sd_bus_message_enter_container(reply, 'a', "s") >= 0) {
        const char* ref = nullptr;
        while (sd_bus_message_read(reply, "s", &ref) > 0 && ref) {
            std::string service, path;
            parseItemRef(ref, service, path);
            next.push_back(fetchItem(service, path));
        }
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);

    clearItems();
    items_ = std::move(next);
    notify();
}

// ─── Item fetch / clear ─────────────────────────────────────────────────────

SNIItem SNIBackend::fetchItem(const std::string& service, const std::string& path) {
    SNIItem item;
    item.service = service;
    item.path = path;

    sd_bus* bus = bus_.get();
    if (!bus) return item;

    // A tray applet is third-party code that may be wedged or still starting,
    // and this call blocks the event loop — so it leans on the connection-wide
    // timeout set in SystemBus's constructor rather than sd-bus's 25s default.
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    int r = sd_bus_call_method(bus, service.c_str(), path.c_str(), kPropsIface, "GetAll", &err,
                               &reply, "s", kItemIface);
    sd_bus_error_free(&err);
    if (r < 0 || !reply) {
        if (reply) sd_bus_message_unref(reply);
        return item;
    }

    if (sd_bus_message_enter_container(reply, 'a', "{sv}") >= 0) {
        while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
            const char* key = nullptr;
            if (sd_bus_message_read(reply, "s", &key) < 0 || !key) {
                sd_bus_message_skip(reply, "v");
                sd_bus_message_exit_container(reply);
                continue;
            }
            if (std::strcmp(key, "IconName") == 0) {
                readVariantString(reply, &item.iconName);
            } else if (std::strcmp(key, "Title") == 0) {
                readVariantString(reply, &item.title);
            } else if (std::strcmp(key, "Status") == 0) {
                readVariantString(reply, &item.status);
            } else if (std::strcmp(key, "IconPixmap") == 0) {
                item.pixmap = readVariantPixmap(reply);
            } else if (std::strcmp(key, "Menu") == 0) {
                readVariantObjectPath(reply, &item.menuPath);
            } else {
                sd_bus_message_skip(reply, "v");
            }
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);
    return item;
}

void SNIBackend::clearItems() {
    for (auto& it : items_) {
        if (it.pixmap) cairo_surface_destroy(it.pixmap);
    }
    items_.clear();
}

}  // namespace qypr
