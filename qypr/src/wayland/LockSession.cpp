#include "wayland/LockSession.hpp"

#include "ext-session-lock-v1-client-protocol.h"

#include "wayland/WaylandDisplay.hpp"

namespace qypr {

namespace {
const ext_session_lock_v1_listener kLockListener = {
    .locked = LockSession::onLocked,
    .finished = LockSession::onFinished,
};
}  // namespace

LockSession::LockSession(WaylandDisplay& display) : display_(display) {}

LockSession::~LockSession() {
    // If still holding a granted lock at teardown, release it cleanly.
    if (lock_) {
        if (locked_)
            ext_session_lock_v1_unlock_and_destroy(lock_);
        else
            ext_session_lock_v1_destroy(lock_);
        lock_ = nullptr;
    }
}

bool LockSession::lock() {
    if (!display_.lockManager()) return false;
    lock_ = ext_session_lock_manager_v1_lock(display_.lockManager());
    ext_session_lock_v1_add_listener(lock_, &kLockListener, this);
    display_.setActiveLock(lock_);
    display_.createLockSurfaces(lock_);
    return true;
}

void LockSession::unlock() {
    if (!lock_) return;
    if (locked_) {
        ext_session_lock_v1_unlock_and_destroy(lock_);
    } else {
        ext_session_lock_v1_destroy(lock_);
    }
    lock_ = nullptr;
    locked_ = false;
    display_.setActiveLock(nullptr);
    // The unlock request is only queued in libwayland's output buffer; without
    // a roundtrip the process would exit before the compositor receives it,
    // leaving the session locked on a frozen screen. Block until it lands.
    display_.roundtrip();
}

void LockSession::onLocked(void* data, ext_session_lock_v1*) {
    auto* self = static_cast<LockSession*>(data);
    self->locked_ = true;
    if (self->onLocked_) self->onLocked_();
}

void LockSession::onFinished(void* data, ext_session_lock_v1*) {
    auto* self = static_cast<LockSession*>(data);
    // Compositor refused or revoked the lock; the object is now inert.
    self->locked_ = false;
    if (self->onFinished_) self->onFinished_();
}

}  // namespace qypr
