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

    void setTheme(const theme::State& state) override {
        theme::ThemeAware::setTheme(state);
        if (tile_) tile_->setTheme(state);
    }

    // Compact, single-control popup: one QS-style tile plus equal padding on
    // all sides. It is intentionally much smaller than the full QS panel.
    double contentWidth() const override { return 360.0; }
    double contentHeight() const override { return 84.0; }
    int autoDismissMs() const override { return 0; }

    void draw(Painter& p, int64_t now) override {
        Rect b = getBounds();

        if (!drawSharedBackdrop(p, b, theme().statusbar.popoverRadius)) {
            p.fillRoundedRectSource(b, theme().statusbar.popoverRadius, theme().panelSurface());
        }

        tile_->bounds = {b.x + 12.0, b.y + 10.0, b.w - 24.0, 64.0};
        tile_->draw(p, now);
    }

    bool handleClick(double x, double y) override {
        dragging_ = tile_->canDragAt(x, y);
        if (tile_->bounds.contains(x, y)) { tile_->onClick(x, y); }
        return true;
    }

    bool handleDrag(double x, double y) override {
        if (!dragging_) { return false; }
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

    void handleRelease() override { dragging_ = false; }

    bool handleKey(uint32_t keysym) override { return tile_->handleKey(keysym); }

private:
    std::unique_ptr<QSTile> tile_;
    bool dragging_ = false;
};

}  // namespace qypr
