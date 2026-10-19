// FingerprintAuthenticator.hpp - Parallel fingerprint unlock for the lock screen.
//
// Inspiration (see docs/FINGERPRINT.md):
//   * SDDM src/helper/backend/PamBackend.cpp ::converse(): PAM_TEXT_INFO and
//     PAM_ERROR_MSG are forwarded to the UI while pam_authenticate() blocks.
//     pam_fprintd reports "Place your finger on the reader", retries and
//     timeouts exactly this way.
//   * swaylock PR #445 ("pam: add optional fingerprint authentication"): a
//     single PAM stack cannot race password (pam_unix) against fingerprint
//     (pam_fprintd) because the modules run sequentially and whichever comes
//     first blocks. swaylock spawns a parallel child driving a dedicated
//     `swaylock-fingerprint` service (only pam_fprintd.so) with an empty
//     password; whichever method succeeds first unlocks.
//   * kscreenlocker: the greeter runs that background fingerprint auth in
//     parallel to the password prompt and surfaces progress in the theme.
//
// This class is the qypr-lock equivalent of swaylock's fingerprint child: a
// worker thread drives pam_authenticate() on a dedicated service
// ("qypr-fingerprint", shipped in pam/qypr-fingerprint) answering every echo
// prompt with an empty reply. Password auth (PamAuthenticator on the
// "qypr-lock" service, which must stay free of pam_fprintd) keeps running
// untouched on its own thread; the first success wins and LockController
// stops the loser.
//
// Lifecycle: start() spawns the worker, which retries failed attempts until
// stop() is called or the reader proves unusable (Unavailable): a hard PAM
// error (service/module failed to load), or kMaxSilentFailures attempts in a
// row that never reached the reader (no device, no enrolled finger, reader
// claimed by another client). Return codes cannot make that call on their
// own — see workerMain(). stop() is non-blocking: a pam_authenticate() already
// in flight cannot be cancelled portably, so the worker is left to finish and
// its late result is dropped via a generation counter. Only the destructor
// joins.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

namespace qypr {

class EventLoop;

class FingerprintAuthenticator {
public:
    enum class Result { Success, Unavailable };
    using Done = std::function<void(Result, std::string message)>;
    // Mirrors PamAuthenticator::Info: transient PAM_TEXT_INFO / PAM_ERROR_MSG
    // text (e.g. "Place your right index finger..."). Runs on the loop thread.
    using Info = std::function<void(std::string message, bool isError)>;

    FingerprintAuthenticator(EventLoop& loop, std::string service = "qypr-fingerprint");
    ~FingerprintAuthenticator();

    FingerprintAuthenticator(const FingerprintAuthenticator&) = delete;
    FingerprintAuthenticator& operator=(const FingerprintAuthenticator&) = delete;

    bool running() const { return running_.load(); }

    // Begin background verification. `done` runs on the loop thread exactly
    // once: Success (unlock now, stop the worker) or Unavailable (hide the
    // fingerprint UI, keep password auth). Transient mismatches/timeouts are
    // reported via `info` and retried internally. No-op if already running.
    void start(Done done, Info info = {});

    // Ask the worker to finish. Non-blocking by design (see above): a blocked
    // pam_authenticate() runs to its timeout, then exits quietly.
    void stop();

    // Cheap static gate: is pam_fprintd even installed? The worker still
    // reports Unavailable at runtime for unenrolled users / missing readers;
    // this just lets the UI skip starting the thread entirely.
    static bool moduleInstalled();

    // Is this instance's PAM service file installed in `pamDir`? Without it
    // PAM falls back to the deny-all "other" stack, which fails instantly and
    // logs a pam_warn line per attempt; don't start the worker at all.
    bool serviceInstalled(const std::string& pamDir = "/etc/pam.d") const;

    // Consecutive silent attempts (the module never reached the reader) before
    // giving up. With the doubling back-off from 500 ms that spans ~30 s:
    // enough to outlast another pam_fprintd client (sudo, polkit) that still
    // holds the reader when the lock starts.
    static constexpr int kMaxSilentFailures = 7;

private:
    void workerMain(const Done& done, Info info, uint64_t generation);

    EventLoop& loop_;
    std::string service_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{true};
    std::atomic<uint64_t> generation_{0};
    // Pause after a failed attempt; doubled per consecutive silent failure.
    // A member (not a constant) only so tests can shrink it.
    std::chrono::milliseconds retryBackoff_{500};
    std::thread worker_;
};

}  // namespace qypr
