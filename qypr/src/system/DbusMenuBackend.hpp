// DbusMenuBackend.hpp - com.canonical.dbusmenu client for tray item menus.
//
// StatusNotifierItems point at a com.canonical.dbusmenu object (the item's
// `Menu` property) that carries the right-click menu. This fetches a menu level
// (GetLayout) and dispatches item activations (Event "clicked"). Standard
// cross-desktop interface — every tray applet that ships a menu implements it
// (nm-applet, blueman, …), so this is not a daemon-specific API.
//
// The fetch is a synchronous request/reply: it happens only when the user opens
// a menu (right-click), the same established tradeoff SNIBackend already makes
// for its per-item property reads. This is not polling — there is no timer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qypr {

class SystemBus;

// One menu entry. Defaults follow the dbusmenu spec: absent enabled/visible
// mean true. `toggleState` is 0 (off), 1 (on), or -1 (not a toggle / unknown).
struct MenuNode {
    int32_t id = 0;
    std::string label;  // mnemonic underscores already stripped
    bool enabled = true;
    bool visible = true;
    bool separator = false;  // type == "separator"
    std::string toggleType;  // "checkmark" | "radio" | ""
    int32_t toggleState = -1;
    bool hasSubmenu = false;  // children-display == "submenu"
    std::vector<MenuNode> children;
};

class DbusMenuBackend {
public:
    explicit DbusMenuBackend(SystemBus& sessionBus) : bus_(sessionBus) {}

    // The direct children of `parentId` (0 = the root menu). Calls AboutToShow
    // first so lazily-populated submenus (nm-applet's network lists) fill in.
    // Returns empty on any error — a menu that will not load simply shows empty.
    std::vector<MenuNode> fetch(const std::string& service, const std::string& menuPath,
                                int32_t parentId = 0);

    // Fire a menu item ("clicked" event). Fire-and-forget async.
    void clicked(const std::string& service, const std::string& menuPath, int32_t id);

private:
    SystemBus& bus_;
};

}  // namespace qypr
