// BarWindow.cpp - One monitor's wlr-layer-shell surface for the standalone bar.
#include "wayland/BarWindow.hpp"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <algorithm>
#include <cstring>

namespace qypr {

namespace {
constexpr int kFallbackOutputHeight = 2160;

const wl_output_listener kOutputListener = {
    .geometry = BarWindow::onGeometry,
    .mode = BarWindow::onMode,
    .done = BarWindow::onDone,
    .scale = BarWindow::onScale,
    .name = BarWindow::onName,
    .description = BarWindow::onDescription,
};

const zwlr_layer_surface_v1_listener kLayerSurfaceListener = {
    .configure = BarWindow::onConfigure,
    .closed = BarWindow::onClosed,
};
}  // namespace

BarWindow::BarWindow(wl_output* output, uint32_t name, OutputEnv* env, int reservedHeight,
                     bool bottom)
    : output_(output),
      name_(name),
      env_(env),
      reservedHeight_(reservedHeight),
      bottom_(bottom) {
    wl_output_add_listener(output_, &kOutputListener, this);
}

BarWindow::~BarWindow() {
    if (frameCallback_) wl_callback_destroy(frameCallback_);
    buffers_.clear();
    if (layerSurface_) zwlr_layer_surface_v1_destroy(layerSurface_);
    if (surface_) wl_surface_destroy(surface_);
    if (output_) wl_output_destroy(output_);
}

void BarWindow::createLayerSurface(zwlr_layer_shell_v1* shell) {
    if (layerSurface_) return;
    shell_ = shell;
    surface_ = wl_compositor_create_surface(env_->compositor);
    wl_surface_set_buffer_scale(surface_, scale_);

    layerSurface_ = zwlr_layer_shell_v1_get_layer_surface(
        shell_, surface_, output_, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "qypr-bar");
    zwlr_layer_surface_v1_add_listener(layerSurface_, &kLayerSurfaceListener, this);

    // Anchor a full-width surface to the configured edge; width 0 stretches
    // between the left/right anchors. Anchoring to a single edge (plus the two
    // perpendicular ones) — rather than to both top and bottom — is what keeps
    // the exclusive zone effective and lets the strip/popover extend inward from
    // that edge.
    zwlr_layer_surface_v1_set_anchor(layerSurface_, (bottom_ ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
                                                             : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) |
                                                        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                                        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    // Fixed full-output height: the surface is sized once and never resized,
    // because a layer-surface resize is what the compositor animates (the
    // popover "bounce"). The exclusive zone still reserves only the strip, so
    // tiled windows tile against the strip; the rest of the surface is
    // transparent and, outside the input region applied in render(), passes
    // clicks through to the apps below.
    const int full = outputHeight_ > 0 ? outputHeight_ : kFallbackOutputHeight;
    zwlr_layer_surface_v1_set_size(layerSurface_, 0, full);
    zwlr_layer_surface_v1_set_exclusive_zone(layerSurface_, reservedHeight_);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layerSurface_, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    // First commit with no buffer: the compositor answers with a configure that
    // carries our size, and render() attaches the first frame.
    wl_surface_commit(surface_);
}

void BarWindow::setKeyboardInteractive(bool on) {
    if (!layerSurface_ || on == kbInteractive_) return;
    kbInteractive_ = on;
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layerSurface_, on ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
                          : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_surface_commit(surface_);
}

void BarWindow::setOverlayHeight(int logicalH) {
    const int full = outputHeight_ > 0 ? outputHeight_ : kFallbackOutputHeight;
    int h = logicalH < reservedHeight_ ? reservedHeight_ : logicalH;
    if (h > full) h = full;
    if (!layerSurface_ || h == inputHeight_) return;
    inputHeight_ = h;
    if (configured_) render();
}

// -----------------------------------------------------------------------------
// wl_output — track scale and the current mode (for overlay sizing).
// -----------------------------------------------------------------------------
void BarWindow::onScale(void* data, wl_output*, int32_t factor) {
    auto* self = static_cast<BarWindow*>(data);
    if (factor > 0) self->scale_ = factor;
}
void BarWindow::onMode(void* data, wl_output*, uint32_t flags, int32_t, int32_t height, int32_t) {
    auto* self = static_cast<BarWindow*>(data);
    constexpr uint32_t kCurrent = 0x1;  // WL_OUTPUT_MODE_CURRENT
    if ((flags & kCurrent) && height > 0)
        self->outputHeight_ = height / (self->scale_ > 0 ? self->scale_ : 1);
}
void BarWindow::onGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t,
                           const char*, const char*, int32_t) {}
void BarWindow::onDone(void*, wl_output*) {}
void BarWindow::onName(void*, wl_output*, const char*) {}
void BarWindow::onDescription(void*, wl_output*, const char*) {}

// -----------------------------------------------------------------------------
// Layer-surface configure: adopt the compositor's size, then paint.
// -----------------------------------------------------------------------------
void BarWindow::onConfigure(void* data, zwlr_layer_surface_v1* surf, uint32_t serial,
                            uint32_t width, uint32_t height) {
    auto* self = static_cast<BarWindow*>(data);
    zwlr_layer_surface_v1_ack_configure(surf, serial);
    self->width_ = static_cast<int>(width);
    self->height_ = static_cast<int>(height);
    self->configured_ = true;
    self->dirty_ = true;
    self->render();
}

void BarWindow::onClosed(void* data, zwlr_layer_surface_v1*) {
    auto* self = static_cast<BarWindow*>(data);
    self->configured_ = false;  // compositor tore the surface down (e.g. output gone)
}

void BarWindow::invalidate() {
    dirty_ = true;
    if (!framePending_) render();
}

// -----------------------------------------------------------------------------
// Frame callback: throttle repaints to the compositor's cadence.
// -----------------------------------------------------------------------------
void BarWindow::onFrame(void* data, wl_callback* cb, uint32_t) {
    auto* self = static_cast<BarWindow*>(data);
    wl_callback_destroy(cb);
    self->frameCallback_ = nullptr;
    self->framePending_ = false;
    if (self->dirty_ || (self->env_->animating && self->env_->animating())) self->render();
}

ShmBuffer* BarWindow::acquireBuffer(int pxW, int pxH) {
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

void BarWindow::render() {
    if (!configured_ || width_ <= 0 || height_ <= 0) return;

    const int pxW = width_ * scale_;
    const int pxH = height_ * scale_;
    ShmBuffer* buf = acquireBuffer(pxW, pxH);
    if (!buf) return;

    cairo_t* cr = cairo_create(buf->cairoSurface());
    cairo_scale(cr, scale_, scale_);

    // Clear only the active and previously drawn regions to keep memory sparse.
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    const int clear_h = std::max(inputHeight_, buf->drawnHeight());
    const int clear_y = bottom_ ? (height_ - clear_h) : 0;
    cairo_rectangle(cr, 0, clear_y, width_, clear_h);
    cairo_fill(cr);
    cairo_restore(cr);

    buf->setDrawnHeight(inputHeight_);

    if (env_->render) env_->render(cr, width_, height_, scale_);
    cairo_destroy(cr);
    cairo_surface_flush(buf->cairoSurface());

    wl_surface_attach(surface_, buf->buffer(), 0, 0);
    buf->markBusy();

    // Damage only the active/previously active region to save compositor resource consumption.
    const int dmgH = clear_h;
    const int dmgY = bottom_ ? (height_ - dmgH) * scale_ : 0;
    wl_surface_damage_buffer(surface_, 0, dmgY, pxW, dmgH * scale_);

    // Keep the pointer input region in step with the open overlay so the strip
    // (and any open popover) takes clicks while the transparent remainder stays
    // click-through. Folded into this commit; only re-applied when it changes.
    if (inputHeight_ != appliedInputHeight_) {
        applyInputRegion();
        appliedInputHeight_ = inputHeight_;
    }

    frameCallback_ = wl_surface_frame(surface_);
    static const wl_callback_listener kFrameListener = {.done = BarWindow::onFrame};
    wl_callback_add_listener(frameCallback_, &kFrameListener, this);
    framePending_ = true;
    dirty_ = false;

    wl_surface_commit(surface_);
}

void BarWindow::applyInputRegion() {
    wl_region* region = wl_compositor_create_region(env_->compositor);
    const int h = inputHeight_ < height_ ? inputHeight_ : height_;
    const int y = bottom_ ? height_ - h : 0;
    wl_region_add(region, 0, y, width_, h);
    wl_surface_set_input_region(surface_, region);
    wl_region_destroy(region);
}

}  // namespace qypr
