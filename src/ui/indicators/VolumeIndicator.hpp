// VolumeIndicator.hpp - Status bar volume icon + QS slider + scroll.
//
// Pure consumer of VolumeBackend snapshots; scroll on the icon and the
// Quick Settings slider write back through the backend (libpulse).

#pragma once

#include "ui/statusbar/StatusIndicator.hpp"
#include "system/VolumeBackend.hpp"
#include <string>

namespace qypr {

class VolumeIndicator : public StatusIndicator {
public:
    explicit VolumeIndicator(const SystemBackends& backends);

    std::string icon() const override;
    std::string themedIcon() const override;
    std::string tooltip() const override;

    void onBackendUpdate() override;
    bool onScroll(double dx, double dy, double x, double y) override;

    std::unique_ptr<QSTile> createTile() override;

    // Click the volume icon to open the audio panel: output-device switching
    // and per-app stream volumes (Phase 14). Only offered on the bar, where the
    // backend can actually enumerate them.
    bool hasDetailedView() const override { return backend_ != nullptr; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

    // Allow-listed on the lock screen: volume scroll/mute is round-tripped
    // through the audio server only (pulse) and changes nothing the unlocked
    // session later trusts. See StatusIndicator::lockInteractive().
    bool lockInteractive() const override { return true; }

private:
    VolumeBackend* backend_ = nullptr;
    bool sessionSurface_ = false;  // bar only → show the per-app stream list
    VolumeSnapshot lastSnap_;
};

}  // namespace qypr
