// RemovableDevicesIndicator.hpp - Removable drives (`removable-devices`).
//
// Aliases: `usb`, `usb-eject` (one module; the old ids each built their own
// polling copy).
//
// Shows drives UDisks2 considers removable (UDisksBackend):
//
//   - a freshly attached drive puts the module on the bar until it is
//     removed;
//   - a drive configured in fstab/crypttab is *fixed* and leaves the module
//     passive: it is listed in the status-area overflow instead of taking a
//     place on the bar;
//   - a running or failed operation always shows the module, so the result
//     of "safely remove" is never hidden.
//
// Left click opens the flyout (Open / Mount / Unmount per volume, Eject per
// drive, and what is keeping a busy drive in use). Right click opens a menu
// with the same actions, above the menu of a claimed tray applet (udiskie).
#pragma once

#include <memory>
#include <string>

#include "ui/indicators/TrayAppBinding.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class EventLoop;
class UDisksBackend;
struct FlyoutModel;
struct RemovableDrive;
struct RemovableDrivesSnapshot;

class RemovableDevicesIndicator : public StatusIndicator {
public:
    explicit RemovableDevicesIndicator(const SystemBackends& backends);

    [[nodiscard]] std::string icon() const override;
    [[nodiscard]] std::string themedIcon() const override;
    [[nodiscard]] std::string tooltip() const override;
    [[nodiscard]] Color iconColor() const override;
    [[nodiscard]] bool sensitive() const override { return true; }

    [[nodiscard]] bool hasDetailedView() const override;
    std::unique_ptr<DetailedPopover> createDetailedView() override;
    [[nodiscard]] bool hasContextMenu() const override;
    std::unique_ptr<DetailedPopover> createContextMenu(double x, double y) override;
    bool onMiddleClick(double x, double y) override { return tray_.secondaryActivate(x, y); }
    [[nodiscard]] OverflowEntry overflowEntry() const override;

    void onBackendUpdate() override;

    // The flyout's content for a snapshot. Static and backend-free for the
    // drawing; the buttons call back into `backend` / `loop`.
    static FlyoutModel buildFlyout(const RemovableDrivesSnapshot& snap, UDisksBackend* backend,
                                   EventLoop* loop);
    // One line per drive state ("Mounted at /mnt/shared", "Removing…").
    static std::string driveStatus(const RemovableDrive& drive);

private:
    [[nodiscard]] const RemovableDrivesSnapshot* snap() const;
    void refreshVisibility();

    UDisksBackend* backend_ = nullptr;
    EventLoop* loop_ = nullptr;
    TrayAppBinding tray_;
};

}  // namespace qypr
