// Interfaces.hpp - Boundaries between the Wayland/platform layer and the UI.
//
// The platform layer depends only on these abstractions, never on concrete
// UI classes, and vice-versa (Dependency Inversion). LockScreen implements
// InputSink; App implements RenderHost.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <cairo/cairo.h>

namespace qypr {

// Draws the whole UI into a surface of the given pixel size / scale.
using RenderFn = std::function<void(cairo_t*, int width, int height, int scale)>;

// Reports whether any animation is in flight, so outputs know to keep
// requesting frames instead of idling.
using AnimatingFn = std::function<bool()>;

// Receives translated input events. surfaceW/H are the focused output's
// logical size so the sink can hit-test against its own layout.
class InputSink {
public:
    virtual ~InputSink() = default;

    virtual void onTextInput(const std::string& utf8) = 0;
    virtual void onSpecialKey(uint32_t keysym, uint32_t modifiers) = 0;

    // The active xkb keyboard layout changed (name is the full description, e.g.
    // "English (US)"; index is the group; count is the number of layouts).
    // Default no-op: only the bar's keyboard-layout indicator cares — the lock
    // screen ignores it.
    virtual void onLayoutChanged(const std::string& name, uint32_t index, uint32_t count) {}

    virtual void onPointerMotion(int surfaceW, int surfaceH, double x, double y) = 0;
    virtual void onPointerButton(int surfaceW, int surfaceH, double x, double y, uint32_t button,
                                 bool pressed) = 0;
    // Scroll wheel/axis. Default no-op: most sinks don't scroll.
    virtual void onPointerScroll(int surfaceW, int surfaceH, double x, double y, double dx,
                                 double dy) {}
    virtual void onPointerLeave() = 0;
};

// The narrowest capability handed to UI components that only need to trigger
// repaints. Lock-agnostic components (StatusBar, indicators) take this — never
// RenderHost — so they cannot reach lock-only powers like requestUnlock().
class Invalidator {
public:
    virtual ~Invalidator() = default;
    virtual void invalidate() = 0;  // repaint every output soon
};

// The lock UI drives the session through this: ask for a repaint, or unlock.
class RenderHost : public Invalidator {
public:
    virtual void requestUnlock() = 0;  // authentication succeeded
};

}  // namespace qypr
