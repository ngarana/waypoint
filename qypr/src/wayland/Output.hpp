// Output.hpp - One monitor's lock surface and its render scheduling.
//
// Each output shows the same UI, sized to itself. Rendering is compositor
// throttled: we only paint in response to a frame callback while something
// is dirty or animating, then go idle — no busy loop, no wasted memory.

#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "core/Interfaces.hpp"
#include "wayland/ShmBuffer.hpp"

struct ext_session_lock_v1;
struct ext_session_lock_surface_v1;

namespace qypr {

// Shared services an Output needs, owned by WaylandDisplay.
struct OutputEnv {
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    RenderFn render;
    AnimatingFn animating;
};

class Output {
public:
    Output(wl_output* output, uint32_t name, OutputEnv* env);
    ~Output();

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    // Create the ext-session-lock surface for this output (called once locked).
    void createLockSurface(ext_session_lock_v1* lock);

    // Mark dirty and repaint as soon as the compositor allows.
    void invalidate();

    uint32_t name() const { return name_; }
    wl_surface* surface() const { return surface_; }
    int logicalWidth() const { return width_; }
    int logicalHeight() const { return height_; }

    // Wayland C callbacks (public so listener tables at file scope can bind them).
    static void onGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t,
                           const char*, const char*, int32_t);
    static void onMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t);
    static void onDone(void*, wl_output*);
    static void onScale(void*, wl_output*, int32_t);
    static void onName(void*, wl_output*, const char*);
    static void onDescription(void*, wl_output*, const char*);
    static void onConfigure(void*, ext_session_lock_surface_v1*, uint32_t, uint32_t, uint32_t);
    static void onFrame(void*, wl_callback*, uint32_t);

private:
    void render();
    ShmBuffer* acquireBuffer(int pxW, int pxH);

    wl_output* output_ = nullptr;
    uint32_t name_ = 0;
    OutputEnv* env_ = nullptr;

    wl_surface* surface_ = nullptr;
    ext_session_lock_surface_v1* lockSurface_ = nullptr;
    wl_callback* frameCallback_ = nullptr;

    std::vector<std::unique_ptr<ShmBuffer>> buffers_;

    int width_ = 0;   // logical
    int height_ = 0;  // logical
    int scale_ = 1;
    bool configured_ = false;
    bool dirty_ = true;
    bool framePending_ = false;
};

}  // namespace qypr
