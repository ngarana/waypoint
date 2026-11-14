// BarBackendLifecycle.hpp - Manages startup sequence and change observation for bar backends.
#pragma once

#include <functional>

#include "core/EventLoop.hpp"
#include "core/Interfaces.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class StateCache;

class BarBackendLifecycle {
public:
    BarBackendLifecycle(EventLoop& loop, SystemBackends& backends, StateCache& stateCache,
                        Invalidator& invalidator);

    // Start backends asynchronously (deferred behind first frame draw).
    void start();

    // Synchronous start for preview mode.
    void startPreview() const;

    // What a notification change runs (default: a repaint). The bar passes
    // its indicator refresh, so modules that group notifications
    // (notification-apps, whatsapp, email) see the change.
    void setOnNotificationsChanged(std::function<void()> fn) { onNotifications_ = std::move(fn); }

private:
    EventLoop& loop_;
    SystemBackends& backends_;
    StateCache& stateCache_;
    Invalidator& invalidator_;
    std::function<void()> onNotifications_;
};

}  // namespace qypr
