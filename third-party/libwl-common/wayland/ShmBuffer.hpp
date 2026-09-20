// ShmBuffer.hpp - A wl_shm buffer wrapped as a cairo image surface.
//
// One CPU-side ARGB32 buffer that Wayland can scan out and cairo can paint
// into. Outputs keep a couple of these and reuse whichever the compositor
// has released, giving lock-free double buffering with minimal memory.

#pragma once

#include <cairo/cairo.h>
#include <wayland-client.h>

#include <memory>

namespace qypr {

class ShmBuffer {
public:
    static std::unique_ptr<ShmBuffer> create(wl_shm* shm, int width, int height);
    ~ShmBuffer();

    ShmBuffer(const ShmBuffer&) = delete;
    ShmBuffer& operator=(const ShmBuffer&) = delete;

    wl_buffer* buffer() const { return buffer_; }
    cairo_surface_t* cairoSurface() const { return cairo_; }
    int width() const { return width_; }
    int height() const { return height_; }

    bool busy() const { return busy_; }
    void markBusy() { busy_ = true; }

    int drawnHeight() const { return drawnHeight_; }
    void setDrawnHeight(int h) { drawnHeight_ = h; }

    // Wayland C callback (public so the listener table can bind it).
    static void handleRelease(void* data, wl_buffer* buffer);

private:
    ShmBuffer() = default;

    wl_buffer* buffer_ = nullptr;
    cairo_surface_t* cairo_ = nullptr;
    void* data_ = nullptr;
    size_t size_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool busy_ = false;
    int drawnHeight_ = 0;
};

}  // namespace qypr
