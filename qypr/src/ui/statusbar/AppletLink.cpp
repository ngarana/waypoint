// AppletLink.cpp - The "More from <applet> ›" row of a merged flyout.
#include "ui/statusbar/AppletLink.hpp"

#include "render/Painter.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/PopoverLayout.hpp"

namespace qypr {

namespace {
constexpr const char* kGlyphMenu = "\U000F035C";     // nf-md-menu
constexpr const char* kGlyphChevron = "\U000F0142";  // nf-md-chevron_right
constexpr double kLabelSize = 12.5;
}  // namespace

double AppletLink::width() const {
    const std::string s = text();
    return s.empty() ? 0.0 : popover_layout::estimatedTextWidth(s, kLabelSize) + 64.0;
}

Rect AppletLink::draw(Painter& p, const theme::State& t, const Rect& card, double pad,
                      double hoverX, double hoverY) const {
    const std::string s = text();
    if (s.empty()) return {};
    const double top = card.y + card.h - pad - kHeight;
    p.fillRect({card.x + pad, top + 4.0, card.w - (2.0 * pad), 1.0}, t.colors.text.withAlpha(0.1));
    const Rect row{card.x + pad, top + 8.0, card.w - (2.0 * pad), kHeight - 8.0};
    if (row.contains(hoverX, hoverY)) p.fillRoundedRect(row, 8.0, t.colors.glassHover);

    const TextStyle glyph{.family = t.font.iconFamily,
                          .size = 14.0,
                          .weight = PANGO_WEIGHT_NORMAL,
                          .color = t.colors.textSubtle};
    p.drawText(row.x + 6.0, row.y + ((row.h - 15.0) / 2.0), kGlyphMenu, glyph);
    const TextStyle label{.family = t.font.family,
                          .size = kLabelSize,
                          .weight = PANGO_WEIGHT_NORMAL,
                          .color = t.colors.text};
    p.drawText(row.x + 30.0, row.y + ((row.h - 15.0) / 2.0), s, label, HAlign::Left, row.w - 56.0);
    const Size cz = p.measureText(kGlyphChevron, glyph);
    p.drawText(row.x + row.w - 6.0 - cz.w, row.y + ((row.h - 15.0) / 2.0), kGlyphChevron, glyph);
    return row;
}

}  // namespace qypr
