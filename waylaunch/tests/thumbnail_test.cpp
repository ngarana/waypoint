#include "waylaunch/switcher/thumbnail.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

using namespace waylaunch;

int main() {
    ThumbnailSurface source(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 50));
    assert(source);
    ThumbnailSurface downscaled = make_thumbnail(source.get(), false, 50, 100);
    assert(downscaled);
    assert(cairo_image_surface_get_width(downscaled.get()) == 50);
    assert(cairo_image_surface_get_height(downscaled.get()) == 25);

    ThumbnailSurface gradient(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 2, 4));
    assert(gradient);
    auto* pixels = reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(gradient.get()));
    int const stride = cairo_image_surface_get_stride(gradient.get()) / 4;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 2; ++x) pixels[(y * stride) + x] = 0xff000000U | (y * 0x00010101U);
    }
    cairo_surface_mark_dirty(gradient.get());
    ThumbnailSurface flipped = make_thumbnail(gradient.get(), true, 2, 4);
    assert(flipped);
    auto const* flipped_pixels =
        reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(flipped.get()));
    int const flipped_stride = cairo_image_surface_get_stride(flipped.get()) / 4;
    assert(flipped_pixels[0] == pixels[static_cast<std::ptrdiff_t>(3) * stride]);
    assert(flipped_pixels[static_cast<std::ptrdiff_t>(3) * flipped_stride] == pixels[0]);

    std::cout << "Thumbnail tests passed\n";
}
