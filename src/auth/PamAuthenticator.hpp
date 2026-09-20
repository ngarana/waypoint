// PamAuthenticator.hpp - Asynchronous PAM password authentication.
//
// pam_authenticate() blocks, so it runs on a worker thread; the result is
// posted back onto the event loop so all UI/state changes stay single-threaded.
// Mirrors LockController.qml's PAM flow (service "login").
//
// The secret is a SecureBuffer, and the worker takes ownership of it by rvalue
// reference — there is deliberately no std::string overload. A std::string
// parameter (or a by-value capture of one) would leave a copy of the password in
// freed heap memory that nothing ever wipes: see QL-3 in
// docs/LOCK_SECURITY_REVIEW.md.

#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "core/SecureBuffer.hpp"

namespace qypr {

class EventLoop;

class PamAuthenticator {
public:
    enum class Result { Success, Failure, Error };
    using Done = std::function<void(Result, std::string message)>;

    PamAuthenticator(EventLoop& loop, std::string service = "login");
    ~PamAuthenticator();

    bool busy() const { return busy_.load(); }

    // Begin authentication. `done` runs on the loop thread exactly once.
    // Returns false if a previous attempt is still running or password empty.
    // The buffer is moved to the worker (the caller's copy is left empty) and
    // wiped when the worker finishes.
    bool authenticate(SecureBuffer&& password, Done done);

private:
    EventLoop& loop_;
    std::string service_;
    std::atomic<bool> busy_{false};
    std::thread worker_;
};

}  // namespace qypr
