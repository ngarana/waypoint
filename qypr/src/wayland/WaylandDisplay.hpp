// WaylandDisplay.hpp - Owns the Wayland connection and the globals we bind.
//
// Discovers the compositor, shm, seat, outputs and the session-lock manager,
// wires them into the event loop, and hands the rest of the app a clean view
// of outputs + input without leaking registry mechanics.

#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "core/Interfaces.hpp"
#include "wayland/Output.hpp"
#include "wayland/Seat.hpp"

struct ext_session_lock_manager_v1;
struct ext_session_lock_v1;

namespace qypr {

class EventLoop;

class WaylandDisplay {
public:
    explicit WaylandDisplay(EventLoop& loop);
    ~WaylandDisplay();

    WaylandDisplay(const WaylandDisplay&) = delete;
    WaylandDisplay& operator=(const WaylandDisplay&) = delete;

    // Connect, bind globals, and integrate the socket into the event loop.
    // Returns false if the compositor lacks ext-session-lock support.
    bool connect();

    void setRenderFn(RenderFn fn) { env_.render = std::move(fn); }
    void setAnimatingFn(AnimatingFn fn) { env_.animating = std::move(fn); }
    void setInputSink(InputSink* sink);

    ext_session_lock_manager_v1* lockManager() const { return lockManager_; }

    // Create a lock surface on every current output, and on outputs that
    // appear afterwards, for the given active lock.
    void createLockSurfaces(ext_session_lock_v1* lock);
    void setActiveLock(ext_session_lock_v1* lock) { activeLock_ = lock; }

    void invalidateAll();
    Output* outputForSurface(wl_surface* surface);

    wl_display* display() const { return display_; }

    // Block until the compositor has processed all pending requests. Needed on
    // unlock so ext_session_lock_v1::unlock_and_destroy is delivered before we
    // exit; otherwise the compositor never unlocks and the screen stays frozen.
    void roundtrip();

    // Wayland C callbacks (public so the registry listener table can bind them).
    static void onGlobal(void*, wl_registry*, uint32_t, const char*, uint32_t);
    static void onGlobalRemove(void*, wl_registry*, uint32_t);

private:
    void flush();

    EventLoop& loop_;
    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;

    wl_compositor* compositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    ext_session_lock_manager_v1* lockManager_ = nullptr;
    ext_session_lock_v1* activeLock_ = nullptr;

    OutputEnv env_;
    std::unique_ptr<Seat> seat_;
    std::vector<std::unique_ptr<Output>> outputs_;
    InputSink* sink_ = nullptr;
};

}  // namespace qypr
