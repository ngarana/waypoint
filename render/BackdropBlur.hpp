// BackdropBlur.hpp - Client-side frosted-glass backdrop.
//
// Downsampled + separable-box-blurred copy of captured screen pixels, painted
// clipped to a rounded rect. Extracted from waylaunch's Renderer (screencopy
// path); qypr's bar uses compositor blur instead, so this unit is exercised by
// its unit test here and awaits waylaunch's adoption of shared render.
// Cairo-only; no compositor or theme dependencies.

#pragma once

#include <cstdint>

#include <cairo/cairo.h>

namespace qypr {

// Separable box blur over a small ARGB32 image surface, in place.
void boxBlurArgb(cairo_surface_t* surface, int radius, int passes);

class BackdropBlur {
public:
    BackdropBlur() = default;
    ~BackdropBlur();
    BackdropBlur(const BackdropBlur&) = delete;
    BackdropBlur& operator=(const BackdropBlur&) = delete;

    // Capture full-output pixels (`shmFormat`: 0 = ARGB8888, 1 = XRGB8888 —
    // wl_shm enum values; both treated as opaque RGB24 since compositors
    // often capture alpha=0 on opaque content). Rebuilds the blurred cache;
    // drops it on bad input or unsupported formats.
    void setBackdrop(const uint8_t* data, int width, int height, int stride, uint32_t shmFormat,
                     bool yInvert);
    bool hasBackdrop() const { return backdrop_ != nullptr; }

    // Paint the blurred copy clipped to a rounded rect at screen (x, y, w, h),
    // sampling the backdrop at those screen coordinates, onto `cr`.
    void drawBackdrop(cairo_t* cr, int x, int y, int w, int h, int radius);

private:
    cairo_surface_t* backdrop_ = nullptr;  // downsampled + blurred
    int backdrop_scale_ = 1;               // downsample factor
    int backdrop_screen_w_ = 0;
    int backdrop_screen_h_ = 0;
    bool backdrop_y_invert_ = false;
};

}  // namespace qypr
