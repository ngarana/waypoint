#include "waylaunch/renderer.h"
#include <algorithm>
#include <cairo/cairo.h>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <pango/pangocairo.h>
#include <sstream>
#include <string>
#include <string_view>

namespace waylaunch {

namespace {

template <typename Callback>
void with_layout(cairo_t* cr, const RenderFontConfig& font, std::string_view text,
                 Callback&& callback) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, font.family.c_str());
    pango_font_description_set_size(desc, static_cast<int>(font.size * PANGO_SCALE));
    if (font.bold) pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
    if (font.italic) pango_font_description_set_style(desc, PANGO_STYLE_ITALIC);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text.data(), static_cast<int>(text.size()));
    callback(layout);
    pango_font_description_free(desc);
    g_object_unref(layout);
}

} // namespace

Color Color::from_hex(const std::string& hex) {
    Color c;
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() >= 6) {
        c.r = std::stoi(h.substr(0, 2), nullptr, 16) / 255.0;
        c.g = std::stoi(h.substr(2, 2), nullptr, 16) / 255.0;
        c.b = std::stoi(h.substr(4, 2), nullptr, 16) / 255.0;
        c.a = (h.size() >= 8) ? std::stoi(h.substr(6, 2), nullptr, 16) / 255.0 : 1.0;
    }
    return c;
}

Color Color::from_rgba(double r, double g, double b, double a) {
    return {.r = r, .g = g, .b = b, .a = a};
}

struct Renderer::CairoState {
    cairo_surface_t* surface = nullptr;
    cairo_t* cr = nullptr;

    ~CairoState() {
        if (cr) {
            cairo_destroy(cr);
            cr = nullptr;
        }
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
    }
};

Renderer::Renderer() : cairo_(std::make_unique<CairoState>()) {}
Renderer::~Renderer() = default;

void Renderer::set_backdrop(const uint8_t* data, int width, int height, int stride,
                            uint32_t shm_format, bool y_invert) {
    backdrop_.setBackdrop(data, width, height, stride, shm_format, y_invert);
}

bool Renderer::has_backdrop() const { return backdrop_.hasBackdrop(); }

void Renderer::draw_backdrop(int x, int y, int w, int h, int radius) {
    if (!cairo_ || !cairo_->cr) return;
    backdrop_.drawBackdrop(cairo_->cr, x, y, w, h, radius);
}

void Renderer::begin(uint8_t* data, int stride, int width, int height) {
    // Destroy previous state
    cairo_.reset();
    cairo_ = std::make_unique<CairoState>();

    cairo_->surface =
        cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, width, height, stride);
    cairo_->cr = cairo_create(cairo_->surface);
}

void Renderer::end() {
    if (cairo_ && cairo_->surface) { cairo_surface_flush(cairo_->surface); }
    cairo_.reset();
}

void Renderer::clear(const Color& color) {
    cairo_set_operator(cairo_->cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cairo_->cr, color.r, color.g, color.b, color.a);
    cairo_paint(cairo_->cr);
    cairo_set_operator(cairo_->cr, CAIRO_OPERATOR_OVER);
}

void Renderer::fill_rect(int x, int y, int w, int h, const Color& color) {
    cairo_set_source_rgba(cairo_->cr, color.r, color.g, color.b, color.a);
    cairo_rectangle(cairo_->cr, x, y, w, h);
    cairo_fill(cairo_->cr);
}

void Renderer::rounded_rect(int x, int y, int w, int h, int radius, const Color& color) {
    cairo_set_source_rgba(cairo_->cr, color.r, color.g, color.b, color.a);
    round_rect_path(cairo_->cr, x, y, w, h, radius);
    cairo_fill(cairo_->cr);
}

void Renderer::draw_selection_pill(int x, int y, int w, int h, int radius, const Color& accent) {
    rounded_rect(x, y, w, h, radius, Color::from_rgba(accent.r, accent.g, accent.b, 0.28));
    rounded_rect(x, y, w, h, radius, Color::from_rgba(accent.r, accent.g, accent.b, 0.6));
}

void Renderer::draw_text(int x, int y, const std::string& text, const RenderFontConfig& font,
                         const Color& color) {
    with_layout(cairo_->cr, font, text, [&](PangoLayout* layout) {
        cairo_set_source_rgba(cairo_->cr, color.r, color.g, color.b, color.a);
        cairo_move_to(cairo_->cr, x, y);
        pango_cairo_show_layout(cairo_->cr, layout);
    });
}

int Renderer::draw_markup(int x, int y, const std::string& markup, const RenderFontConfig& font,
                          const Color& color, int max_width, int max_lines, bool center) {
    if (!cairo_) return 0;
    int height = 0;
    with_layout(cairo_->cr, font, {}, [&](PangoLayout* layout) {
        if (max_width > 0) pango_layout_set_width(layout, max_width * PANGO_SCALE);
        if (max_width > 0 && center) pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
        if (max_lines == 1) {
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        } else if (max_lines > 1) {
            pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
            pango_layout_set_height(layout, -max_lines);
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        } else if (max_width > 0) {
            pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        }
        // Callers escape user text before adding <span> runs, so the markup is valid.
        pango_layout_set_markup(layout, markup.c_str(), -1);
        cairo_set_source_rgba(cairo_->cr, color.r, color.g, color.b, color.a);
        cairo_move_to(cairo_->cr, x, y);
        pango_cairo_show_layout(cairo_->cr, layout);
        PangoRectangle lr;
        pango_layout_get_pixel_extents(layout, nullptr, &lr);
        height = lr.height;
    });
    return height;
}

