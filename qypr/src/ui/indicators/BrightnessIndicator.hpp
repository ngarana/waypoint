// BrightnessIndicator.hpp - Status bar brightness icon + QS slider + scroll.
//
// Pure consumer of BrightnessBackend snapshots; scroll on the icon and the
// Quick Settings slider write back through the backend (logind).

#pragma once

#include "ui/statusbar/StatusIndicator.hpp"
#include "system/BrightnessBackend.hpp"
#include <string>

namespace qypr {

class BrightnessIndicator : public StatusIndicator {
public:
    explicit BrightnessIndicator(const SystemBackends& backends);

    std::string icon() const override;
    std::string themedIcon() const override;
    std::string tooltip() const override;

    void onBackendUpdate() override;
    bool onScroll(double dx, double dy, double x, double y) override;

    std::unique_ptr<QSTile> createTile() override;
    bool hasDetailedView() const override { return backend_ != nullptr; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

    // Allow-listed on the lock screen: backlight scroll is a transient
    // hardware setting (logind), not session state. See
    // StatusIndicator::lockInteractive().
    bool lockInteractive() const override { return true; }

private:
    BrightnessBackend* backend_ = nullptr;
    BrightnessSnapshot lastSnap_;
};

}  // namespace qypr
