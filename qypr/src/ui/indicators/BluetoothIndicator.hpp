// BluetoothIndicator.hpp - Status bar Bluetooth icon + QS toggle tile.
//
// Pure consumer of BluetoothBackend snapshots; the Quick Settings toggle
// flips the adapter through the backend (async Powered write).

#pragma once

#include "ui/indicators/TrayAppBinding.hpp"
#include "ui/statusbar/StatusIndicator.hpp"
#include "system/BluetoothBackend.hpp"
#include <string>

namespace qypr {

class BluetoothIndicator : public StatusIndicator {
public:
    explicit BluetoothIndicator(const SystemBackends& backends);

    std::string icon() const override;
    std::string themedIcon() const override;
    std::string tooltip() const override;
    Color iconColor() const override;

    void onBackendUpdate() override;

    std::unique_ptr<QSTile> createTile() override;

    // Click the icon to open the device picker (connect/disconnect, battery).
    bool hasDetailedView() const override { return backend_ != nullptr; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

    // One surface: right click and the Menu key open the same picker. The
    // tray applet this module claims (blueman — see StatusArea) is merged
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
    BluetoothBackend* backend_ = nullptr;
    TrayAppBinding tray_;
    BluetoothSnapshot lastSnap_;
};

}  // namespace qypr
