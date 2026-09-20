// DNDIndicator.hpp - Do Not Disturb: moon icon (visible only when active)
// plus a Quick Settings toggle. Consumes the qypr-local DndState — no
// notification daemon involved (native-only principle).

#pragma once

#include "ui/statusbar/StatusIndicator.hpp"
#include "system/DndState.hpp"
#include <string>

namespace qypr {

class DNDIndicator : public StatusIndicator {
public:
    explicit DNDIndicator(const SystemBackends& backends);

    std::string icon() const override { return "󰽥"; }
    std::string themedIcon() const override { return "weather-clear-night-symbolic"; }
    std::string tooltip() const override { return "Do Not Disturb"; }
    Color iconColor() const override;
    bool qsOnly() const override { return true; }

    void onBackendUpdate() override;
    bool onClick(double x, double y) override;  // click toggles DND directly

    std::unique_ptr<QSTile> createTile() override;

private:
    DndState* dnd_ = nullptr;
};

}  // namespace qypr
