#include "wayland/BarDisplay.hpp"

#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "wlr-gamma-control-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <algorithm>
#include <cstring>

#include "core/EventLoop.hpp"

namespace qypr {

namespace {
const wl_registry_listener kRegistryListener = {
    .global = BarDisplay::onGlobal,
    .global_remove = BarDisplay::onGlobalRemove,
};
}  // namespace

BarDisplay::BarDisplay(EventLoop& loop, int reservedHeight, bool bottom)
    : loop_(loop),
      reservedHeight_(reservedHeight),
      bottom_(bottom) {}

BarDisplay::~BarDisplay() {
    windows_.clear();
    seat_.reset();
    if (gammaMgr_) zwlr_gamma_control_manager_v1_destroy(gammaMgr_);
    if (idleMgr_) zwp_idle_inhibit_manager_v1_destroy(idleMgr_);
    if (layerShell_) zwlr_layer_shell_v1_destroy(layerShell_);
    if (compositor_) wl_compositor_destroy(compositor_);
    if (shm_) wl_shm_destroy(shm_);
    if (registry_) wl_registry_destroy(registry_);
    if (display_) wl_display_disconnect(display_);
}

wl_surface* BarDisplay::anchorSurface() const {
    return windows_.empty() ? nullptr : windows_.front()->surface();
}

bool BarDisplay::connect() {
    display_ = wl_display_connect(nullptr);
    if (!display_) return false;

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);

    wl_display_roundtrip(display_);  // bind globals
    wl_display_roundtrip(display_);  // output/seat sub-events (mode, scale, caps)

    if (!compositor_ || !shm_ || !layerShell_) return false;

    for (auto& win : windows_) win->createLayerSurface(layerShell_);
    connected_ = true;

    loop_.addPrepare([this] { flush(); });
    loop_.addFd(wl_display_get_fd(display_), [this](uint32_t) {
        if (wl_display_dispatch(display_) < 0) loop_.quit();
    });
    return true;
}

void BarDisplay::flush() {
    wl_display_dispatch_pending(display_);
    wl_display_flush(display_);
}

void BarDisplay::roundtrip() {
    if (display_) wl_display_roundtrip(display_);
}

void BarDisplay::setInputSink(InputSink* sink) {
    sink_ = sink;
    if (seat_) seat_->setSink(sink);
}

void BarDisplay::invalidateAll() {
    for (auto& win : windows_) win->invalidate();
}

void BarDisplay::setOverlayHeight(int logicalH) {
    for (auto& win : windows_) win->setOverlayHeight(logicalH);
}

void BarDisplay::setKeyboardInteractive(bool on) {
    for (auto& win : windows_) win->setKeyboardInteractive(on);
    flush();
}

BarWindow* BarDisplay::windowForSurface(wl_surface* surface) {
    for (auto& win : windows_) {
        if (win->surface() == surface) return win.get();
    }
    return nullptr;
}

std::vector<wl_output*> BarDisplay::outputs() const {
    std::vector<wl_output*> out;
    out.reserve(windows_.size());
    for (const auto& win : windows_) out.push_back(win->output());
    return out;
}

void BarDisplay::onGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface,
                          uint32_t version) {
    auto* self = static_cast<BarDisplay*>(data);

    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        self->compositor_ = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
        self->env_.compositor = self->compositor_;
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        self->shm_ = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        self->env_.shm = self->shm_;
    } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        self->layerShell_ = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(
            registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
    } else if (std::strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0) {
        self->idleMgr_ = static_cast<zwp_idle_inhibit_manager_v1*>(
            wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, 1));
    } else if (std::strcmp(interface, zwlr_gamma_control_manager_v1_interface.name) == 0) {
        self->gammaMgr_ = static_cast<zwlr_gamma_control_manager_v1*>(
            wl_registry_bind(registry, name, &zwlr_gamma_control_manager_v1_interface, 1));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        auto* seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7u)));
        self->seat_ = std::make_unique<Seat>(seat, self->loop_, &self->env_);
        self->seat_->setSink(self->sink_);
        self->seat_->setSurfaceSizer([self](wl_surface* s, int& w, int& h) {
            BarWindow* win = self->windowForSurface(s);
            if (!win) return false;
            w = win->logicalWidth();
            h = win->logicalHeight();
            return true;
        });
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        auto* output = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u)));
        auto win = std::make_unique<BarWindow>(output, name, &self->env_, self->reservedHeight_,
                                               self->bottom_);
        if (self->connected_ && self->layerShell_) win->createLayerSurface(self->layerShell_);
        self->windows_.push_back(std::move(win));
    }
}

void BarDisplay::onGlobalRemove(void* data, wl_registry*, uint32_t name) {
    auto* self = static_cast<BarDisplay*>(data);
    for (auto it = self->windows_.begin(); it != self->windows_.end(); ++it) {
        if ((*it)->name() == name) {
            self->windows_.erase(it);
            return;
        }
    }
}

}  // namespace qypr
