#include "waylaunch/switcher/hyprland_toplevel_export.h"
#include "waylaunch/switcher/wlr_toplevel_backend.h"
#include <utility>

#ifdef HAS_TOPLEVEL_EXPORT
#include "hyprland-toplevel-export-v1-client-protocol.h"
#include "wayland/ShmBuffer.hpp"
#include "waylaunch/wayland_core.h"
#include <algorithm>
#include <climits>
#include <unordered_map>
#include <vector>
#include <wayland-client.h>
#endif

namespace waylaunch {

#ifdef HAS_TOPLEVEL_EXPORT

struct HyprlandToplevelExportBackend::Impl {
    struct BufferInfo {
        uint32_t format;
        uint32_t width;
        uint32_t height;
        uint32_t stride;
    };

    struct CachedBuffer {
        std::unique_ptr<qypr::ShmBuffer> shm;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t stride = 0;
        uint32_t format = 0;
    };

    struct Capture {
        uintptr_t handle_id = 0;
        hyprland_toplevel_export_frame_v1* frame = nullptr;
        std::vector<BufferInfo> buffers;
        bool y_invert = false;
    };

    Impl(WaylandCore* core, WlrForeignToplevelBackend* windows)
        : manager(core ? core->toplevel_export_manager() : nullptr),
          shm(core ? core->shm() : nullptr), toplevels(windows) {}

    bool available() const { return manager && shm && toplevels; }

    static void buffer_cb(void* data, hyprland_toplevel_export_frame_v1* frame, uint32_t format,
                          uint32_t width, uint32_t height, uint32_t stride) {
        auto* self = static_cast<Impl*>(data);
        auto const it = self->captures.find(frame);
        if (it != self->captures.end()) {
            it->second->buffers.push_back({
                .format = format,
                .width = width,
                .height = height,
                .stride = stride,
            });
        }
    }

    static void damage_cb(void*, hyprland_toplevel_export_frame_v1*, uint32_t, uint32_t, uint32_t,
                          uint32_t) {}

    static void flags_cb(void* data, hyprland_toplevel_export_frame_v1* frame, uint32_t flags) {
        auto* self = static_cast<Impl*>(data);
        auto const it = self->captures.find(frame);
        if (it != self->captures.end()) {
            it->second->y_invert = (flags & HYPRLAND_TOPLEVEL_EXPORT_FRAME_V1_FLAGS_Y_INVERT) != 0U;
        }
    }

    static void ready_cb(void* data, hyprland_toplevel_export_frame_v1* frame, uint32_t, uint32_t,
                         uint32_t) {
        static_cast<Impl*>(data)->finish(frame, true);
    }

    static void failed_cb(void* data, hyprland_toplevel_export_frame_v1* frame) {
        static_cast<Impl*>(data)->finish(frame, false);
    }

    static void linux_dmabuf_cb(void*, hyprland_toplevel_export_frame_v1*, uint32_t, uint32_t,
                                uint32_t) {}

    static void buffer_done_cb(void* data, hyprland_toplevel_export_frame_v1* frame) {
        static_cast<Impl*>(data)->copy(frame);
    }

    static const hyprland_toplevel_export_frame_v1_listener* listener() {
        static const hyprland_toplevel_export_frame_v1_listener value = {
            .buffer = buffer_cb,
            .damage = damage_cb,
            .flags = flags_cb,
            .ready = ready_cb,
            .failed = failed_cb,
            .linux_dmabuf = linux_dmabuf_cb,
            .buffer_done = buffer_done_cb,
        };
        return &value;
    }

    void request(uintptr_t handle_id, const Sink& sink) {
        if (!available()) {
            if (sink) sink(handle_id, nullptr);
            return;
        }
        auto* handle = toplevels->find_handle(handle_id);
        if (!handle) {
            if (sink) sink(handle_id, nullptr);
            return;
        }
        auto capture = std::make_unique<Capture>();
        capture->handle_id = handle_id;
        capture->frame =
            hyprland_toplevel_export_manager_v1_capture_toplevel_with_wlr_toplevel_handle(
                manager, 0, handle);
        if (!capture->frame) {
            if (sink) sink(handle_id, nullptr);
            return;
        }
        auto* frame = capture->frame;
        captures.emplace(frame, std::move(capture));
        if (hyprland_toplevel_export_frame_v1_add_listener(frame, listener(), this) != 0) {
            hyprland_toplevel_export_frame_v1_destroy(frame);
            captures.erase(frame);
            if (sink) sink(handle_id, nullptr);
        }
    }

