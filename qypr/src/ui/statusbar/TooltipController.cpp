// TooltipController.cpp - Hover tooltip dwell, fade, and geometry.
#include "ui/statusbar/TooltipController.hpp"

#include <algorithm>
#include <string>

#include "render/Painter.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

bool TooltipController::setTarget(StatusIndicator* ind, int64_t now) {
    if (ind == target_) { return false; }
    target_ = ind;
    hoverStartMs_ = now;
    alpha_.set(0.0);
    return true;
}

void TooltipController::clear() {
    target_ = nullptr;
    alpha_.set(0.0);
}

void TooltipController::draw(Painter& p, const theme::State& theme, const BarGeometry& geom,
                             const Rect& strip, int64_t now) const {
    if (target_ == nullptr) {
        if (alpha_.target() > 0.01) { alpha_.animateTo(0.0, theme.anim.fast, ease::inOutQuad); }
        return;
    }
    const std::string text = target_->tooltip();
    if (text.empty()) { return; }

    const int64_t dwell = now - hoverStartMs_;
    if (dwell >= kDelayMs && alpha_.target() < 0.99) {
        alpha_.animateTo(1.0, theme.anim.fast, ease::inOutQuad);
    }

    const double a = alpha_.value(now);
    if (a < 0.01) { return; }

    // Geometry: measure once, place below (or above on a bottom bar) the
    // indicator — away from the anchored screen edge, like every popover.
    constexpr double kPadX = 10.0;
    constexpr double kPadY = 6.0;
    constexpr double kRadius = 8.0;
    constexpr double kGap = 8.0;

    TextStyle const style{.family = theme.font.family,
                          .size = 12.0,
                          .weight = PANGO_WEIGHT_NORMAL,
                          .color = theme.colors.text};
    Size const ts = p.measureText(text, style);
    double const w = ts.w + (kPadX * 2.0);
    double const h = ts.h + (kPadY * 2.0);

    double const cx = target_->bounds.cx();
    double x = cx - (w / 2.0);
    double const y =
        geom.bottom ? target_->bounds.y - kGap - h : target_->bounds.y + target_->bounds.h + kGap;
    x = std::clamp(x, strip.x + 2.0, strip.x + strip.w - w - 2.0);

    Rect const r{.x = x, .y = y, .w = w, .h = h};
    p.pushGroup();
    p.fillRoundedRect(r, kRadius, theme.colors.glass);
    p.strokeRoundedRect(r, kRadius, theme.colors.glassBorder, 1.0);
    p.drawText(r.x + kPadX, r.y + kPadY, text, style);
    p.popGroupWithAlpha(a);
}

}  // namespace qypr
