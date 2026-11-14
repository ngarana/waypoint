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

    // Right click: the menu of the tray applet this module claims (nm-applet,
    // blueman, pasystray — see StatusArea); middle click forwards its
    // SecondaryActivate. Inert while the applet is not running.
    bool hasContextMenu() const override { return tray_.bound(); }
    std::unique_ptr<DetailedPopover> createContextMenu(double x, double y) override;
    bool onMiddleClick(double x, double y) override { return tray_.secondaryActivate(x, y); }

private:
    BluetoothBackend* backend_ = nullptr;
    TrayAppBinding tray_;
    BluetoothSnapshot lastSnap_;
};

}  // namespace qypr
