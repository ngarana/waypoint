#include "waylaunch/switcher/thumbnail.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace waylaunch {

void ThumbnailSurfaceDeleter::operator()(cairo_surface_t* surface) const {
    if (surface) cairo_surface_destroy(surface);
}

ThumbnailSurface make_thumbnail(cairo_surface_t* source, bool y_invert, int max_width,
                                int max_height) {
    if (!source || cairo_surface_status(source) != CAIRO_STATUS_SUCCESS || max_width <= 0 ||
        max_height <= 0) {
        return {};
    }

    int const source_width = cairo_image_surface_get_width(source);
    int const source_height = cairo_image_surface_get_height(source);
    if (source_width <= 0 || source_height <= 0) return {};

    double const scale = std::min({
        1.0,
        static_cast<double>(max_width) / source_width,
        static_cast<double>(max_height) / source_height,
    });
    int const width = std::max(1, static_cast<int>(std::floor(source_width * scale)));
    int const height = std::max(1, static_cast<int>(std::floor(source_height * scale)));
    ThumbnailSurface result(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height));
    if (!result || cairo_surface_status(result.get()) != CAIRO_STATUS_SUCCESS) return {};

    cairo_t* cr = cairo_create(result.get());
    if (y_invert) {
        cairo_translate(cr, 0, height);
        cairo_scale(cr, scale, -scale);
    } else {
        cairo_scale(cr, scale, scale);
    }
    cairo_set_source_surface(cr, source, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_status_t const status = cairo_status(cr);
    cairo_destroy(cr);
    cairo_surface_flush(result.get());
    if (status != CAIRO_STATUS_SUCCESS) return {};
    return result;
}

void ThumbnailCache::put(uintptr_t handle_id, ThumbnailSurface surface, double aspect,
                         uint64_t captured_at) {
    if (handle_id == 0 || !surface) return;
    entries_.insert_or_assign(
        handle_id,
        Entry{.surface = std::move(surface), .aspect = aspect, .captured_at = captured_at});
}

const ThumbnailCache::Entry* ThumbnailCache::get(uintptr_t handle_id) const {
    auto const it = entries_.find(handle_id);
    return it == entries_.end() ? nullptr : &it->second;
}

void ThumbnailCache::evict(uintptr_t handle_id) { entries_.erase(handle_id); }

void ThumbnailCache::retain_only(const std::unordered_set<uintptr_t>& live_handles) {
    std::erase_if(entries_,
                  [&live_handles](const auto& item) { return !live_handles.contains(item.first); });
}

} // namespace waylaunch
