#include "wayland/Output.hpp"

#include "ext-session-lock-v1-client-protocol.h"

#include <cairo/cairo.h>

namespace qypr {

namespace {
const wl_output_listener kOutputListener = {
    .geometry = Output::onGeometry,
    .mode = Output::onMode,
    .done = Output::onDone,
    .scale = Output::onScale,
    .name = Output::onName,
    .description = Output::onDescription,
};

const ext_session_lock_surface_v1_listener kLockSurfaceListener = {
    .configure = Output::onConfigure,
};
}  // namespace

Output::Output(wl_output* output, uint32_t name, OutputEnv* env)
    : output_(output),
      name_(name),
      env_(env) {
    wl_output_add_listener(output_, &kOutputListener, this);
}

Output::~Output() {
    if (frameCallback_) wl_callback_destroy(frameCallback_);
    buffers_.clear();
    if (lockSurface_) ext_session_lock_surface_v1_destroy(lockSurface_);
    if (surface_) wl_surface_destroy(surface_);
    if (output_) wl_output_destroy(output_);
}

void Output::createLockSurface(ext_session_lock_v1* lock) {
    if (lockSurface_) return;
    surface_ = wl_compositor_create_surface(env_->compositor);
    wl_surface_set_buffer_scale(surface_, scale_);
    lockSurface_ = ext_session_lock_v1_get_lock_surface(lock, surface_, output_);
    ext_session_lock_surface_v1_add_listener(lockSurface_, &kLockSurfaceListener, this);
}

// -----------------------------------------------------------------------------
// wl_output geometry — we only care about scale; size comes from configure.
// -----------------------------------------------------------------------------
void Output::onScale(void* data, wl_output*, int32_t factor) {
    auto* self = static_cast<Output*>(data);
    if (factor > 0) self->scale_ = factor;
}
void Output::onGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*,
                        const char*, int32_t) {}
void Output::onMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
void Output::onDone(void*, wl_output*) {}
void Output::onName(void*, wl_output*, const char*) {}
void Output::onDescription(void*, wl_output*, const char*) {}

// -----------------------------------------------------------------------------
// Lock-surface configure: adopt the compositor's size, then paint.
// -----------------------------------------------------------------------------
void Output::onConfigure(void* data, ext_session_lock_surface_v1* surf, uint32_t serial,
                         uint32_t width, uint32_t height) {
    auto* self = static_cast<Output*>(data);
    ext_session_lock_surface_v1_ack_configure(surf, serial);
    self->width_ = static_cast<int>(width);
    self->height_ = static_cast<int>(height);
    self->configured_ = true;
    self->dirty_ = true;
    self->render();
}

void Output::invalidate() {
    dirty_ = true;
    if (!framePending_) render();
}

// -----------------------------------------------------------------------------
// Frame callback: throttle repaints to the compositor's cadence.
// -----------------------------------------------------------------------------
void Output::onFrame(void* data, wl_callback* cb, uint32_t) {
    auto* self = static_cast<Output*>(data);
    wl_callback_destroy(cb);
    self->frameCallback_ = nullptr;
    self->framePending_ = false;
    if (self->dirty_ || (self->env_->animating && self->env_->animating())) self->render();
}

ShmBuffer* Output::acquireBuffer(int pxW, int pxH) {
    for (auto& b : buffers_) {
        if (!b->busy() && b->width() == pxW && b->height() == pxH) return b.get();
    }
    // Drop stale-sized free buffers to bound memory, then allocate.
    if (buffers_.size() >= 2) {
        for (auto it = buffers_.begin(); it != buffers_.end();) {
            if (!(*it)->busy() && ((*it)->width() != pxW || (*it)->height() != pxH))
                it = buffers_.erase(it);
            else
                ++it;
        }
    }
    auto buf = ShmBuffer::create(env_->shm, pxW, pxH);
    if (!buf) return nullptr;
    buffers_.push_back(std::move(buf));
    return buffers_.back().get();
}

void Output::render() {
    if (!configured_ || width_ <= 0 || height_ <= 0) return;

    const int pxW = width_ * scale_;
    const int pxH = height_ * scale_;
    ShmBuffer* buf = acquireBuffer(pxW, pxH);
    if (!buf) return;

    cairo_t* cr = cairo_create(buf->cairoSurface());
    cairo_scale(cr, scale_, scale_);
    if (env_->render) env_->render(cr, width_, height_, scale_);
    cairo_destroy(cr);
    cairo_surface_flush(buf->cairoSurface());

    wl_surface_attach(surface_, buf->buffer(), 0, 0);
    buf->markBusy();
    wl_surface_damage_buffer(surface_, 0, 0, pxW, pxH);

    frameCallback_ = wl_surface_frame(surface_);
    static const wl_callback_listener kFrameListener = {.done = Output::onFrame};
    wl_callback_add_listener(frameCallback_, &kFrameListener, this);
    framePending_ = true;
    dirty_ = false;

    wl_surface_commit(surface_);
}

}  // namespace qypr
