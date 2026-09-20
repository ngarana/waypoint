// MediaIndicator.hpp - Now-playing applet for the unlocked bar.
//
// Compact "▶ Title — Artist" in the bar; a popover with transport controls.
// Reads the existing MprisController, which the bar puts in push mode
// (STATUS_BAR.md decision D4) so nothing is polled.
//
// Session-sensitive: what you are listening to is session content, and the lock
// screen already has its own audio panel behind the reveal. Hides itself
// entirely when no controller is supplied (qypr-lock supplies none).

#pragma once

#include <string>

#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class MprisController;

class MediaIndicator : public StatusIndicator {
public:
    explicit MediaIndicator(const SystemBackends& backends);

    std::string icon() const override;
    std::string themedIcon() const override;
    std::string label() const override;
    std::string tooltip() const override;
    Color iconColor() const override;

    bool hasDetailedView() const override { return true; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

    // Click the compact view to play/pause; the popover has the full transport.
    bool onClick(double x, double y) override;
    // Scroll over the applet to change tracks.
    bool onScroll(double dx, double dy, double x, double y) override;

    void onBackendUpdate() override;

    bool sensitive() const override { return true; }

    // Allow-listed on the lock screen: transport control (play/pause/next) is a
    // deliberate part of what the lock offers, alongside volume and brightness.
    // It is unreachable today — sensitive() keeps the applet hidden while the
    // session is locked, and qypr-lock supplies no MprisController — but the
    // policy belongs here rather than in the host, so a future lock-screen media
    // applet does not silently become non-interactive.
    bool lockInteractive() const override { return true; }

private:
    bool playing() const;

    MprisController* mpris_ = nullptr;
};

}  // namespace qypr
