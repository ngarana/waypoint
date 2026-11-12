// FingerprintAuthenticator.hpp - Parallel fingerprint unlock for the lock screen.
//
// Fingerprint verification races password auth: the first success unlocks
// (LockController owns that policy). The finger side talks to fprintd over
// D-Bus — Claim, VerifyStart, VerifyStatus, VerifyStop, Release — the way
// hyprlock and gnome-shell do, rather than running pam_fprintd on a thread as
// swaylock#445 does. See docs/FINGERPRINT.md for the full rationale; in short:
//
//   * A D-Bus scan can be ended at any moment (VerifyStop). A pam_authenticate()
//     blocked inside pam_fprintd cannot be cancelled, so stop() used to leave a
//     detached thread holding the reader until its `timeout=` expired.
//   * Suspend: suspend() ends the scan (keeping the claim) before the machine
//     sleeps and resume() starts a fresh one, under SleepMonitor's logind delay
//     inhibitor — the standard sequence hyprlock implements.
//   * No PAM timeout cycle: one scan stays open for as long as the lock wants
//     it, so the reader is not re-opened (and, on Goodix sensors, USB-reset)
//     every 30 s.
//
// Everything runs on the loop thread; there are no worker threads. A single
// reconcile() step compares what is wanted (a live scan while start() is in
// effect and the machine is awake) with what is held (claim, scan) and issues at
// most one fprintd call at a time, so stop/suspend/resume can never interleave
// two calls on the reader.
//
// Failures that show no sign of a working reader — claim refused because sudo
// or polkit holds it, fprintd erroring a scan, fprintd restarting — back off
// 0.5 → 1 → … → 16 s and after kMaxSilentFailures in a row (~30 s) report
// Unavailable, so the lock quietly stays password-only. Any finger status
// (match, no-match, "try again") resets that budget, as does a resume. A
// missing reader, no enrolled prints, or a polkit denial is Unavailable at once.
//
// Wrong fingers: after kMaxMisses non-matching scans the authenticator reports
// LockedOut, releases the reader, and refuses every later start() for its
// lifetime — i.e. until this lock process ends with a password unlock. The
// count survives stop/start and suspend/resume, so neither can buy an attacker
// fresh attempts. Five, as Touch ID allows before it asks for the password.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "auth/FprintdTransport.hpp"

namespace qypr {

class EventLoop;

class FingerprintAuthenticator {
public:
    // LockedOut: too many wrong fingers; only the password can unlock now.
    enum class Result { Success, Unavailable, LockedOut };
    using Done = std::function<void(Result, std::string message)>;
    // Transient prompts ("Place your finger…", "Fingerprint not recognised").
    // Runs on the loop thread; `isError` marks a miss or a retry request.
    using Info = std::function<void(std::string message, bool isError)>;
    using Trace = std::function<void(const std::string& event)>;

    FingerprintAuthenticator(EventLoop& loop, FprintdTransport& fprintd);
    ~FingerprintAuthenticator();

    FingerprintAuthenticator(const FingerprintAuthenticator&) = delete;
    FingerprintAuthenticator& operator=(const FingerprintAuthenticator&) = delete;

    bool running() const { return running_; }

    // Begin background verification. `done` runs on the loop thread at most
    // once per start(): Success (unlock), Unavailable (hide the fingerprint
    // UI, keep password auth) or LockedOut (likewise, and say why). No-op if
    // already running; reports LockedOut straight away once locked out.
    void start(Done done, Info info = {});
    bool lockedOut() const { return misses_ >= kMaxMisses; }
    int misses() const { return misses_; }

    // End verification and release the reader. Non-blocking; any result still
    // in flight is dropped.
    void stop();

    // Suspend handling (SleepMonitor). suspend() ends any scan in progress but
    // keeps the claim, then calls `ready` once fprintd has acknowledged — the
    // caller lets the system sleep after that. resume() starts a new scan with
    // a fresh retry budget if start() is still in effect.
    void suspend(std::function<void()> ready);
    void resume();
    bool sleeping() const { return sleeping_; }

    // Diagnostics (--fingerprint-test): one line per fprintd call and status.
    void setTrace(Trace trace) { trace_ = std::move(trace); }

    // Consecutive failures with no sign of a working reader before giving up.
    // With the doubling back-off from 500 ms that spans ~30 s: enough to
    // outlast another pam_fprintd client (sudo, polkit) that still holds the
    // reader when the lock starts.
    static constexpr int kMaxSilentFailures = 7;

    // Non-matching scans allowed per lock before only the password unlocks.
    static constexpr int kMaxMisses = 5;

private:
    bool wantScan() const { return running_ && !sleeping_; }
    void reconcile();
    void onClaimed(const FprintdError& error);
    void onVerifyStarted(const FprintdError& error);
    void onVerifyStopped(const FprintdError& error);
    void onReleased(const FprintdError& error);
    void onStatus(const std::string& result, bool done);
    void onVanished();

    // A failure with no sign of a working reader. Arms the back-off timer;
    // false (after reporting Unavailable) once the budget is spent.
    bool silentFailure(const std::string& why);
    void restartScan(std::chrono::milliseconds pause);
    void succeed();
    void giveUp(const std::string& why);
    void lockOut();
    void armTimer(std::chrono::milliseconds delay);
    void cancelTimer();
    void report(Result result, std::string message);
    void notify(std::string message, bool isError);
    void trace(const std::string& event) const;
    // Completion wrapper: drops the callback if this object is gone.
    FprintdTransport::Reply guard(void (FingerprintAuthenticator::*handler)(const FprintdError&));

    EventLoop& loop_;
    FprintdTransport& fprintd_;
    Done done_;
    Info info_;
    Trace trace_;

    bool running_ = false;       // start() in effect
    bool sleeping_ = false;      // between suspend() and resume()
    bool claimed_ = false;       // we hold the reader's claim
    bool scanning_ = false;      // VerifyStart succeeded; VerifyStop still owed
    bool inflight_ = false;      // one fprintd call outstanding
    bool needsRestart_ = false;  // the live scan ended: stop it, scan again
    bool needsReclaim_ = false;  // device state suspect: release, claim again
    bool releaseFirst_ = false;  // a claim was refused: drop any stale claim of ours
    int silentFailures_ = 0;
    int misses_ = 0;                  // wrong fingers so far; never reset (see kMaxMisses)
    int timer_ = -1;                  // back-off / pause before the next attempt
    std::function<void()> quiesced_;  // suspend()'s `ready`, until quiet

    // Bumped by start()/stop(); posted results from an earlier run are dropped.
    // Shared so posted callbacks can also tell whether this object still exists.
    std::shared_ptr<uint64_t> run_ = std::make_shared<uint64_t>(0);

    // Pause between a missed finger and the next scan. A member (not a
    // constant) only so tests can shrink it; silent failures double it.
    std::chrono::milliseconds retryBackoff_{500};
};

}  // namespace qypr
