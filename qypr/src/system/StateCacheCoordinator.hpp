// StateCacheCoordinator.hpp - Event-loop debounce and backend sampling
// for the state cache.
//
// Owns the debounce timer and the list of backends to sample. Isolated
// from StateCacheCodec (typed conversion) and StateCacheStore (file
// persistence) so each concern has a single owner.

#pragma once

#include <functional>
#include <string>

#include "core/EventLoop.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class StateCacheCodec;

class StateCacheCoordinator {
public:
    StateCacheCoordinator() = default;

    // Begin persisting state changes. Records the backends to sample
    // and the loop to schedule the debounced write on.
    void track(EventLoop& loop, const SystemBackends& backends, StateCacheCodec& codec,
               const std::string& path);

    // Note that something may have changed. Cheap and safe to call
    // often (it is wired to the repaint hook, which also fires for
    // hover and animation): the first call arms a one-shot debounce
    // timer, and subsequent calls before it fires are free. The write
    // itself is skipped when the serialized state is byte-identical
    // to what is already on disk.
    void noteChanged();

    // Flush pending state to disk immediately. Public for testing.
    void flush();

private:
    static constexpr int kDebounceMs = 5000;

    EventLoop* loop_ = nullptr;
    const SystemBackends* backends_ = nullptr;
    StateCacheCodec* codec_ = nullptr;
    std::string path_;
    int timer_ = -1;           // armed one-shot debounce, -1 when idle
    std::string lastWritten_;  // suppresses no-op writes
};

}  // namespace qypr
