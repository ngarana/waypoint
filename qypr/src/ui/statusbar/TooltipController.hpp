// TooltipController.hpp - Hover-dwell indicator tooltips for the status bar.
//
// Owns target selection, dwell timing, fade animation, and glass-card geometry.
// StatusBar records hover via setTarget/clear and calls draw() last in paint.
#pragma once

#include "core/Types.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/StatusBarLayout.hpp"  // BarGeometry

namespace qypr {

class Painter;
class StatusIndicator;

class TooltipController {
public:
    // Delay before the label fades in while the pointer rests on one indicator.
    static constexpr int kDelayMs = 500;

    // Record the current hover target. Changing target (or clearing) resets
    // dwell and starts the fade-out of any visible label. Returns true when
    // the target actually changed (host should invalidate).
    bool setTarget(StatusIndicator* ind, int64_t now);
    void clear();

    StatusIndicator* target() const { return target_; }

    // Paint (or fade out) the label for the current target. Call last so the
    // tooltip floats over the strip. Mutates the fade animation as needed.
    void draw(Painter& p, const theme::State& theme, const BarGeometry& geom, const Rect& strip,
              int64_t now) const;

    bool animating(int64_t now) const { return alpha_.active(now); }

private:
    StatusIndicator* target_ = nullptr;
    int64_t hoverStartMs_ = 0;
    mutable Animated alpha_{0.0};
};

}  // namespace qypr
