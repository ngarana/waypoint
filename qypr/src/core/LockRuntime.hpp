// LockRuntime.hpp - Runtime coordinator for qypr-lock session lock lifecycle.
#pragma once

#include <functional>

#include "core/EventLoop.hpp"
#include "core/Interfaces.hpp"
#include "ui/statusbar/StatusIndicator.hpp"
#include "wayland/LockSession.hpp"
#include "wayland/WaylandDisplay.hpp"

namespace qypr {

class NotificationMonitor;

class LockRuntime {
public:
    LockRuntime(EventLoop& loop, WaylandDisplay& display, LockSession& lock);

    // Connects display, binds input and rendering hooks, then acquires session lock.
    // Returns false if display connect or lock acquisition failed.
    bool acquire(InputSink* sink, RenderFn renderFn, AnimatingFn animatingFn);

    // Starts backends permitted on the lock screen.
    void startBackends(SystemBackends& backends, NotificationMonitor& notifications);

    // Unlock and terminate the lock session loop.
    void unlockAndQuit();

private:
    EventLoop& loop_;
    WaylandDisplay& display_;
    LockSession& lock_;
};

}  // namespace qypr
