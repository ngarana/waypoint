// TrayAppBinding.hpp - A native module's link to the tray app it supersedes.
//
// The wifi module claims nm-applet, bluetooth claims blueman (StatusArea's
// claim table). The applet's tray icon is never drawn; instead the module
// forwards the gestures the applet would have received:
//
//   right click  → the applet's own menu (dbusmenu), appended under any
//                  native section the module supplies;
//   middle click → SecondaryActivate.
//
// App modules (whatsapp, email) also forward left click to an app that has
// no window open but sits in the tray (activate()).
//
// The binding resolves the claimed item freshly on every call: tray items
// come and go with their processes, and the backend rebuilds its list on each
// change, so neither pointers nor indices are cached.
#pragma once

#include <memory>
#include <string>

#include "ui/statusbar/AppletLink.hpp"
#include "ui/statusbar/MenuSource.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class DetailedPopover;
struct SNIItem;

class TrayAppBinding {
public:
    TrayAppBinding(const SystemBackends& backends, std::string moduleId);

    // The claimed tray item, or nullptr while the applet is not running.
    [[nodiscard]] const SNIItem* item() const;
    [[nodiscard]] bool bound() const { return item() != nullptr; }

    // The applet's dbusmenu as a menu section, or null when it has none.
    [[nodiscard]] std::unique_ptr<MenuSource> menuSection() const;

    // A context menu: `native` (may be null) above the applet's own menu.
    // When neither has items, asks a menu-less applet to show its own menu
    // (the spec's ContextMenu) and returns null.
    std::unique_ptr<DetailedPopover> contextMenu(double x, double y,
                                                 std::unique_ptr<MenuSource> native,
                                                 const std::string& title) const;

    // Merged modules (wifi, bluetooth) reach the applet from their flyout
    // instead: link() is the flyout's "More from nm-applet ›" row, and
    // appletMenu() the popover it swaps in — the applet's dbusmenu under a
    // first row that goes back to the module's flyout ("‹ Wi-Fi"). Null when
    // the applet isn't running; a menu-less applet is asked to show its own
    // menu instead.
    [[nodiscard]] AppletLink link() const;
    std::unique_ptr<DetailedPopover> appletMenu(const std::string& backLabel) const;

    // Forward middle click. False when no applet is bound.
    bool secondaryActivate(double x, double y) const;
    // The applet's Activate (a tray app shows its hidden window). False when
    // no applet is bound.
    bool activate(double x, double y) const;

private:
    SNIBackend* sni_ = nullptr;
    DbusMenuBackend* menus_ = nullptr;
    StatusArea* area_ = nullptr;
    std::string moduleId_;
};

}  // namespace qypr
