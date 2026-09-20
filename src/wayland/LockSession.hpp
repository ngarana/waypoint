// LockSession.hpp - Lifecycle of a single ext-session-lock-v1 lock.
//
// lock() grabs the session and covers every output; the compositor confirms
// with `locked` (secure) or refuses with `finished`. unlock() is only valid
// once locked — enforced here so the protocol is never misused.

#pragma once

#include <functional>

struct ext_session_lock_v1;

namespace qypr {

class WaylandDisplay;

class LockSession {
public:
    explicit LockSession(WaylandDisplay& display);
    ~LockSession();

    using Callback = std::function<void()>;
    void setOnLocked(Callback cb) { onLocked_ = std::move(cb); }
    void setOnFinished(Callback cb) { onFinished_ = std::move(cb); }

    // Acquire the lock and create surfaces on all outputs.
    bool lock();

    // Release the lock (only if it was granted) and tear down.
    void unlock();

    bool locked() const { return locked_; }

    // Wayland C callbacks (public so the listener table can bind them).
    static void onLocked(void*, ext_session_lock_v1*);
    static void onFinished(void*, ext_session_lock_v1*);

private:
    WaylandDisplay& display_;
    ext_session_lock_v1* lock_ = nullptr;
    bool locked_ = false;
    Callback onLocked_;
    Callback onFinished_;
};

}  // namespace qypr
