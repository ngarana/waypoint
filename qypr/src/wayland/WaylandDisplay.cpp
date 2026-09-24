#include "wayland/WaylandDisplay.hpp"

#include "ext-session-lock-v1-client-protocol.h"

#include <algorithm>
#include <cstring>

#include "core/EventLoop.hpp"

namespace qypr {

namespace {
const wl_registry_listener kRegistryListener = {
    .global = WaylandDisplay::onGlobal,
    .global_remove = WaylandDisplay::onGlobalRemove,
};
}  // namespace

WaylandDisplay::WaylandDisplay(EventLoop& loop) : loop_(loop) {}

WaylandDisplay::~WaylandDisplay() {
    outputs_.clear();
    seat_.reset();
    if (lockManager_) ext_session_lock_manager_v1_destroy(lockManager_);
    if (compositor_) wl_compositor_destroy(compositor_);
    if (shm_) wl_shm_destroy(shm_);
    if (registry_) wl_registry_destroy(registry_);
    if (display_) wl_display_disconnect(display_);
}

bool WaylandDisplay::connect() {
    display_ = wl_display_connect(nullptr);
    if (!display_) return false;

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);

    wl_display_roundtrip(display_);  // bind globals
    wl_display_roundtrip(display_);  // output/seat sub-events (scale, caps, keymap)

    if (!compositor_ || !shm_ || !lockManager_) return false;

    // Integrate the Wayland socket into the loop; flush before every wait.
    loop_.addPrepare([this] { flush(); });
    loop_.addFd(wl_display_get_fd(display_), [this](uint32_t) {
        if (wl_display_dispatch(display_) < 0) loop_.quit();
    });
    return true;
}

void WaylandDisplay::flush() {
    wl_display_dispatch_pending(display_);
    wl_display_flush(display_);
}

void WaylandDisplay::roundtrip() {
    if (display_) {
        wl_display_roundtrip(display_);
        // Configure callbacks may queue the initial lock-surface commit while
        // the roundtrip is dispatching. Send that frame before startup work
        // outside the event loop continues.
        flush();
    }
}

void WaylandDisplay::setInputSink(InputSink* sink) {
    sink_ = sink;
    if (seat_) seat_->setSink(sink);
}

void WaylandDisplay::createLockSurfaces(ext_session_lock_v1* lock) {
    for (auto& out : outputs_) out->createLockSurface(lock);
}

void WaylandDisplay::invalidateAll() {
    for (auto& out : outputs_) out->invalidate();
}

Output* WaylandDisplay::outputForSurface(wl_surface* surface) {
    for (auto& out : outputs_) {
        if (out->surface() == surface) return out.get();
    }
    return nullptr;
}

void WaylandDisplay::onGlobal(void* data, wl_registry* registry, uint32_t name,
                              const char* interface, uint32_t version) {
    auto* self = static_cast<WaylandDisplay*>(data);

    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        self->compositor_ = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4U)));
        self->env_.compositor = self->compositor_;
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        self->shm_ = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        self->env_.shm = self->shm_;
    } else if (std::strcmp(interface, ext_session_lock_manager_v1_interface.name) == 0) {
        self->lockManager_ = static_cast<ext_session_lock_manager_v1*>(
            wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        auto* seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7U)));
        self->seat_ = std::make_unique<Seat>(seat, self->loop_, &self->env_);
        self->seat_->setSink(self->sink_);
        self->seat_->setSurfaceSizer([self](wl_surface* s, int& w, int& h) {
            Output* o = self->outputForSurface(s);
            if (!o) return false;
            w = o->logicalWidth();
            h = o->logicalHeight();
            return true;
        });
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        auto* output = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4U)));
        auto out = std::make_unique<Output>(output, name, &self->env_);
        if (self->activeLock_) out->createLockSurface(self->activeLock_);
        self->outputs_.push_back(std::move(out));
    }
}

void WaylandDisplay::onGlobalRemove(void* data, wl_registry*, uint32_t name) {
    auto* self = static_cast<WaylandDisplay*>(data);
    for (auto it = self->outputs_.begin(); it != self->outputs_.end(); ++it) {
        if ((*it)->name() == name) {
            self->outputs_.erase(it);
            return;
        }
    }
}

}  // namespace qypr
