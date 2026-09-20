// blur_test.cpp - Unit tests for shared render/BackdropBlur.
// Assert-based, like the consumers' suites (fails loud, no framework).
#include "render/BackdropBlur.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

cairo_surface_t* make_surface(int w, int h) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    assert(cairo_surface_status(s) == CAIRO_STATUS_SUCCESS);
    return s;
}

void fill_half(cairo_surface_t* s) {
    // Left half black, right half white (opaque).
    int w = cairo_image_surface_get_width(s);
    int h = cairo_image_surface_get_height(s);
    int stride = cairo_image_surface_get_stride(s);
    unsigned char* data = cairo_image_surface_get_data(s);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            unsigned char* px = data + y * stride + x * 4;
            unsigned char v = (x < w / 2) ? 0 : 255;
            px[0] = px[1] = px[2] = v;
            px[3] = 255;
        }
    }
    cairo_surface_mark_dirty(s);
}

unsigned char pixel_r(cairo_surface_t* s, int x, int y) {
    int stride = cairo_image_surface_get_stride(s);
    unsigned char* data = cairo_image_surface_get_data(s);
    return (data + y * stride + x * 4)[2];
}

void test_blur_noop_guards() {
    cairo_surface_t* s = make_surface(8, 8);
    fill_half(s);
    // radius < 1 or passes < 1: surface untouched.
    qypr::boxBlurArgb(s, 0, 3);
    assert(pixel_r(s, 0, 0) == 0);
    assert(pixel_r(s, 7, 0) == 255);
    qypr::boxBlurArgb(s, 2, 0);
    assert(pixel_r(s, 0, 0) == 0);
    cairo_surface_destroy(s);
    // Null surface: must not crash.
    qypr::boxBlurArgb(nullptr, 2, 3);
    printf("[PASS] blur no-op guards\n");
}

void test_blur_softens_step_edge() {
    cairo_surface_t* s = make_surface(16, 4);
    fill_half(s);
    qypr::boxBlurArgb(s, 2, 3);
    // Interior far from the edge is unchanged...
    assert(pixel_r(s, 0, 0) == 0);
    assert(pixel_r(s, 15, 0) == 255);
    // ...but pixels adjacent to the step are strictly intermediate.
    unsigned char left = pixel_r(s, 7, 0);
    unsigned char right = pixel_r(s, 8, 0);
    assert(left > 0 && left < 255);
    assert(right > 0 && right < 255);
    // Symmetry of the kernel around a centred step (integer-division
    // truncation accumulates over passes; tolerance stays tight enough to
    // catch a one-sided kernel bug).
    assert(std::abs(static_cast<int>(left) + static_cast<int>(right) - 255) <= 6);
    cairo_surface_destroy(s);
    printf("[PASS] blur softens step edge\n");
}

void test_backdrop_rejects_bad_input() {
    qypr::BackdropBlur blur;
    assert(!blur.hasBackdrop());
    blur.setBackdrop(nullptr, 64, 64, 256, 0, false);
    assert(!blur.hasBackdrop());
    std::vector<unsigned char> pixels(64 * 64 * 4, 128);
    blur.setBackdrop(pixels.data(), 0, 64, 256, 0, false);
    assert(!blur.hasBackdrop());
    blur.setBackdrop(pixels.data(), 64, 64, 256, /*shmFormat=*/2, false);
    assert(!blur.hasBackdrop());
    printf("[PASS] backdrop rejects bad input\n");
}

void test_backdrop_paints_glass() {
    // 64x64 mid-grey capture → blurred cache exists and paints onto a
    // target without touching pixels outside the rounded-rect clip.
    std::vector<unsigned char> pixels(64 * 64 * 4, 128);
    qypr::BackdropBlur blur;
    blur.setBackdrop(pixels.data(), 64, 64, 64 * 4, /*shmFormat=*/0, false);
    assert(blur.hasBackdrop());

    cairo_surface_t* target = make_surface(64, 64);
    cairo_t* cr = cairo_create(target);
    cairo_set_source_rgb(cr, 1, 0, 0);  // red background: clip leaks show up
    cairo_paint(cr);
    blur.drawBackdrop(cr, 8, 8, 32, 32, 6);
    cairo_destroy(cr);

    cairo_surface_flush(target);
    // Corner (outside the rounded rect) is still pure red.
    assert(pixel_r(target, 0, 0) == 255);
    // Centre of the painted rect is the grey glass, not red.
    unsigned char centre = pixel_r(target, 24, 24);
    assert(centre < 200);
    cairo_surface_destroy(target);
    printf("[PASS] backdrop paints clipped glass\n");
}

}  // namespace

int main() {
    test_blur_noop_guards();
    test_blur_softens_step_edge();
    test_backdrop_rejects_bad_input();
    test_backdrop_paints_glass();
    printf("All BackdropBlur unit tests passed successfully!\n");
    return 0;
}
