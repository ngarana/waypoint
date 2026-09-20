// SNITrayHost.hpp - Status bar system-tray applet (StatusNotifierItem host).
//
// Renders one small icon per tracked SNI item and left-click-activates the
// item under the pointer. Icons come from the item's themed IconName (via the
// shared IconResolver) or, failing that, its IconPixmap.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "system/SNIBackend.hpp"  // SNIItem
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class SNIBackend;
class DbusMenuBackend;

class SNITrayHost : public StatusIndicator {
public:
    explicit SNITrayHost(const SystemBackends& backends);

    std::string icon() const override { return ""; }  // custom multi-icon draw
    std::string tooltip() const override;
    // The tray is a first-class bar applet (a row of live item icons), not a
    // Quick Settings toggle — it must NOT be qsOnly(), or isShown() skips it in
    // layout/draw/input and, with no createTile(), it appears nowhere.

    double measureWidth(Painter& p) override;
    void draw(Painter& p, int64_t now) override;

    void onBackendUpdate() override;
    bool onClick(double x, double y) override;                         // left: Activate
    bool onSecondaryClick(double x, double y) override;                // right: dbusmenu
    bool onMiddleClick(double x, double y) override;                   // middle: SecondaryActivate
    bool onScroll(double dx, double dy, double x, double y) override;  // forward SNI Scroll

    // A right-click on an item with a menu stashes it here and asks StatusBar to
    // open the detailed view; createDetailedView() then builds the MenuPopover.
    // Clicking the overflow chevron (when passive items exist) stashes -2 and
    // opens the overflow list instead. Both share a single popover slot.
    bool hasDetailedView() const override { return pendingMenu_ != -1; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

private:
    int iconIndexAt(double x) const;  // which visible tray icon is under x, or -1
    bool overOverflow(double x) const;
    // Whether the icon resolved for `name` is monochrome/symbolic (needs
    // recolouring to the foreground). Memoised in monoCache_.
    bool iconIsMonochrome(const std::string& name, cairo_surface_t* s);

    // Active/NeedsAttention items go on the strip; Passive ones live in the
    // overflow popover. Items report SNI status "Active" | "Passive" |
    // "NeedsAttention"; an item is shown on-strip unless it is "Passive".
    bool onStrip(const SNIItem& it) const {
        return (it.status == "Passive") ? false : (!it.iconName.empty() || it.pixmap);
    }

    SNIBackend* backend_ = nullptr;
    DbusMenuBackend* dbusMenu_ = nullptr;
    int pendingMenu_ = -1;  // item index whose menu a right-click requested
    // Width of the overflow chevron column (set in draw() so hit-testing knows
    // where to look).
    double overflowBoxStart_ = -1.0;
    bool overflowBoxShown_ = false;
    // Icon name → is-monochrome, so the per-frame draw doesn't re-scan pixels.
    std::unordered_map<std::string, bool> monoCache_;
};

}  // namespace qypr
