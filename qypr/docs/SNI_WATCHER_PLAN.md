# Built-in SNI Watcher for qypr-bar

## Problem

`SNIBackend` only runs in host mode against an external `StatusNotifierWatcher`.
When no external watcher exists, zero tray items are discovered and the SNI
indicator stays hidden.

## Solution

Add dual-mode operation: detect no external watcher → claim the watcher name →
implement the watcher interface → act as both watcher and host simultaneously.

## Files to Modify

- `src/system/SNIBackend.hpp`
- `src/system/SNIBackend.cpp`

## Architecture

### Mode Detection

At startup, call `sd_bus_name_has_owner(bus, "org.kde.StatusNotifierWatcher", ...)`:
- **Owned** → host mode (current behavior)
- **Not owned** → watcher+host mode

### Watcher+Host Mode

Claim `org.kde.StatusNotifierWatcher` via `sd_bus_request_name`, register a vtable
at `/StatusNotifierWatcher` with:

| Type | Name | Signature |
|------|------|-----------|
| Method | `RegisterStatusNotifierItem` | `(s)` |
| Method | `RegisterStatusNotifierHost` | `(s)` |
| Property | `RegisteredStatusNotifierItems` | `(as)` |
| Property | `IsStatusNotifierHostRegistered` | `(b)` |
| Property | `ProtocolVersion` | `(i)` |
| Signal | `StatusNotifierItemRegistered` | `(s)` |
| Signal | `StatusNotifierItemUnregistered` | `(s)` |
| Signal | `StatusNotifierHostRegistered` | `()` |
| Signal | `StatusNotifierHostUnregistered` | `()` |

Also register as host (watcher can be a host too). Monitor each registered item's
bus ownership via `NameOwnerChanged` match rules to auto-remove dead items.

## Header Changes (SNIBackend.hpp)

New members (watcher mode only):

```cpp
enum class Mode { Host, WatcherHost };

Mode mode_ = Mode::Host;
std::vector<std::string> registeredItems_;
std::vector<std::string> registeredHosts_;
sd_bus_slot* watcherVtableSlot_ = nullptr;
std::vector<sd_bus_slot*> ownerWatchSlots_;
```

New private methods:

```cpp
bool detectExternalWatcher();
void switchToWatcherMode();
void registerWatcherVtable();
int handleRegisterItem(sd_bus_message* m);
int handleRegisterHost(sd_bus_message* m);
void emitItemRegistered(const std::string& service);
void emitItemUnregistered(const std::string& service);
void watchItemOwnership(const std::string& service);
static int onItemOwnerChanged(sd_bus_message* m, void* ud, sd_bus_error* e);
```

## Vtable

```cpp
static const sd_bus_vtable kWatcherVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("RegisterStatusNotifierItem", "s", "",
                  &SNIBackend::handleRegisterItem, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RegisterStatusNotifierHost", "s", "",
                  &SNIBackend::handleRegisterHost, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("RegisteredStatusNotifierItems", "as",
                    &SNIBackend::getRegisteredItems, nullptr,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("IsStatusNotifierHostRegistered", "b",
                    &SNIBackend::getHostRegistered, nullptr, 0),
    SD_BUS_PROPERTY("ProtocolVersion", "i",
                    &SNIBackend::getProtocolVersion, nullptr, 0),
    SD_BUS_SIGNAL("StatusNotifierItemRegistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierItemUnregistered", "s", 0),
    SD_BUS_SIGNAL("StatusNotifierHostRegistered", "", 0),
    SD_BUS_SIGNAL("StatusNotifierHostUnregistered", "", 0),
    SD_BUS_VTABLE_END,
};
```

## Start Flow

```cpp
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
```

In watcher mode, `refresh()` reads from the local `registeredItems_` vector instead
of querying the external watcher.

## Ownership Monitoring

Each registered item gets a `NameOwnerChanged` match rule. When the item's bus
name disappears, it is removed from `registeredItems_`, the
`StatusNotifierItemUnregistered` signal is emitted, and `refresh()` is called.

## Behavior Summary

| Scenario | Mode | Behavior |
|----------|------|----------|
| External watcher running | Host | Current behavior |
| No external watcher | WatcherHost | Claim watcher, implement interface, track items, also act as host |
| External watcher appears later | WatcherHost | Keep own watcher (both coexist) |

## Testing

- Existing tests: `SNIParseItemRef`, `SNITrayHostConstruction`, `SNITrayHostVisibleWithItems`
- New: verify `detectExternalWatcher()` returns false when no watcher
- New: verify watcher vtable registration
- Integration: run `qypr-bar` standalone, launch a tray app (nm-applet), verify icons appear
