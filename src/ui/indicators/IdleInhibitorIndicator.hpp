// IdleInhibitorIndicator.hpp - "Keep awake" toggle (Phase 15).
//
// A one-click bar toggle over IdleInhibitor: click the icon to hold/drop a
// zwp_idle_inhibitor so an idle daemon won't blank or lock the screen. Also
// offers a Quick Settings toggle tile. Bar-only (the backend is null on the lock
// screen) and self-hides when the compositor lacks the protocol.
#pragma once

#include <string>

#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class IdleInhibitor;

class IdleInhibitorIndicator : public StatusIndicator {
public:
    explicit IdleInhibitorIndicator(const SystemBackends& backends);

    std::string icon() const override;
    std::string themedIcon() const override;
    std::string tooltip() const override;
    Color iconColor() const override;
    bool qsOnly() const override { return true; }

    void onBackendUpdate() override;
    bool onClick(double x, double y) override;  // click toggles directly

    std::unique_ptr<QSTile> createTile() override;

private:
    IdleInhibitor* backend_ = nullptr;
};

}  // namespace qypr
