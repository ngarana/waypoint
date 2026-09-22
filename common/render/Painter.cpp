#include "render/Painter.hpp"

#include <cmath>
#include <numbers>

namespace qypr {

namespace {
void setSource(cairo_t* cr, const Color& c) {
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}
}  // namespace

void roundedRectPath(cairo_t* cr, double x, double y, double w, double h, double radius) {
    const double rad = std::min(radius, std::min(w, h) / 2.0);
    const double deg = std::numbers::pi / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - rad, y + rad, rad, -90 * deg, 0);
    cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, 90 * deg);
    cairo_arc(cr, x + rad, y + h - rad, rad, 90 * deg, 180 * deg);
    cairo_arc(cr, x + rad, y + rad, rad, 180 * deg, 270 * deg);
    cairo_close_path(cr);
}

void Painter::fillRect(const Rect& r, const Color& c) {
    setSource(cr_, c);
    cairo_rectangle(cr_, r.x, r.y, r.w, r.h);
    cairo_fill(cr_);
}

void Painter::fillRectSource(const Rect& r, const Color& c) {
    cairo_save(cr_);
    cairo_set_operator(cr_, CAIRO_OPERATOR_SOURCE);
    fillRect(r, c);
    cairo_restore(cr_);
}

void Painter::fillRoundedRect(const Rect& r, double radius, const Color& c) {
    roundedRectPath(cr_, r.x, r.y, r.w, r.h, radius);
    setSource(cr_, c);
    cairo_fill(cr_);
}

void Painter::fillRoundedRectSource(const Rect& r, double radius, const Color& c) {
    cairo_save(cr_);
    cairo_set_operator(cr_, CAIRO_OPERATOR_SOURCE);
    fillRoundedRect(r, radius, c);
    cairo_restore(cr_);
}

void Painter::fillCircleSource(double cx, double cy, double radius, const Color& c) {
    cairo_save(cr_);
    cairo_set_operator(cr_, CAIRO_OPERATOR_SOURCE);
    fillCircle(cx, cy, radius, c);
    cairo_restore(cr_);
}

void Painter::strokeRoundedRect(const Rect& r, double radius, const Color& c, double lineWidth) {
    // Inset by half the line width so the stroke stays inside the bounds.
    Rect const inset{.x = r.x + (lineWidth / 2),
                     .y = r.y + (lineWidth / 2),
                     .w = r.w - lineWidth,
                     .h = r.h - lineWidth};
    roundedRectPath(cr_, inset.x, inset.y, inset.w, inset.h, radius);
    setSource(cr_, c);
    cairo_set_line_width(cr_, lineWidth);
    cairo_stroke(cr_);
}

void Painter::strokeRoundedRectSource(const Rect& r, double radius, const Color& c,
                                      double lineWidth) {
    cairo_save(cr_);
    cairo_set_operator(cr_, CAIRO_OPERATOR_SOURCE);
    strokeRoundedRect(r, radius, c, lineWidth);
    cairo_restore(cr_);
}

void Painter::fillGlass(const Rect& r, double radius, const Color& base, const Color& border,
                        bool solid) {
    if (solid) {
        // Solid card: opaque rounded rect with the same hue (no alpha), no
        // sheen, no top highlight — just the border.  The caller still passes
        // the translucent glass colour; we make it opaque here.
        fillRoundedRect(r, radius, base.withAlpha(1.0));
        strokeRoundedRect(r, radius, border, 1.0);
        return;
    }

    double const rad = std::min(radius, std::min(r.w, r.h) / 2.0);

    // 1. Translucent base fill.
    roundedRectPath(cr_, r.x, r.y, r.w, r.h, radius);
    setSource(cr_, base);
    cairo_fill(cr_);

    // 2. Frosted sheen: a soft white highlight fading down from the top edge,
    //    clipped to the rounded shape. This is a light effect, not a theme
    //    colour, so white-with-low-alpha is deliberate.
    cairo_save(cr_);
    roundedRectPath(cr_, r.x, r.y, r.w, r.h, radius);
    cairo_clip(cr_);
    cairo_pattern_t* sheen = cairo_pattern_create_linear(0, r.y, 0, r.y + r.h);
    cairo_pattern_add_color_stop_rgba(sheen, 0.0, 1, 1, 1, 0.10);
    cairo_pattern_add_color_stop_rgba(sheen, 0.35, 1, 1, 1, 0.02);
    cairo_pattern_add_color_stop_rgba(sheen, 1.0, 1, 1, 1, 0.0);
    cairo_set_source(cr_, sheen);
    cairo_rectangle(cr_, r.x, r.y, r.w, r.h);
    cairo_fill(cr_);
    cairo_pattern_destroy(sheen);
    // A crisp 1px highlight along the very top, inset past the corner radius.
    cairo_set_source_rgba(cr_, 1, 1, 1, 0.14);
    cairo_set_line_width(cr_, 1.0);
    cairo_move_to(cr_, r.x + rad, r.y + 0.5);
    cairo_line_to(cr_, r.x + r.w - rad, r.y + 0.5);
    cairo_stroke(cr_);
    cairo_restore(cr_);

    // 3. Hairline border.
    strokeRoundedRect(r, radius, border, 1.0);
}

void Painter::fillCircle(double cx, double cy, double radius, const Color& c) {
    setSource(cr_, c);
    cairo_arc(cr_, cx, cy, radius, 0, 2 * M_PI);
    cairo_fill(cr_);
}

void Painter::strokeCircle(double cx, double cy, double radius, const Color& c, double lineWidth) {
    setSource(cr_, c);
    cairo_set_line_width(cr_, lineWidth);
    cairo_arc(cr_, cx, cy, radius - (lineWidth / 2), 0, 2 * M_PI);
    cairo_stroke(cr_);
}

