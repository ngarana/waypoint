#include "ui/LockLayout.hpp"

#include <algorithm>

#include "ui/PasswordField.hpp"

namespace qypr {

LockLayout::Result LockLayout::compute(int width, int height, const Size& clockSize,
                                       const Size& statusSize) const {
    Result result;
    result.centerX = width / 2.0;

    // Keep the auth stack clear of the chromeless status bar. The preferred
    // position retains the established visual balance on normal displays,
    // while the clamp prevents the clock from drifting under the bar on short
    // laptop or portrait outputs.
    const double topInset =
        theme().statusbar.topMargin + theme().statusbar.height + theme().spacing.small;
    const double bottomInset = theme().spacing.xlarge;
    const double stackHeight = clockSize.h + theme().spacing.xlarge + PasswordField::kHeight +
                               theme().spacing.small + statusSize.h;
    const double preferredTop = height * 0.18;
    const double latestTop =
        std::max(topInset, static_cast<double>(height) - bottomInset - stackHeight);
    result.clockTop = std::clamp(preferredTop, topInset, latestTop);

    const double clockBottom = result.clockTop + clockSize.h;
    result.columnWidth = centerColumnWidth(width);
    const double passwordTop = clockBottom + theme().spacing.xlarge;
    result.password = {.x = result.centerX - (result.columnWidth / 2.0),
                       .y = passwordTop,
                       .w = result.columnWidth,
                       .h = PasswordField::kHeight};
    result.statusTop = result.password.y + result.password.h + theme().spacing.small;
    result.audioTop = result.statusTop + statusSize.h + theme().spacing.small;

    return result;
}

double LockLayout::centerColumnWidth(int width) const {
    const double horizontalMargin = theme().spacing.large;
    return std::max(
        0.0, std::min(static_cast<double>(width) - (horizontalMargin * 2.0), kMaxColumnWidth));
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
