// WifiIndicator.hpp - Status bar WiFi signal icon + QS toggle tile.
//
// Pure consumer of WifiBackend snapshots; the Quick Settings toggle flips
// the radio through the backend (async WirelessEnabled write).

#pragma once

#include "ui/indicators/TrayAppBinding.hpp"
#include "ui/statusbar/StatusIndicator.hpp"
#include "system/WifiBackend.hpp"
#include <string>

namespace qypr {

class WifiIndicator : public StatusIndicator {
public:
    explicit WifiIndicator(const SystemBackends& backends);

    std::string icon() const override;
    std::string themedIcon() const override;
    std::string tooltip() const override;

    void onBackendUpdate() override;

    std::unique_ptr<QSTile> createTile() override;

    // Click the icon to open the network picker (nearby APs; join a saved one,
    // or disconnect the active one).
    bool hasDetailedView() const override { return backend_ != nullptr; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

    // One surface: right click and the Menu key open the same picker. The
    // tray applet this module claims (nm-applet — see StatusArea) is merged
    // into it: the picker's last row opens the applet's menu (AppletLink).
    // Middle click forwards the applet's SecondaryActivate.
    bool onSecondaryClick(double /*x*/, double /*y*/) override { return hasDetailedView(); }
    bool hasContextMenu() const override { return hasDetailedView(); }
    std::unique_ptr<DetailedPopover> createContextMenu(double /*x*/, double /*y*/) override {
        return createDetailedView();
    }
    std::unique_ptr<DetailedPopover> createAppletMenu() override;
    bool onMiddleClick(double x, double y) override { return tray_.secondaryActivate(x, y); }

private:
    WifiBackend* backend_ = nullptr;
    TrayAppBinding tray_;
    WifiSnapshot lastSnap_;
};

}  // namespace qypr
