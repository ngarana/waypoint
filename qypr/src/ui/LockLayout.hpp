// LockLayout.hpp - Geometry for the lock screen and its power controls.

#pragma once

#include <array>

#include "core/Types.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"

namespace qypr {

// The lock layout knows only about measurements and the theme. It does not
// own widgets or input state, so it can be exercised without a compositor.
class LockLayout : public theme::ThemeAware {
public:
    static constexpr int kNumPowerActions = 4;
    static constexpr double kButtonDiameter = 52.0;
    static constexpr double kPillPad = 8.0;

    struct Result {
        double centerX = 0.0;
        double clockTop = 0.0;
        double columnWidth = 0.0;
        Rect password;
        double statusTop = 0.0;
        double audioTop = 0.0;
        Rect powerRow;
        std::array<Rect, kNumPowerActions> powerButtons{};
        Rect powerAnchor;
    };

    // The widget measurements are supplied by the renderer because measuring
    // text requires a Painter. Everything after those measurements is pure
    // geometry derived from the output size and theme.
    Result compute(int width, int height, const Size& clockSize, const Size& statusSize) const;

    double centerColumnWidth(int width) const;
    Rect powerRowRect(int width, int height) const;
    Rect powerButtonRect(int index, int width, int height) const;
    Rect powerAnchorRect(int width, int height) const;
};

}  // namespace qypr
