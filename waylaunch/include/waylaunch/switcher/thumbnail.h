#pragma once

#include <cairo/cairo.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace waylaunch {

struct ThumbnailSurfaceDeleter {
    void operator()(cairo_surface_t* surface) const;
};

using ThumbnailSurface = std::unique_ptr<cairo_surface_t, ThumbnailSurfaceDeleter>;

ThumbnailSurface make_thumbnail(cairo_surface_t* source, bool y_invert, int max_width,
                                int max_height);

class ThumbnailCache {
  public:
    struct Entry {
        ThumbnailSurface surface;
        double aspect = 1.0;
        uint64_t captured_at = 0;
    };

    void put(uintptr_t handle_id, ThumbnailSurface surface, double aspect, uint64_t captured_at);
    const Entry* get(uintptr_t handle_id) const;
    void evict(uintptr_t handle_id);
    void retain_only(const std::unordered_set<uintptr_t>& live_handles);
    size_t size() const { return entries_.size(); }

  private:
    std::unordered_map<uintptr_t, Entry> entries_;
};

} // namespace waylaunch
