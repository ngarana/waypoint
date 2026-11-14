// PhoneLinkIndicator.hpp - A paired phone over KDE Connect (`phone-link`).
//
// Aliases: `kde-connect`, `phone-connect`.
//
// On the bar while a paired phone is connected or a device asks to pair;
// passive (listed in the status-area overflow) while paired phones are out of
// reach; absent without kdeconnectd or paired devices. The old module showed
// whenever the daemon process existed and always said "Connected".
//
// The flyout carries what KDE Connect's own applet does from a panel: battery
// and signal, Ring, send the clipboard or files, browse the phone, the
// notifications mirrored from it (dismissing one dismisses it on the phone),
// and pairing requests. Right click lists the same actions above the menu of
// a claimed kdeconnect-indicator.
//
// Session-sensitive: phone notifications never reach the lock screen.
#pragma once

#include <memory>
#include <string>

#include "ui/indicators/TrayAppBinding.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class EventLoop;
class KdeConnectBackend;
struct FlyoutModel;
struct PhoneDevice;
struct PhoneLinkSnapshot;

class PhoneLinkIndicator : public StatusIndicator {
public:
    explicit PhoneLinkIndicator(const SystemBackends& backends);

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

    static FlyoutModel buildFlyout(const PhoneLinkSnapshot& snap, KdeConnectBackend* backend,
                                   EventLoop* loop);
    // "98% · charging · LTE ▂▄▆█", "Not connected".
    static std::string deviceStatus(const PhoneDevice& device);

    // Notification rows shown before "N more on the phone".
    static constexpr size_t kMaxNotifications = 5;

private:
    [[nodiscard]] const PhoneLinkSnapshot* snap() const;
    void refreshVisibility();

    KdeConnectBackend* backend_ = nullptr;
    EventLoop* loop_ = nullptr;
    TrayAppBinding tray_;
};

}  // namespace qypr
