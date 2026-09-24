// Painter.hpp - Thin cairo + pango drawing helpers.
//
// Wraps the low-level cairo/pango calls the widgets need (rounded rects,
// circles, gradients, measured/aligned/ellipsised text, text shadows) so no
// widget repeats them (DRY). Holds no state beyond the borrowed cairo context.

#pragma once

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <string>

#include "core/Types.hpp"

namespace qypr {

enum class HAlign { Left, Center, Right };

struct TextStyle {
    std::string family = "Inter";
    double size = 16;
    PangoWeight weight = PANGO_WEIGHT_NORMAL;
    Color color = {1, 1, 1, 1};
};

struct Size {
    double w = 0, h = 0;
};

class Painter {
public:
    explicit Painter(cairo_t* cr) : cr_(cr) {}

    cairo_t* cr() const { return cr_; }

    // Group opacity: draw a whole widget, then composite it at one alpha.
    void pushGroup() { cairo_push_group(cr_); }
    void popGroupWithAlpha(double alpha) {
        cairo_pop_group_to_source(cr_);
        cairo_paint_with_alpha(cr_, alpha);
    }

    // Shapes
    void fillRect(const Rect& r, const Color& c);
    void fillRectSource(const Rect& r, const Color& c);
    void fillRoundedRect(const Rect& r, double radius, const Color& c);
    void fillRoundedRectSource(const Rect& r, double radius, const Color& c);
    void fillCircleSource(double cx, double cy, double radius, const Color& c);
    void strokeRoundedRect(const Rect& r, double radius, const Color& c, double lineWidth);
    void strokeRoundedRectSource(const Rect& r, double radius, const Color& c, double lineWidth);
    // Frosted-glass panel: the translucent `base` fill, a soft light sheen that
    // fades from the top edge (the frosted-glass highlight), a bright hairline
    // along the very top, and the `border` stroke — the shared card look for the
    // status bar strip and every popover. Theme-agnostic: callers pass the glass
    // colours. On a blur-capable compositor (the bar's layer namespace is
    // "qypr-bar") the translucency reads as real frost; without blur the sheen +
    // border still give a glassy panel.
    // `solid` renders an opaque card (no translucency/sheen); qypr passes
    // its theme style flag here so this unit stays theme-free (shared).
    void fillGlass(const Rect& r, double radius, const Color& base, const Color& border,
                   bool solid = false);
    void fillCircle(double cx, double cy, double radius, const Color& c);
    void strokeCircle(double cx, double cy, double radius, const Color& c, double lineWidth);

    // Full-cover vertical 3-stop gradient (the lock background).
    void verticalGradient(int w, int h, const Color& top, const Color& mid, const Color& bottom);

    // Image
    void drawSurface(cairo_surface_t* surface, const Rect& dest);
    // Same as drawSurface, but tints a single-colour alpha-mask surface (freedesktop
    // *-symbolic SVGs render as black silhouettes with alpha = icon shape) to the
    // given colour. Used to recolour symbolic status icons to the bar text colour.
    void drawSurfaceTinted(cairo_surface_t* surface, const Rect& dest, const Color& tint);

    // Text
    Size measureText(const std::string& text, const TextStyle& style, double maxWidth = -1);
    // Measure text constrained to maxWidth using word/character wrapping.
    // Unlike measureText(..., maxWidth), this never ellipsizes the content.
    Size measureTextWrapped(const std::string& text, const TextStyle& style, double maxWidth);
    // Draws text anchored at (x, y): x is left/center/right per align, y is the top.
    void drawText(double x, double y, const std::string& text, const TextStyle& style,
                  HAlign align = HAlign::Left, double maxWidth = -1);
    // Draw wrapped text constrained to maxWidth.
    void drawTextWrapped(double x, double y, const std::string& text, const TextStyle& style,
                         HAlign align, double maxWidth);
    // Same, but paints an offset drop-shadow underneath for readability.
    void drawTextShadowed(double x, double y, const std::string& text, const TextStyle& style,
                          HAlign align, double shadowAlpha, double shadowOffset);

private:
    // Builds a configured, ellipsised layout the caller must g_object_unref.
    PangoLayout* makeLayout(const std::string& text, const TextStyle& style, double maxWidth,
                            bool ellipsize = true);
    static double anchorX(double x, double layoutW, HAlign align);

    cairo_t* cr_;
};

// Append a rounded-rectangle sub-path (radius clamped to half the shorter
// side). Shared neutral primitive (ARCHITECTURE_REVIEW finding 9): Painter's
// fill/strokeRoundedRect and waylaunch's Renderer build on this instead of
// each carrying the four-arc walk. Doubles throughout so both Color models
// (and integer callers) use it without conversion scaffolding.
void roundedRectPath(cairo_t* cr, double x, double y, double w, double h, double radius);

}  // namespace qypr
