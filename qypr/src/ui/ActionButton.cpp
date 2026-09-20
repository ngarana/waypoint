#include "ui/ActionButton.hpp"

#include "render/Painter.hpp"

namespace qypr {

bool ActionButton::contains(double px, double py) const {
    const double r = diameter / 2.0;
    double dx = px - bounds.cx();
    double dy = py - bounds.cy();
    return dx * dx + dy * dy <= r * r;
}

void ActionButton::setHovered(bool hovered, int64_t now) {
    if (hovered == hovered_) return;
    hovered_ = hovered;
    scale_.animateTo(hovered ? 1.1 : 1.0, theme::anim::medium, ease::outBack);
}

void ActionButton::draw(Painter& p, int64_t now) {
    const bool dim = !enabled;
    if (dim) p.pushGroup();

    const double scale = scale_.value(now);
    const double cx = bounds.cx();
    const double cy = bounds.cy();
    const double r = (diameter / 2.0) * scale;

    Color fill = enabled ? (hovered_ ? theme::color::glassHover : theme::color::glass)
                         : Color::rgba(0.3, 0.3, 0.3, 0.4);
    Color border = enabled ? (hovered_ ? theme::color::primary : theme::color::glassBorder)
                           : theme::color::textMuted;
    Color iconColor =
        enabled ? (hovered_ ? theme::color::primary : theme::color::text) : theme::color::textMuted;

    p.fillCircle(cx, cy, r, fill);
    p.strokeCircle(cx, cy, r, border, 1);

    TextStyle is{iconFamily, iconSize, PANGO_WEIGHT_NORMAL, iconColor};
    Size gs = p.measureText(icon, is);
    p.drawText(cx - gs.w / 2.0, cy - gs.h / 2.0, icon, is, HAlign::Left);

    // Tooltip above the button while hovered.
    if (hovered_ && !label.empty()) {
        TextStyle ts{theme::font::family, 12, PANGO_WEIGHT_NORMAL, theme::color::textSubtle};
        Size ls = p.measureText(label, ts);
        double padW = theme::spacing::medium;
        double padH = theme::spacing::small;
        Rect tip{cx - (ls.w + padW) / 2.0, bounds.y - (ls.h + padH) - 8, ls.w + padW, ls.h + padH};
        p.fillRoundedRect(tip, theme::radius::medium, theme::color::glass);
        p.strokeRoundedRect(tip, theme::radius::medium, theme::color::glassBorder, 1);
        p.drawText(tip.cx(), tip.cy() - ls.h / 2.0, label, ts, HAlign::Center);
    }

    if (dim) p.popGroupWithAlpha(0.4);
}

}  // namespace qypr
