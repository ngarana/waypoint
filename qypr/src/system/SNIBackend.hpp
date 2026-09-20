// SNIBackend.hpp - StatusNotifierItem (system tray) host + optional watcher.
//
// Dual-mode operation:
//   - Host mode: connects to an external StatusNotifierWatcher (e.g. waybar),
//     mirrors its RegisteredStatusNotifierItems list.
//   - Watcher+Host mode: when no external watcher is running, claims the
//     watcher name, implements the watcher interface, and acts as both watcher
//     and host. Tray apps register with us; we track their bus ownership and
//     auto-remove dead items.
//
// Everything is push: the watcher signals item add/remove, each item signals
// its own icon/title/status changes — no polling.
//
// Standard cross-desktop protocol only (org.kde.StatusNotifier* is *the*
// tray protocol on Wayland; it is not a specific daemon's private interface).

#pragma once

#include <cairo/cairo.h>

#include <functional>
#include <string>
#include <vector>

struct sd_bus;
struct sd_bus_message;
struct sd_bus_slot;
struct sd_bus_error;

namespace qypr {

class SystemBus;

struct SNIItem {
    std::string service;   // owning bus name (e.g. ":1.51")
    std::string path;      // item object path (e.g. "/org/blueman/sni")
    std::string iconName;  // themed IconName ("" if the item ships only a pixmap)
    std::string title;     // Title (tooltip text)
    std::string status;    // "Active" | "Passive" | "NeedsAttention"
    std::string menuPath;  // com.canonical.dbusmenu object path ("" if none)

    // Best IconPixmap converted to a premultiplied cairo surface, or nullptr.
    // Owned by the backend (freed in clearItems / the destructor). Used only
    // when iconName does not resolve through the icon theme.
    cairo_surface_t* pixmap = nullptr;
};

class SNIBackend {
public:
    enum class Mode { Host, WatcherHost };

    explicit SNIBackend(SystemBus& bus);
    ~SNIBackend();

    SNIBackend(const SNIBackend&) = delete;
    SNIBackend& operator=(const SNIBackend&) = delete;

    // Register as a StatusNotifierHost (and optionally as the watcher).
    // Returns false only when the session bus itself is unavailable; a running
    // host with no items yet is a success (the indicator stays hidden).
    bool start();

    Mode mode() const { return mode_; }
    const std::vector<SNIItem>& items() const { return items_; }

    // Fires whenever the item list or an item's icon/title/status changes.
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    // Left-click activation. x,y are screen coordinates (the item may use them
    // to position its own menu). Fire-and-forget async call.
    void activate(size_t index, int x, int y);

    // Middle-click: the spec's SecondaryActivate. Fire-and-forget async.
    void secondaryActivate(size_t index, int x, int y);

    // Scroll-on-tray-item: forwards the spec's Scroll(dx, dy). Fire-and-forget
    // async; an item with no scroll handler simply ignores it on its side.
    void scroll(size_t index, int dx, int dy);

    // Split a watcher item reference ("service/path" or a bare "service") into
    // its parts; the default path is "/StatusNotifierItem". Static + pure so
    // the parsing is unit-testable without a bus.
    static void parseItemRef(const std::string& ref, std::string& service, std::string& path);

    // sd-bus vtable property getters (public so the anonymous namespace vtable
    // callbacks can forward to them; userdata = this).
    static int getRegisteredItems(sd_bus* bus, const char* path, const char* interface,
                                  const char* property, sd_bus_message* reply, void* userdata,
                                  sd_bus_error* ret_error);
    static int getHostRegistered(sd_bus* bus, const char* path, const char* interface,
                                 const char* property, sd_bus_message* reply, void* userdata,
                                 sd_bus_error* ret_error);
    static int getProtocolVersion(sd_bus* bus, const char* path, const char* interface,
                                  const char* property, sd_bus_message* reply, void* userdata,
                                  sd_bus_error* ret_error);

    // Watcher-mode method handlers (public so the vtable callbacks can forward).
    int handleRegisterItem(sd_bus_message* m);
    int handleRegisterHost(sd_bus_message* m);

private:
    // --- host mode (both modes) ---
    static int onItemChanged(sd_bus_message*, void*, sd_bus_error*);
    static int onWatcherOwnerChanged(sd_bus_message*, void*, sd_bus_error*);

    void registerHost();
    void refresh();  // re-read the watcher list (host) or local list (watcher)
    SNIItem fetchItem(const std::string& service, const std::string& path);
    void clearItems();
    void notify() {
        if (onChange_) onChange_();
    }

    // --- watcher mode ---
    bool detectExternalWatcher();
    void switchToWatcherMode();
    void registerWatcherVtable();
    void emitItemRegistered(const std::string& service);
    void emitItemUnregistered(const std::string& service);
    void watchItemOwnership(const std::string& service);
    static int onItemOwnerChanged(sd_bus_message* m, void* ud, sd_bus_error* e);

    SystemBus& bus_;
    Mode mode_ = Mode::Host;
    std::string hostName_;
    sd_bus_slot* regSlot_ = nullptr;
    sd_bus_slot* unregSlot_ = nullptr;
    sd_bus_slot* itemSlot_ = nullptr;
    sd_bus_slot* watcherSlot_ = nullptr;
    std::vector<SNIItem> items_;
    std::function<void()> onChange_;

    // Watcher-mode state (only used when mode_ == WatcherHost).
    sd_bus_slot* watcherVtableSlot_ = nullptr;
    std::vector<std::string> registeredItems_;   // bus names of registered SNI items
    std::vector<std::string> registeredHosts_;   // bus names of registered SNI hosts
    std::vector<sd_bus_slot*> ownerWatchSlots_;  // per-item ownership watches
};

}  // namespace qypr
