// BarDisplay.hpp - Wayland connection for the standalone qypr-bar.
//
// The unlocked-desktop sibling of WaylandDisplay: it binds the compositor, shm,
// seat and outputs, plus wlr-layer-shell instead of the session-lock manager,
// and hosts one BarWindow per output. The lock path (WaylandDisplay) is left
// untouched; the two share only the lower-level plumbing (Seat, Output env,
// ShmBuffer). Exposes the raw wl_display so the WM backends (workspaces, active
// window) can bind their own registries on the same connection.

#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "core/Interfaces.hpp"
#include "wayland/BarWindow.hpp"
#include "wayland/Seat.hpp"

struct zwlr_layer_shell_v1;
struct zwp_idle_inhibit_manager_v1;
struct zwlr_gamma_control_manager_v1;

namespace qypr {

class EventLoop;

class BarDisplay {
public:
    BarDisplay(EventLoop& loop, int reservedHeight, bool bottom = false);
    ~BarDisplay();

    BarDisplay(const BarDisplay&) = delete;
    BarDisplay& operator=(const BarDisplay&) = delete;

    // Connect, bind globals, create layer surfaces, and integrate the socket
    // into the event loop. Returns false without a compositor/shm/layer-shell.
    bool connect();

    void setRenderFn(RenderFn fn) { env_.render = std::move(fn); }
    void setAnimatingFn(AnimatingFn fn) { env_.animating = std::move(fn); }
    void setInputSink(InputSink* sink);

    void invalidateAll();

    // Resize every bar surface to fit an open overlay (Quick Settings/popover),
    // or back to the idle strip. `logicalH` is the height from the anchored edge
    // (0/small = idle); each surface clamps it to its own output.
    void setOverlayHeight(int logicalH);

    // Grab/release keyboard focus on every bar surface (launcher search box).
    void setKeyboardInteractive(bool on);

    BarWindow* windowForSurface(wl_surface* surface);

    wl_display* display() const { return display_; }
    void roundtrip();

    // Idle-inhibit plumbing for the "keep awake" indicator (Phase 15): the bound
    // manager (nullptr if the compositor lacks the protocol) and a surface to
    // anchor an inhibitor on (the first bar window, or nullptr before any exist).
    zwp_idle_inhibit_manager_v1* idleInhibitManager() const { return idleMgr_; }
    // Night Light plumbing (wlr-gamma-control): the bound manager, or nullptr
    // when the compositor lacks the protocol — the toggle then hides.
    zwlr_gamma_control_manager_v1* gammaControlManager() const { return gammaMgr_; }
    wl_surface* anchorSurface() const;

    // The wl_output objects behind every discovered window. Used by the Night
    // Light backend to create per-output gamma controls. The pointers are
    // borrowed from BarWindow and remain valid while the window exists.
    std::vector<wl_output*> outputs() const;

    // Wayland C callbacks (public so the registry listener table can bind them).
    static void onGlobal(void*, wl_registry*, uint32_t, const char*, uint32_t);
    static void onGlobalRemove(void*, wl_registry*, uint32_t);

private:
    void flush();

    EventLoop& loop_;
    int reservedHeight_ = 0;
    bool bottom_ = false;  // anchor the panel to the lower screen edge

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;

    wl_compositor* compositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    zwlr_layer_shell_v1* layerShell_ = nullptr;
    zwp_idle_inhibit_manager_v1* idleMgr_ = nullptr;
    zwlr_gamma_control_manager_v1* gammaMgr_ = nullptr;

    OutputEnv env_;
    std::unique_ptr<Seat> seat_;
    std::vector<std::unique_ptr<BarWindow>> windows_;
    InputSink* sink_ = nullptr;
    bool connected_ = false;  // true once initial globals are bound
};

}  // namespace qypr
