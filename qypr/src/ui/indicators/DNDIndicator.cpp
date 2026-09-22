// DNDIndicator.cpp - Do Not Disturb indicator implementation.
#include "ui/indicators/DNDIndicator.hpp"

#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/QSTile.hpp"

namespace qypr {

DNDIndicator::DNDIndicator(const SystemBackends& backends)
    : StatusIndicator("dnd", Zone::Right, 400),
      dnd_(backends.dnd) {
    // Tray icon only appears while DND is active (the QS tile is always
    // there to switch it on).
    visible = dnd_ && dnd_->enabled();
}

Color DNDIndicator::iconColor() const {
    return theme().colors.primary;  // accent while shown (i.e. active)
}

void DNDIndicator::onBackendUpdate() {
    visible = dnd_ && dnd_->enabled();
}

bool DNDIndicator::onClick(double, double) {
    // The tray icon only shows while DND is active, so a click is a request to
    // switch it back off. Toggle directly instead of opening Quick Settings.
    if (!dnd_) return false;
    dnd_->toggle();
    return true;  // consume — no popover/panel
}

std::unique_ptr<QSTile> DNDIndicator::createTile() {
    auto dnd = dnd_;
    return std::make_unique<QSToggleTile>(
        "Do Not Disturb", "󰽥", [dnd]() { return dnd && dnd->enabled(); },
        [dnd]() {
            if (dnd) dnd->toggle();
        },
        [dnd]() -> std::string { return dnd && dnd->enabled() ? "On" : "Off"; }, Color{0, 0, 0, 0},
        QSTile::Role::Dnd);
}

REGISTER_INDICATOR("dnd", Zone::Right, 400, DNDIndicator)

}  // namespace qypr
