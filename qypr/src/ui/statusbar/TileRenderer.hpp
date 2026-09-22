// TileRenderer.hpp - Shared rendering primitives for Quick Settings tiles.
#pragma once

#include "core/Types.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"

namespace qypr {

class TileRenderer {
public:
    static constexpr double kTileRadius = 12.0;
    static constexpr double kSectionRadius = 14.0;

    static void drawCard(Painter& p, const Rect& bounds, double radius, const Color& bg,
                         const Color& border, double borderWidth = 1.0);

    static void drawBadge(Painter& p, double cx, double cy, double r, bool active, const Color& c1,
                          const Color& c2, const Color& inactiveBg);

    static void drawSliderTrack(Painter& p, const Rect& trackRect, double radius, double fraction,
                                const Color& trackBg, const Color& fillBg);

    static void drawSliderThumb(Painter& p, double cx, double cy, double r, const Color& color);

    static void drawFocusRing(Painter& p, const Rect& bounds, double radius, const Color& color);
};

}  // namespace qypr
