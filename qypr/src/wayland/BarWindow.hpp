// BarWindow.hpp - One monitor's wlr-layer-shell surface for the standalone bar.
//
// The unlocked-desktop counterpart to Output (which owns a session-lock
// surface). It anchors a chromeless strip to the top of an output, reserves an
// exclusive zone so tiled windows never sit under it, and drives the same
// throttled shm render loop. When the bar opens an overlay (Quick Settings or a
// popover) the surface does NOT resize — a layer-surface resize is what the
// compositor animates (the popover "bounce"). Instead the surface is a fixed
// full-output-height buffer, transparent everywhere the bar isn't drawing, and
// only its pointer input region changes. While a panel is open, the owning
// output captures outside clicks for light dismissal; otherwise only the strip
// takes input. The exclusive zone always reserves just the strip.

#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "wayland/Output.hpp"  // OutputEnv
#include "wayland/ShmBuffer.hpp"

struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

namespace qypr {

class BarWindow {
public:
    // `bottom` anchors the panel to the lower screen edge instead of the top.
    BarWindow(wl_output* output, uint32_t name, OutputEnv* env, int reservedHeight,
              bool bottom = false);
    ~BarWindow();

    BarWindow(const BarWindow&) = delete;
    BarWindow& operator=(const BarWindow&) = delete;

    // Create the layer surface, anchor it, and reserve its exclusive zone. The
    // shell is passed here (not the ctor) so the window is constructed — and its
    // wl_output listener attached — before the compositor streams mode/scale.
    void createLayerSurface(zwlr_layer_shell_v1* shell);

    // Track the drawn overlay extent without resizing the layer surface.
    // Any extent beyond the strip enables outside-click dismissal over the output.
    void setOverlayHeight(int logicalH);

    // Grab (EXCLUSIVE) or release (NONE) keyboard focus for this surface — set
    // while a popover is open, for Escape and control navigation.
    void setKeyboardInteractive(bool on);

    // Mark dirty and repaint as soon as the compositor allows.
    void invalidate();
    void setRenderObserver(std::function<void(BarWindow*)> observer) {
        renderObserver_ = std::move(observer);
    }

    uint32_t name() const { return name_; }
    wl_surface* surface() const { return surface_; }
    wl_output* output() const { return output_; }
    int logicalWidth() const { return width_; }
    int logicalHeight() const { return height_; }

    // Wayland C callbacks (public so file-scope listener tables can bind them).
    static void onGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t,
                           const char*, const char*, int32_t);
    static void onMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t);
    static void onDone(void*, wl_output*);
    static void onScale(void*, wl_output*, int32_t);
    static void onName(void*, wl_output*, const char*);
    static void onDescription(void*, wl_output*, const char*);
    static void onConfigure(void*, zwlr_layer_surface_v1*, uint32_t, uint32_t, uint32_t);
    static void onClosed(void*, zwlr_layer_surface_v1*);
    static void onFrame(void*, wl_callback*, uint32_t);

private:
    void render();
    std::function<void(BarWindow*)> renderObserver_;
    void applyInputRegion();  // (re)commit the pointer input region for inputHeight_
    ShmBuffer* acquireBuffer(int pxW, int pxH);

    wl_output* output_ = nullptr;
    uint32_t name_ = 0;
    OutputEnv* env_ = nullptr;
    zwlr_layer_shell_v1* shell_ = nullptr;

    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layerSurface_ = nullptr;
    wl_callback* frameCallback_ = nullptr;

    std::vector<std::unique_ptr<ShmBuffer>> buffers_;

    int reservedHeight_ = 0;  // exclusive zone + idle strip height (logical)
    int outputPixelHeight_ = 0;
    int outputHeight_ = 0;  // full output height (logical); the fixed surface height
    // Start with the idle strip active. BarSurfaceController intentionally
    // treats its initial idle height as already synchronized, so relying on a
    // later setOverlayHeight() call leaves the first frame with zero damage
    // and no pointer input region.
    int inputHeight_ = 0;          // input-region height from the anchored edge (logical)
    int appliedInputHeight_ = -1;  // last input-region height committed (avoid churn)
    bool bottom_ = false;          // anchored to the lower screen edge
    bool kbInteractive_ = false;   // keyboard grab state (active flyout)

    int width_ = 0;   // logical, from configure
    int height_ = 0;  // logical, from configure
    int scale_ = 1;
    bool configured_ = false;
    bool dirty_ = true;
    bool framePending_ = false;
};

}  // namespace qypr
