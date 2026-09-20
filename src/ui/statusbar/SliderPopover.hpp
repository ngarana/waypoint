// SliderPopover.hpp - Compact standalone slider popover for bar indicators.
#pragma once

#include "ui/statusbar/DetailedPopover.hpp"
#include "ui/statusbar/QSTile.hpp"

#include <memory>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace qypr {

// Hosts one of the existing QS slider tiles in a small anchored popover. This
// keeps bar clicks and QS controls on the same value/drag/key handling path.
class SliderPopover final : public DetailedPopover {
public:
    explicit SliderPopover(std::unique_ptr<QSTile> tile) : tile_(std::move(tile)) {}

    // Compact, single-control popup: one QS-style tile plus equal padding on
    // all sides. It is intentionally much smaller than the full QS panel.
    double contentWidth() const override { return 360.0; }
    double contentHeight() const override { return 84.0; }
    int autoDismissMs() const override { return 6000; }

    void draw(Painter& p, int64_t now) override {
        Rect b = getBounds();
        b.y += (growUp ? 1.0 : -1.0) * (1.0 - openProgress_.value(now)) * 6.0;

        if (!drawSharedBackdrop(p, b, theme::statusbar::popoverRadius)) {
            p.fillRoundedRectSource(b, theme::statusbar::popoverRadius,
                                    theme::statusbar::panelSurface());
        }

        tile_->bounds = {b.x + 12.0, b.y + 10.0, b.w - 24.0, 64.0};
        tile_->draw(p, now);
    }

    bool handleClick(double x, double y) override {
        tile_->onClick(x, y);
        return true;
    }

    bool handleDrag(double x, double y) override {
        tile_->onDrag(x, y);
        return true;
    }

    bool handleScroll(double dx, double dy) override {
        // Match the indicator behavior: a negative vertical wheel delta means
        // scroll up/raise, while horizontal wheels remain useful fallback.
        const double delta = dy != 0.0 ? dy : dx;
        if (delta == 0.0) return false;
        return tile_->handleKey(delta < 0.0 ? XKB_KEY_Up : XKB_KEY_Down);
    }

    bool handleKey(uint32_t keysym) override { return tile_->handleKey(keysym); }

private:
    std::unique_ptr<QSTile> tile_;
};

}  // namespace qypr