void Renderer::draw_search_glyph(int cx, int cy, int size, const Color& color) {
    if (!cairo_) return;
    cairo_t* cr = cairo_->cr;
    double r = size / 2.0;
    cairo_set_line_width(cr, std::max(1.5, size * 0.12));
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

    // lens
    cairo_arc(cr, cx + (r * 0.55), cy + (r * 0.55), r * 0.6, 0, 2 * M_PI);
    cairo_stroke(cr);

    // handle
    cairo_move_to(cr, cx + (r * 1.15), cy + (r * 1.15));
    cairo_line_to(cr, cx + (r * 1.7), cy + (r * 1.7));
    cairo_stroke(cr);
}

cairo_t* Renderer::cr() const { return cairo_ ? cairo_->cr : nullptr; }

void Renderer::round_rect_path(cairo_t* cr, int x, int y, int w, int h, int radius) {
    double r = std::min({static_cast<double>(radius), w / 2.0, h / 2.0});
    if (r < 1.0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

int Renderer::text_width(const std::string& text, const RenderFontConfig& font) {
    if (!cairo_ || text.empty()) return 0;
    int width = 0;
    with_layout(cairo_->cr, font, text, [&](PangoLayout* layout) {
        // Logical (not ink) width so trailing whitespace counts — otherwise the
        // caret wouldn't advance after typing a space and the space would look lost.
        PangoRectangle logical;
        pango_layout_get_pixel_extents(layout, nullptr, &logical);
        width = logical.width;
    });
    return width;
}

int Renderer::text_height(const RenderFontConfig& font) {
    if (!cairo_) return static_cast<int>(font.size);
    int height = 0;
    with_layout(cairo_->cr, font, "Ayg", [&](PangoLayout* layout) {
        PangoRectangle logical;
        pango_layout_get_pixel_extents(layout, nullptr, &logical);
        height = logical.height;
    });
    return height;
}

namespace {

std::string monogram_of(const std::string& label) {
    for (char c : label) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            char buf[2] = {static_cast<char>(std::toupper(static_cast<unsigned char>(c))), 0};
            return {buf};
        }
    }
    return label.empty() ? "#" : label.substr(0, 1);
}

} // namespace

cairo_surface_t* Renderer::load_icon_surface(const std::string& icon_name, int size) {
    if (icon_name.empty()) return nullptr;
    return icons_.get(icon_name, size);
}

void Renderer::draw_icon(int x, int y, int size, const std::string& icon_name,
                         const std::string& label, const Color& accent) {
    if (!cairo_) return;
    cairo_surface_t* surf = load_icon_surface(icon_name, size);

    int radius = std::max(4, size / 5);
    if (!surf) {
        // monogram placeholder: rounded square accent-tinted
        Color bg{.r = accent.r, .g = accent.g, .b = accent.b, .a = 0.18};
        rounded_rect(x, y, size, size, radius, bg);
        Color fg{.r = accent.r, .g = accent.g, .b = accent.b, .a = 1.0};
        std::string m = monogram_of(label.empty() ? icon_name : label);
        RenderFontConfig f{.family = "Sans",
                           .size = static_cast<double>(size) * 0.5,
                           .bold = true,
                           .italic = false};
        with_layout(cairo_->cr, f, m, [&](PangoLayout* layout) {
            // Center by ink extents so the glyph is optically centred in the tile.
            PangoRectangle ink;
            pango_layout_get_pixel_extents(layout, &ink, nullptr);
            cairo_set_source_rgba(cairo_->cr, fg.r, fg.g, fg.b, fg.a);
            cairo_move_to(cairo_->cr, x + ((size - ink.width) / 2.0) - ink.x,
                          y + ((size - ink.height) / 2.0) - ink.y);
            pango_cairo_show_layout(cairo_->cr, layout);
        });
        return;
    }

    // clip to rounded rect and paint the icon scaled to size
    cairo_save(cairo_->cr);
    round_rect_path(cairo_->cr, x, y, size, size, radius);
    cairo_clip(cairo_->cr);
    int sw = cairo_image_surface_get_width(surf);
    int sh = cairo_image_surface_get_height(surf);
    double scale = std::min(static_cast<double>(size) / sw, static_cast<double>(size) / sh);
    double dw = sw * scale;
    double dh = sh * scale;
    cairo_translate(cairo_->cr, x + ((size - dw) / 2), y + ((size - dh) / 2));
    cairo_scale(cairo_->cr, scale, scale);
    cairo_set_source_surface(cairo_->cr, surf, 0, 0);
    cairo_paint(cairo_->cr);
    cairo_restore(cairo_->cr);
}

} // namespace waylaunch
