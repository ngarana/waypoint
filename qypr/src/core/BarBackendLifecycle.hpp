// BarBackendLifecycle.hpp - Manages startup sequence and change observation for bar backends.
#pragma once

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

private:
    EventLoop& loop_;
    SystemBackends& backends_;
    StateCache& stateCache_;
    Invalidator& invalidator_;
};

}  // namespace qypr
