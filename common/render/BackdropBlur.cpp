// BackdropBlur.cpp - see the header. Pixel logic ported verbatim from
// waylaunch's Renderer::set_backdrop/draw_backdrop (box_blur_argb); only the
// state moved into this class and the API follows shared (qypr) conventions.
#include "render/BackdropBlur.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace qypr {

void boxBlurArgb(cairo_surface_t* s, int radius, int passes) {
    if (!s || radius < 1 || passes < 1) return;
    int w = cairo_image_surface_get_width(s);
    int h = cairo_image_surface_get_height(s);
    int stride = cairo_image_surface_get_stride(s);
    cairo_surface_flush(s);
    unsigned char* data = cairo_image_surface_get_data(s);
    if (!data || w <= 0 || h <= 0) return;
    std::vector<unsigned char> tmp(static_cast<size_t>(stride) * h);
    for (int p = 0; p < passes; ++p) {
        for (int y = 0; y < h; ++y) {
            unsigned char* row = data + (static_cast<ptrdiff_t>(y * stride));
            unsigned char* out = tmp.data() + (static_cast<ptrdiff_t>(y * stride));
            for (int x = 0; x < w; ++x) {
                int b = 0;
                int g = 0;
                int r = 0;
                int a = 0;
                int cnt = 0;
                for (int dx = -radius; dx <= radius; ++dx) {
                    int xx = x + dx;
                    if (xx < 0 || xx >= w) continue;
                    unsigned char* px = row + (static_cast<ptrdiff_t>(xx * 4));
                    b += px[0];
                    g += px[1];
                    r += px[2];
                    a += px[3];
                    ++cnt;
                }
                unsigned char* o = out + (static_cast<ptrdiff_t>(x * 4));
                o[0] = b / cnt;
                o[1] = g / cnt;
                o[2] = r / cnt;
                o[3] = a / cnt;
            }
        }
        for (int x = 0; x < w; ++x) {
            for (int y = 0; y < h; ++y) {
                int b = 0;
                int g = 0;
                int r = 0;
                int a = 0;
                int cnt = 0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= h) continue;
                    unsigned char* px = tmp.data() + static_cast<ptrdiff_t>(yy * stride) +
                                        static_cast<ptrdiff_t>(x * 4);
                    b += px[0];
                    g += px[1];
                    r += px[2];
                    a += px[3];
                    ++cnt;
                }
                unsigned char* o =
                    data + static_cast<ptrdiff_t>(y * stride) + static_cast<ptrdiff_t>(x * 4);
                o[0] = b / cnt;
                o[1] = g / cnt;
                o[2] = r / cnt;
                o[3] = a / cnt;
            }
        }
    }
    cairo_surface_mark_dirty(s);
}

namespace {

void roundRectPath(cairo_t* cr, int x, int y, int w, int h, int radius) {
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

}  // namespace

BackdropBlur::~BackdropBlur() {
    if (backdrop_) cairo_surface_destroy(backdrop_);
}

void BackdropBlur::setBackdrop(const uint8_t* data, int width, int height, int stride,
                               uint32_t shmFormat, bool yInvert) {
    if (backdrop_) {
        cairo_surface_destroy(backdrop_);
        backdrop_ = nullptr;
    }
    if (!data || width <= 0 || height <= 0) return;

    // Treat the captured desktop as opaque (RGB24 ignores the 4th byte). The
    // compositor often captures opaque content with alpha=0 in ARGB8888, which as
    // premultiplied ARGB32 would render fully transparent → no visible glass.
    cairo_format_t cfmt;
    if (shmFormat == 0 || shmFormat == 1) cfmt = CAIRO_FORMAT_RGB24;  // A/XRGB8888
    else return;                                                      // unsupported → no glass

    cairo_surface_t* src = cairo_image_surface_create_for_data(const_cast<unsigned char*>(data),
                                                               cfmt, width, height, stride);
    if (!src || cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
        if (src) cairo_surface_destroy(src);
        return;
    }

    const int factor = 8;
    int dw = std::max(1, width / factor);
    int dh = std::max(1, height / factor);
    cairo_surface_t* small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
    cairo_t* cr = cairo_create(small);
    cairo_scale(cr, static_cast<double>(dw) / width, static_cast<double>(dh) / height);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(src);

    boxBlurArgb(small, 2, 3);

    backdrop_ = small;
    backdrop_scale_ = factor;
    backdrop_screen_w_ = width;
    backdrop_screen_h_ = height;
    backdrop_y_invert_ = yInvert;
}

void BackdropBlur::drawBackdrop(cairo_t* cr, int x, int y, int w, int h, int radius) {
    if (!backdrop_ || !cr) return;
    int dw = cairo_image_surface_get_width(backdrop_);
    int dh = cairo_image_surface_get_height(backdrop_);
    if (dw <= 0 || dh <= 0) return;
    double sx = static_cast<double>(backdrop_screen_w_) / dw;
    double sy = static_cast<double>(backdrop_screen_h_) / dh;

    cairo_save(cr);
    roundRectPath(cr, x, y, w, h, radius);
    cairo_clip(cr);
    if (backdrop_y_invert_) {
        cairo_translate(cr, 0, backdrop_screen_h_);
        cairo_scale(cr, sx, -sy);
    } else {
        cairo_scale(cr, sx, sy);
    }
    cairo_set_source_surface(cr, backdrop_, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
}

}  // namespace qypr
