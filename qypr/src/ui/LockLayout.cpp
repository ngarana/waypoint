#include "ui/LockLayout.hpp"

#include <algorithm>

#include "ui/PasswordField.hpp"

namespace qypr {

LockLayout::Result LockLayout::compute(int width, int height, const Size& clockSize,
                                       const Size& statusSize) const {
    Result result;
    result.centerX = width / 2.0;
    result.clockTop = height * 0.18;

    const double clockBottom = result.clockTop + clockSize.h;
    result.columnWidth = centerColumnWidth(width);
    const double passwordTop = clockBottom + theme().spacing.xlarge;
    result.password = {.x = result.centerX - (result.columnWidth / 2.0),
                       .y = passwordTop,
                       .w = result.columnWidth,
                       .h = PasswordField::kHeight};
    result.statusTop = result.password.y + result.password.h + theme().spacing.small;
    result.audioTop = result.statusTop + statusSize.h + theme().spacing.small;

    result.powerRow = powerRowRect(width, height);
    for (int i = 0; i < kNumPowerActions; ++i) {
        result.powerButtons.at(static_cast<size_t>(i)) = powerButtonRect(i, width, height);
    }
    result.powerAnchor = powerAnchorRect(width, height);
    return result;
}

double LockLayout::centerColumnWidth(int width) const {
    return std::max(0.0, std::min(width - (theme().spacing.xlarge * 2.0),
                                  static_cast<double>(theme().audio.maxWidth)));
}

Rect LockLayout::powerRowRect(int width, int height) const {
    const double d = kButtonDiameter;
    const double spacing = theme().spacing.medium;
    const double fullHeight =
        ((kNumPowerActions + 1) * d) + (kNumPowerActions * spacing) + (kPillPad * 2.0);
    const double pillWidth = d + (kPillPad * 2.0);
    const double right = width - theme().spacing.xlarge;
    const double bottom = height - theme().spacing.xlarge;
    return {.x = right - pillWidth, .y = bottom - fullHeight, .w = pillWidth, .h = fullHeight};
}

Rect LockLayout::powerButtonRect(int index, int width, int height) const {
    if (index < 0 || index >= kNumPowerActions) { return {}; }

    const double d = kButtonDiameter;
    const double spacing = theme().spacing.medium;
    const Rect column = powerRowRect(width, height);
    const double centerY = column.y + kPillPad + (d / 2.0) + (index * (d + spacing));
    return {.x = column.cx() - (d / 2.0), .y = centerY - (d / 2.0), .w = d, .h = d};
}

Rect LockLayout::powerAnchorRect(int width, int height) const {
    const double d = kButtonDiameter;
    const double spacing = theme().spacing.medium;
    const Rect column = powerRowRect(width, height);
    const double centerY = column.y + kPillPad + (d / 2.0) + (kNumPowerActions * (d + spacing));
    return {.x = column.cx() - (d / 2.0), .y = centerY - (d / 2.0), .w = d, .h = d};
}

}  // namespace qypr
