// Cursor.hpp - Pointer cursor for the lock surfaces.
//
// A Wayland client must supply its own cursor image; if it never calls
// wl_pointer.set_cursor the compositor shows no pointer at all over our
// surfaces. This loads the system default pointer from the XCursor theme and
// applies it to the seat's pointer on enter.

#pragma once

#include <wayland-client.h>

#include <cstdint>

struct wl_cursor_theme;
struct wl_cursor;

namespace qypr {

class Cursor {
public:
    Cursor(wl_compositor* compositor, wl_shm* shm, int size = 24);
    ~Cursor();

    Cursor(const Cursor&) = delete;
    Cursor& operator=(const Cursor&) = delete;

    bool valid() const { return surface_ != nullptr; }

    // Point the given pointer at our cursor image for this enter serial.
    void apply(wl_pointer* pointer, uint32_t serial);

private:
    wl_surface* surface_ = nullptr;
    wl_cursor_theme* theme_ = nullptr;
    wl_cursor* cursor_ = nullptr;
    int32_t hotspotX_ = 0;
    int32_t hotspotY_ = 0;
};

}  // namespace qypr
