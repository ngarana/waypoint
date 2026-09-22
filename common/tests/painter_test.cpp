// painter_test.cpp - Unit tests for the shared rounded-rect path.
// Assert-based, like the other shared suites. Paints through
// qypr::roundedRectPath and checks pixels: interior filled, far corner
// untouched, small radii degrading to (near-)rectangles.
#include "render/Painter.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

#include <cairo/cairo.h>

namespace {

cairo_surface_t* make_surface() {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 40, 40);
    assert(cairo_surface_status(s) == CAIRO_STATUS_SUCCESS);
    return s;
}

uint32_t pixel_at(cairo_surface_t* s, int x, int y) {
    cairo_surface_flush(s);
    auto* data = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s);
    auto* row = reinterpret_cast<uint32_t*>(data + y * stride);
    return row[x];
}

void fill_path_red(cairo_t* cr) {
    cairo_set_source_rgba(cr, 1, 0, 0, 1);
    cairo_fill(cr);
}

void test_interior_filled_corner_kept() {
    cairo_surface_t* s = make_surface();
    cairo_t* cr = cairo_create(s);
    qypr::roundedRectPath(cr, 4, 4, 32, 32, 8);
    fill_path_red(cr);
    cairo_destroy(cr);
    // Centre is red; the far corner outside the arc stays transparent.
    assert((pixel_at(s, 20, 20) & 0x00ffffff) == 0x00ff0000);
    assert((pixel_at(s, 0, 0) >> 24) == 0);
    // Just inside the arc start is red too (path is closed and filled).
    assert((pixel_at(s, 6, 20) & 0x00ffffff) == 0x00ff0000);
    cairo_surface_destroy(s);
    std::printf("[PASS] interior filled corner kept\n");
}

void test_zero_radius_is_rectangle() {
    cairo_surface_t* s = make_surface();
    cairo_t* cr = cairo_create(s);
    qypr::roundedRectPath(cr, 4, 4, 32, 32, 0);
    fill_path_red(cr);
    cairo_destroy(cr);
    // Sharp corners: the rect corner pixel itself is painted.
    assert((pixel_at(s, 4, 4) & 0x00ffffff) == 0x00ff0000);
    cairo_surface_destroy(s);
    std::printf("[PASS] zero radius is rectangle\n");
}

void test_radius_clamped() {
    cairo_surface_t* s = make_surface();
    cairo_t* cr = cairo_create(s);
    // Absurd radius clamps to half the shorter side instead of exploding.
    qypr::roundedRectPath(cr, 4, 4, 32, 32, 1000);
    fill_path_red(cr);
    cairo_destroy(cr);
    assert((pixel_at(s, 20, 20) & 0x00ffffff) == 0x00ff0000);
    cairo_surface_destroy(s);
    std::printf("[PASS] radius clamped\n");
}

}  // namespace

int main() {
    test_interior_filled_corner_kept();
    test_zero_radius_is_rectangle();
    test_radius_clamped();
    printf("All rounded-rect path unit tests passed successfully!\n");
    return 0;
}
