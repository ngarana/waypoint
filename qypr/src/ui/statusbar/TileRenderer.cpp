// TileRenderer.cpp - Shared rendering primitives for Quick Settings tiles.
#include "ui/statusbar/TileRenderer.hpp"

#include <cmath>

namespace qypr {

void TileRenderer::drawCard(Painter& p, const Rect& bounds, double radius, const Color& bg,
                            const Color& border, double borderWidth) {
    p.fillRoundedRectSource(bounds, radius, bg);
    if (borderWidth > 0.0 && border.a > 0.0) {
        p.strokeRoundedRectSource(bounds, radius, border, borderWidth);
    }
}

void TileRenderer::drawBadge(Painter& p, double cx, double cy, double r, bool active,
                             const Color& c1, const Color& c2, const Color& inactiveBg) {
    if (active) {
        cairo_t* cr = p.cr();
        cairo_pattern_t* pat = cairo_pattern_create_linear(cx - r, cy - r, cx + r, cy + r);
        cairo_pattern_add_color_stop_rgba(pat, 0.0, c1.r, c1.g, c1.b, c1.a);
        cairo_pattern_add_color_stop_rgba(pat, 1.0, c2.r, c2.g, c2.b, c2.a);
        cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
        cairo_set_source(cr, pat);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
    } else {
        p.fillCircle(cx, cy, r, inactiveBg);
    }
}

void TileRenderer::drawSliderTrack(Painter& p, const Rect& trackRect, double radius,
                                   double fraction, const Color& trackBg, const Color& fillBg) {
    p.fillRoundedRectSource(trackRect, radius, trackBg);
    if (fraction > 0.0) {
        Rect filled = trackRect;
        filled.w = trackRect.w * std::clamp(fraction, 0.0, 1.0);
        p.fillRoundedRectSource(filled, radius, fillBg);
    }
}

void TileRenderer::drawSliderThumb(Painter& p, double cx, double cy, double r, const Color& color) {
    p.fillCircle(cx, cy, r, color);
}

void TileRenderer::drawFocusRing(Painter& p, const Rect& bounds, double radius,
                                 const Color& color) {
    p.strokeRoundedRectSource(bounds, radius, color, 2.0);
}

}  // namespace qypr
