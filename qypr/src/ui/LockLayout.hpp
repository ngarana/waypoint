// LockLayout.hpp - Geometry for the lock screen and its power controls.
//
// The lock screen follows the composition of the redesign mock
// (docs/mock/code.html): an asymmetric two-column canvas. The left column
// carries the oversized chronometer and the facts line under it; the right
// column carries the authentication card, bottom-anchored to the content
// baseline. The now-playing strip shares the footer with the notification
// chips (right and left respectively), and the power pill keeps the
// bottom-right corner.
//
// The layout knows only about measurements and the theme. It does not own
// widgets or input state, so it can be exercised without a compositor.

#pragma once

#include "core/Types.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"

namespace qypr {

class LockLayout : public theme::ThemeAware {
public:
    static constexpr int kNumPowerActions = 4;
    static constexpr double kButtonDiameter = 52.0;
    static constexpr double kPillPad = 8.0;

    struct Result {
        Rect clock;  // left column: the chronometer lockup
        Rect facts;  // left column: the facts line under the lockup
        Rect auth;   // right column: the authentication card
        Rect media;  // footer right: the now-playing strip (empty when idle)
    };

    // Responsive type size of the oversized clock: it grows with the output
    // but stays inside sane bounds on small screens.
    double clockFontSize(int width, int height) const;

    // Left edge of the lock's text. The status bar sets its labels inside the
    // strip's padding, so the chronometer, facts and chips line up with them.
    double contentMargin() const;

    // Width of the right-hand column (the auth card and the media strip).
    double authColumnWidth(int width) const;

    // Width of the left-hand column (the chronometer lockup).
    double clockColumnWidth(int width) const;

    // Height the notification chips may claim at the bottom-left. The clock
    // column is kept clear of it.
    double notificationZoneHeight(int height) const;

    // Width the notification chip row may claim: the left column's width.
    double notificationZoneWidth(int width) const;

    // The widget measurements are supplied by the renderer because measuring
    // text requires a Painter. `hasMedia` reserves the footer strip.
    Result compute(int width, int height, const Size& clockSize, const Size& factsSize,
                   const Size& authSize, bool hasMedia) const;

    Rect powerRowRect(int width, int height) const;
    Rect powerButtonRect(int index, int width, int height) const;
    Rect powerAnchorRect(int width, int height) const;

private:
    // Horizontal room the right-hand column leaves for the power pill.
    double powerColumnReserve() const;
};

}  // namespace qypr
