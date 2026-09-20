#include "wayland/Cursor.hpp"

#include <wayland-cursor.h>

namespace qypr {

Cursor::Cursor(wl_compositor* compositor, wl_shm* shm, int size) {
    // nullptr theme name -> honour XCURSOR_THEME / the compositor default.
    theme_ = wl_cursor_theme_load(nullptr, size, shm);
    if (!theme_) return;

    cursor_ = wl_cursor_theme_get_cursor(theme_, "left_ptr");
    if (!cursor_) cursor_ = wl_cursor_theme_get_cursor(theme_, "default");
    if (!cursor_ || cursor_->image_count == 0) return;

    wl_cursor_image* image = cursor_->images[0];
    wl_buffer* buffer = wl_cursor_image_get_buffer(image);
    if (!buffer) return;

    surface_ = wl_compositor_create_surface(compositor);
    hotspotX_ = static_cast<int32_t>(image->hotspot_x);
    hotspotY_ = static_cast<int32_t>(image->hotspot_y);
    wl_surface_attach(surface_, buffer, 0, 0);
    wl_surface_damage(surface_, 0, 0, image->width, image->height);
    wl_surface_commit(surface_);
}

Cursor::~Cursor() {
    if (surface_) wl_surface_destroy(surface_);
    if (theme_) wl_cursor_theme_destroy(theme_);  // owns cursor_ and its buffers
}

void Cursor::apply(wl_pointer* pointer, uint32_t serial) {
    if (!surface_) return;
    wl_pointer_set_cursor(pointer, serial, surface_, hotspotX_, hotspotY_);
}

}  // namespace qypr