void Painter::verticalGradient(int w, int h, const Color& top, const Color& mid,
                               const Color& bottom) {
    cairo_pattern_t* g = cairo_pattern_create_linear(0, 0, 0, h);
    cairo_pattern_add_color_stop_rgba(g, 0.0, top.r, top.g, top.b, top.a);
    cairo_pattern_add_color_stop_rgba(g, 0.5, mid.r, mid.g, mid.b, mid.a);
    cairo_pattern_add_color_stop_rgba(g, 1.0, bottom.r, bottom.g, bottom.b, bottom.a);
    cairo_rectangle(cr_, 0, 0, w, h);
    cairo_set_source(cr_, g);
    cairo_fill(cr_);
    cairo_pattern_destroy(g);
}

void Painter::drawSurface(cairo_surface_t* surface, const Rect& dest) {
    if (surface == nullptr) { return; }
    int const sw = cairo_image_surface_get_width(surface);
    int const sh = cairo_image_surface_get_height(surface);
    if (sw <= 0 || sh <= 0) { return; }

    double const scale = std::min(dest.w / sw, dest.h / sh);
    double const dw = sw * scale;
    double const dh = sh * scale;
    double const dx = dest.x + ((dest.w - dw) / 2.0);
    double const dy = dest.y + ((dest.h - dh) / 2.0);

    cairo_save(cr_);
    cairo_rectangle(cr_, dest.x, dest.y, dest.w, dest.h);
    cairo_clip(cr_);
    cairo_translate(cr_, dx, dy);
    cairo_scale(cr_, scale, scale);
    cairo_set_source_surface(cr_, surface, 0, 0);
    cairo_paint(cr_);
    cairo_restore(cr_);
}

void Painter::drawSurfaceTinted(cairo_surface_t* surface, const Rect& dest, const Color& tint) {
    if (surface == nullptr) { return; }
    int const sw = cairo_image_surface_get_width(surface);
    int const sh = cairo_image_surface_get_height(surface);
    if (sw <= 0 || sh <= 0) { return; }

    double const scale = std::min(dest.w / sw, dest.h / sh);
    double const dw = sw * scale;
    double const dh = sh * scale;
    double const dx = dest.x + ((dest.w - dw) / 2.0);
    double const dy = dest.y + ((dest.h - dh) / 2.0);

    cairo_save(cr_);
    cairo_rectangle(cr_, dest.x, dest.y, dest.w, dest.h);
    cairo_clip(cr_);
    cairo_translate(cr_, dx, dy);
    cairo_scale(cr_, scale, scale);
    // Use the surface as a mask: paint the tint colour through the surface's
    // alpha channel. Symbolic SVGs are black-with-alpha, so this replaces the
    // black with `tint` while preserving the icon shape — the standard way to
    // recolour a freedesktop symbolic icon to a UI foreground colour.
    cairo_set_source_rgba(cr_, tint.r, tint.g, tint.b, tint.a);
    cairo_mask_surface(cr_, surface, 0, 0);
    cairo_restore(cr_);
}

PangoLayout* Painter::makeLayout(const std::string& text, const TextStyle& style, double maxWidth) {
    PangoLayout* layout = pango_cairo_create_layout(cr_);
    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, style.family.c_str());
    pango_font_description_set_absolute_size(desc, style.size * PANGO_SCALE);
    pango_font_description_set_weight(desc, style.weight);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    pango_layout_set_text(layout, text.c_str(), -1);
    if (maxWidth > 0) {
        pango_layout_set_width(layout, static_cast<int>(maxWidth) * PANGO_SCALE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    }
    return layout;
}

double Painter::anchorX(double x, double layoutW, HAlign align) {
    switch (align) {
        case HAlign::Center:
            return x - (layoutW / 2.0);
        case HAlign::Right:
            return x - layoutW;
        case HAlign::Left:
        default:
            return x;
    }
}

Size Painter::measureText(const std::string& text, const TextStyle& style, double maxWidth) {
    PangoLayout* layout = makeLayout(text, style, maxWidth);
    int w;
    int h;
    pango_layout_get_pixel_size(layout, &w, &h);
    g_object_unref(layout);
    return {.w = static_cast<double>(w), .h = static_cast<double>(h)};
}

void Painter::drawText(double x, double y, const std::string& text, const TextStyle& style,
                       HAlign align, double maxWidth) {
    PangoLayout* layout = makeLayout(text, style, maxWidth);
    int w;
    int h;
    pango_layout_get_pixel_size(layout, &w, &h);
    cairo_move_to(cr_, anchorX(x, w, align), y);
    setSource(cr_, style.color);
    pango_cairo_show_layout(cr_, layout);
    g_object_unref(layout);
}

void Painter::drawTextShadowed(double x, double y, const std::string& text, const TextStyle& style,
                               HAlign align, double shadowAlpha, double shadowOffset) {
    PangoLayout* layout = makeLayout(text, style, -1);
    int w;
    int h;
    pango_layout_get_pixel_size(layout, &w, &h);
    double const ax = anchorX(x, w, align);

    // The drop shadow is an offset black copy; a zero/negative alpha means the
    // caller asked for no shadow (light palettes disable it) — skip the pass
    // so it cannot leave a transparent-but-rendered residue.
    if (shadowAlpha > 0.0) {
        cairo_move_to(cr_, ax + shadowOffset, y + shadowOffset);
        cairo_set_source_rgba(cr_, 0, 0, 0, shadowAlpha);
        pango_cairo_show_layout(cr_, layout);
    }

    cairo_move_to(cr_, ax, y);
    setSource(cr_, style.color);
    pango_cairo_show_layout(cr_, layout);
    g_object_unref(layout);
}

}  // namespace qypr
