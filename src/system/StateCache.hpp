// StateCache.hpp - Last-known indicator state, persisted across restarts.
//
// The problem this solves is a boot-order one, not a speed one. The daemons the
// bar reads from are mostly D-Bus-activated or socket-activated, so at login
// they are frequently *not running yet* — UPower typically starts after the bar
// does, NetworkManager may still be associating, pipewire-pulse may still be
// coming up. Every hardware indicator therefore has nothing to draw for the
// first few seconds and falls back to a neutral "unknown" glyph, which is the
// one thing a status bar must never show: a slot that looks live but carries no
// information.
//
// So we do what a desktop shell does: draw the last values we knew, immediately,
// and reconcile when the daemons answer. The cache is a handful of scalars in a
// small INI file under $XDG_CACHE_HOME/qypr — it is read once before the first
// frame (a local file read, never a daemon round trip) and rewritten, debounced,
// as state changes.
//
// It is a *cache*, not state of record: a missing, stale, truncated or garbage
// file must always be survivable, because it is written asynchronously and the
// machine can lose power at any point. Every read falls back to the compiled
// default, and the first live push from a backend overwrites whatever was
// seeded — so the worst case is that one frame shows a slightly old battery
// percentage.

#pragma once

#include <string>

// The snapshot structs by value — SystemBackends only forward-declares the
// backend classes, and we hold a copy of each snapshot between load() and seed().
#include "system/BatteryBackend.hpp"
#include "system/BluetoothBackend.hpp"
#include "system/BrightnessBackend.hpp"
#include "system/VolumeBackend.hpp"
#include "system/WifiBackend.hpp"
#include "ui/statusbar/StatusIndicator.hpp"  // SystemBackends

namespace qypr {

class EventLoop;

class StateCache {
public:
    // Read the cache file. Never fails: an absent or unparsable file simply
    // leaves every value at its default, and seed() then does nothing useful.
    void load();

    // Push the loaded values into the backends that support seeding. Call
    // before the first paint and before the backends are started, so the first
    // frame carries real numbers. Backends that have already produced a live
    // reply ignore the seed.
    void seed(const SystemBackends& backends);

    // Begin persisting state changes. Records the backends to sample and the
    // loop to schedule the debounced write on.
    void track(EventLoop& loop, const SystemBackends& backends);

    // Note that something may have changed. Cheap and safe to call often (it is
    // wired to the repaint hook, which also fires for hover and animation): the
    // first call arms a one-shot debounce timer, and subsequent calls before it
    // fires are free. The write itself is skipped when the serialized state is
    // byte-identical to what is already on disk, so pointer motion never causes
    // file I/O.
    void noteChanged();

    // Serialize the tracked backends to the cache format. Public for testing.
    [[nodiscard]] std::string serialize() const;

    // $XDG_CACHE_HOME/qypr/bar-state, else $HOME/.cache/qypr/bar-state.
    static std::string defaultPath();

private:
    void flush();

    // Debounce window. Long enough that a volume drag or a wifi scan storm
    // collapses into one write, short enough that a reboot moments after a real
    // change still seeds from it.
    static constexpr int kDebounceMs = 5000;

    std::string path_{defaultPath()};
    // Values read at startup; consumed by seed() and then never used again.
    BatterySnapshot battery_;
    VolumeSnapshot volume_;
    WifiSnapshot wifi_;
    BluetoothSnapshot bluetooth_;
    BrightnessSnapshot brightness_;
    bool loaded_ = false;

    EventLoop* loop_ = nullptr;
    const SystemBackends* backends_ = nullptr;
    int timer_ = -1;           // armed one-shot debounce, -1 when idle
    std::string lastWritten_;  // suppresses no-op writes
};

}  // namespace qypr