    void copy(hyprland_toplevel_export_frame_v1* frame) {
        auto const capture_it = captures.find(frame);
        if (capture_it == captures.end()) return;
        Capture& capture = *capture_it->second;
        for (const BufferInfo& info : capture.buffers) {
            cairo_format_t const format = qypr::ShmBuffer::cairoFormatForWlShmFormat(info.format);
            if (format == CAIRO_FORMAT_INVALID || info.width == 0 || info.height == 0 ||
                info.width > static_cast<uint32_t>(INT_MAX) ||
                info.height > static_cast<uint32_t>(INT_MAX) ||
                info.stride > static_cast<uint32_t>(INT_MAX)) {
                continue;
            }
            CachedBuffer& cached = buffers[capture.handle_id];
            if (!cached.shm || cached.width != info.width || cached.height != info.height ||
                cached.stride != info.stride || cached.format != info.format) {
                cached.shm = qypr::ShmBuffer::create(shm, static_cast<int>(info.width),
                                                     static_cast<int>(info.height),
                                                     static_cast<int>(info.stride), info.format);
                cached.width = info.width;
                cached.height = info.height;
                cached.stride = info.stride;
                cached.format = info.format;
            }
            if (!cached.shm) continue;
            hyprland_toplevel_export_frame_v1_copy(frame, cached.shm->buffer(), 1);
            return;
        }
        finish(frame, false);
    }

    void finish(hyprland_toplevel_export_frame_v1* frame, bool success) {
        auto it = captures.find(frame);
        if (it == captures.end()) return;
        std::unique_ptr<Capture> capture = std::move(it->second);
        captures.erase(it);
        auto const buffer_it = buffers.find(capture->handle_id);
        if (success && buffer_it != buffers.end() && buffer_it->second.shm) {
            cairo_surface_t* surface = buffer_it->second.shm->cairoSurface();
            cairo_surface_mark_dirty(surface);
            if (sink) {
                CapturedFrame result{.source = surface, .y_invert = capture->y_invert};
                sink(capture->handle_id, &result);
            }
        } else if (sink) {
            sink(capture->handle_id, nullptr);
        }
        hyprland_toplevel_export_frame_v1_destroy(frame);
    }

    void cancel(uintptr_t handle_id, bool evict_buffer) {
        for (auto it = captures.begin(); it != captures.end();) {
            if (it->second->handle_id != handle_id) {
                ++it;
                continue;
            }
            auto* frame = it->first;
            hyprland_toplevel_export_frame_v1_destroy(frame);
            it = captures.erase(it);
        }
        if (evict_buffer) buffers.erase(handle_id);
    }

    void cancel_all() {
        for (auto& [frame, capture] : captures) {
            (void) capture;
            hyprland_toplevel_export_frame_v1_destroy(frame);
        }
        captures.clear();
    }

    hyprland_toplevel_export_manager_v1* manager = nullptr;
    wl_shm* shm = nullptr;
    WlrForeignToplevelBackend* toplevels = nullptr;
    Sink sink;
    std::unordered_map<hyprland_toplevel_export_frame_v1*, std::unique_ptr<Capture>> captures;
    std::unordered_map<uintptr_t, CachedBuffer> buffers;
};

HyprlandToplevelExportBackend::HyprlandToplevelExportBackend(WaylandCore* wayland,
                                                             WlrForeignToplevelBackend* toplevels)
    : impl_(std::make_unique<Impl>(wayland, toplevels)) {}

HyprlandToplevelExportBackend::~HyprlandToplevelExportBackend() { cancel_all(); }

bool HyprlandToplevelExportBackend::available() const { return impl_ && impl_->available(); }

void HyprlandToplevelExportBackend::set_sink(Sink sink) {
    if (impl_) impl_->sink = std::move(sink);
}

void HyprlandToplevelExportBackend::request(uintptr_t handle_id) {
    if (impl_) impl_->request(handle_id, impl_->sink);
}

void HyprlandToplevelExportBackend::cancel(uintptr_t handle_id) {
    if (impl_) impl_->cancel(handle_id, true);
}

void HyprlandToplevelExportBackend::cancel_all() {
    if (impl_) impl_->cancel_all();
}

#else

struct HyprlandToplevelExportBackend::Impl {};

HyprlandToplevelExportBackend::HyprlandToplevelExportBackend(WaylandCore*,
                                                             WlrForeignToplevelBackend*)
    : impl_(std::make_unique<Impl>()) {}

HyprlandToplevelExportBackend::~HyprlandToplevelExportBackend() = default;
bool HyprlandToplevelExportBackend::available() const { return false; }
void HyprlandToplevelExportBackend::set_sink(Sink) {}
void HyprlandToplevelExportBackend::request(uintptr_t) {}
void HyprlandToplevelExportBackend::cancel(uintptr_t) {}
void HyprlandToplevelExportBackend::cancel_all() {}

#endif

} // namespace waylaunch
