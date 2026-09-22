// StateCache.hpp - Last-known indicator state, persisted across restarts.
//
// Thin facade: delegates to StateCacheCodec (typed conversion),
// StateCacheStore (atomic persistence), and
// StateCacheCoordinator (debounce/sampling). See those classes
// for the individual concerns.

#pragma once

#include <string>

#include "core/EventLoop.hpp"
#include "system/StateCacheCodec.hpp"
#include "system/StateCacheCoordinator.hpp"
#include "system/StateCacheStore.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class StateCache {
public:
    // Read the cache file. Never fails: an absent or unparsable file simply
    // leaves every value at its default, and seed() then does nothing useful.
    void load();

    // Push the loaded values into the backends that support seeding. Call
    // before the first paint and before the backends are started, so the
    // first frame carries real numbers. Backends that have already
    // produced a live reply ignore the seed.
    void seed(const SystemBackends& backends);

    // Begin persisting state changes. Records the backends to sample and
    // the loop to schedule the debounced write on.
    void track(EventLoop& loop, const SystemBackends& backends);

    // Note that something may have changed. Cheap and safe to call often
    // (it is wired to the repaint hook, which also fires for hover and
    // animation): the first call arms a one-shot debounce timer, and
    // subsequent calls before it fires are free. The write itself is
    // skipped when the serialized state is byte-identical to what is
    // already on disk, so pointer motion never causes file I/O.
    void noteChanged();

    // Flush pending state to disk immediately. Public for testing.
    void flush();

    // Serialize the tracked backends to the cache format. Public for testing.
    [[nodiscard]] std::string serialize() const;

    // $XDG_CACHE_HOME/qypr/bar-state, else $HOME/.cache/qypr/bar-state.
    static std::string defaultPath();

private:
    std::string path_{defaultPath()};
    bool loaded_ = false;

    StateCacheCodec codec_;
    StateCacheStore store_;
    StateCacheCoordinator coordinator_;

    // Backends to serialize (set by track(), used by serialize()).
    const SystemBackends* backends_ = nullptr;
};

}  // namespace qypr
