// PowerMenuIndicator.cpp - Power trigger for the unlocked bar.
//
// The indicator itself is only a trigger: activating it opens Quick Settings
// (see StatusBar::activateIndicator, which diverts "power" before any detailed
// view is consulted), whose power tile runs `waylaunch --power`. The former
// in-bar PowerMenuPopover was unreachable through every path and is deleted;
// the lock screen keeps its own ui/ConfirmPopover, which a layer-shell overlay
// could never replace.
#include "ui/indicators/PowerMenuIndicator.hpp"

#include "power/SystemActions.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

PowerMenuIndicator::PowerMenuIndicator(const SystemBackends& backends)
    : StatusIndicator("power", Zone::Right, 700),
      power_(backends.power) {
    visible = power_ != nullptr;
}

Color PowerMenuIndicator::iconColor() const {
    return theme().colors.text;
}

bool PowerMenuIndicator::onClick(double x, double y) {
    (void)x;
    (void)y;
    return false;
}

std::unique_ptr<DetailedPopover> PowerMenuIndicator::createDetailedView() {
    // Unreachable: StatusBar diverts "power" to Quick Settings before any
    // detailed view is consulted. Kept returning null (rather than removing
    // the override) so a future caller fails closed instead of resurrecting a
    // second power UI.
    return nullptr;
}

REGISTER_INDICATOR("power", Zone::Right, 700, PowerMenuIndicator)

}  // namespace qypr
