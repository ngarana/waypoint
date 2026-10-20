#include "ui/LockLayout.hpp"

#include <algorithm>

namespace qypr {

namespace {
constexpr double kMaxAuthColumnWidth = 480.0;
constexpr double kMinAuthColumnWidth = 260.0;
constexpr double kMaxClockColumnWidth = 720.0;
constexpr double kMinClockFontSize = 56.0;
constexpr double kMaxClockFontSize = 208.0;
}  // namespace

double LockLayout::clockFontSize(int width, int height) const {
    const double byWidth = static_cast<double>(width) * 0.09;
    const double byHeight = static_cast<double>(height) * 0.19;
    return std::clamp(std::min(byWidth, byHeight), kMinClockFontSize, kMaxClockFontSize);
}

double LockLayout::authColumnWidth(int width) const {
    const double byWidth = static_cast<double>(width) * 0.32;
    return std::clamp(std::min(byWidth, kMaxAuthColumnWidth), kMinAuthColumnWidth,
                      kMaxAuthColumnWidth);
}

double LockLayout::clockColumnWidth(int width) const {
    // The clock column stops short of the auth column so the two never
    // collide on narrow outputs.
    const double margin = theme().spacing.xlarge;
    const double columnRight = static_cast<double>(width) - margin - powerColumnReserve();
    const double columnLeft = std::max(margin, columnRight - authColumnWidth(width));
    const double available = std::max(margin, columnLeft - margin - theme().spacing.xlarge);
    return std::min({static_cast<double>(width) * 0.45, kMaxClockColumnWidth, available});
}

double LockLayout::notificationZoneWidth(int width) const {
    // The chip row shares the left column's width: it sits under the
    // chronometer, and stops short of the authentication card.
    return clockColumnWidth(width);
}

double LockLayout::notificationZoneHeight(int height) const {
    // The notification chips are a footer band: a row of chips, plus one
    // wrapped row for the narrowest outputs. The band never grows past the
    // rows the theme allows — what does not fit becomes a "+N" chip — so the
    // reserved height is exact.
    const theme::State::Notification& n = theme().notification;
    const double rows = static_cast<double>(std::max(1, n.chipRows));
    const double zone = (rows * n.chipHeight) + ((rows - 1.0) * n.chipGap);
    // On short outputs the band may take more than the screen can spare.
    return std::min(zone, static_cast<double>(height) * 0.12);
}

double LockLayout::powerColumnReserve() const {
    return kButtonDiameter + (2 * kPillPad) + theme().spacing.medium;
}

LockLayout::Result LockLayout::compute(int width, int height, const Size& clockSize,
                                       const Size& factsSize, const Size& authSize,
                                       const Size& mediaSize) const {
    Result result;
    const double margin = theme().spacing.xlarge;
    const double topInset =
        theme().statusbar.topMargin + theme().statusbar.height + theme().spacing.small;

    // Right-hand column: the auth card and the media card above it, kept
    // clear of the power pill's column on the far right.
    const double columnWidth = authColumnWidth(width);
    const double columnRight = static_cast<double>(width) - margin - powerColumnReserve();
    const double columnLeft = std::max(margin, columnRight - columnWidth);

    // The content band: both columns share one baseline, the way the mock
    // aligns its typographic lockup with the authentication block. The
    // baseline sits above the notification cards' zone.
    const double zoneTop = static_cast<double>(height) - margin - notificationZoneHeight(height);
    const double baseline = zoneTop - theme().spacing.large;

    result.auth = {columnLeft, baseline - authSize.h, columnWidth, authSize.h};
    if (mediaSize.h > 0) {
        result.media = {columnLeft, result.auth.y - theme().spacing.medium - mediaSize.h,
                        columnWidth, mediaSize.h};
    }

    // Left-hand column: the chronometer lockup with the facts line under
    // it, sharing the baseline with the auth card.
    const double clockWidth = clockColumnWidth(width);
    double factsTop = baseline - factsSize.h;
    double clockTop = factsTop - theme().spacing.large - clockSize.h;
    if (clockTop < topInset) {
        clockTop = topInset;
        factsTop = clockTop + clockSize.h + theme().spacing.large;
    }

    result.clock = {margin, clockTop, clockWidth, clockSize.h};
    result.facts = {margin, factsTop, clockWidth, factsSize.h};
    return result;
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
