// FingerprintAuthenticator.cpp - fprintd-driven fingerprint unlock (see header).
#include "auth/FingerprintAuthenticator.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "core/EventLoop.hpp"

namespace qypr {

namespace {

// Permanent for this lock: no reader, no fprintd, nothing enrolled, or polkit
// says no. Retrying cannot help, so report Unavailable at once.
bool isFatal(const FprintdError& e) {
    static constexpr std::array<const char*, 6> kFatal = {
        fprintd_error::kNoSuchDevice,     fprintd_error::kServiceUnknown,
        fprintd_error::kNoEnrolledPrints, fprintd_error::kPermissionDenied,
        fprintd_error::kAccessDenied,     "org.freedesktop.DBus.Error.ServiceNotFound",
    };
    return std::ranges::any_of(kFatal, [&e](const char* name) { return e.name == name; });
}

std::string describe(const FprintdError& e) {
    return e.message.empty() ? e.name : e.message;
}

// Statuses that ask for another touch within the same scan (done == false).
const char* retryPrompt(const std::string& result) {
    if (result == "verify-retry-scan") { return "Scan again"; }
    if (result == "verify-swipe-too-short") { return "Swipe was too short, try again"; }
    if (result == "verify-finger-not-centered") { return "Finger not centred, try again"; }
    if (result == "verify-remove-and-retry") { return "Lift your finger and try again"; }
    return nullptr;
}

constexpr const char* kLockedOutMessage = "Too many fingerprint attempts";

}  // namespace

FingerprintAuthenticator::FingerprintAuthenticator(EventLoop& loop, FprintdTransport& fprintd)
    : loop_(loop),
      fprintd_(fprintd) {
    fprintd_.setHandlers([this](const std::string& result, bool done) { onStatus(result, done); },
                         [this] { onVanished(); });
}

FingerprintAuthenticator::~FingerprintAuthenticator() {
    cancelTimer();
    fprintd_.setHandlers({}, {});
    // Best effort, fire-and-forget: the process is usually exiting, and fprintd
    // drops a vanished client's claim anyway. No completion may touch `this`.
    if (scanning_) {
        fprintd_.verifyStop([](const FprintdError&) {});
    }
    if (claimed_) {
        fprintd_.release([](const FprintdError&) {});
    }
}

FprintdTransport::Reply FingerprintAuthenticator::guard(
    void (FingerprintAuthenticator::*handler)(const FprintdError&)) {
    return [this, alive = std::weak_ptr<uint64_t>(run_), handler](const FprintdError& e) {
        if (alive.expired()) { return; }
        (this->*handler)(e);
    };
}

void FingerprintAuthenticator::start(Done done, Info info) {
    if (running_) { return; }
    ++*run_;
    done_ = std::move(done);
    info_ = std::move(info);
    if (lockedOut()) {
        // No fresh attempts for a new start(): the reader is not touched.
        trace("start refused: locked out");
        report(Result::LockedOut, kLockedOutMessage);
        return;
    }
    running_ = true;
    silentFailures_ = 0;
    needsRestart_ = false;
    cancelTimer();
    trace("start");
    reconcile();
}

void FingerprintAuthenticator::stop() {
    if (!running_ && !claimed_ && !scanning_) { return; }
    running_ = false;
    ++*run_;
    needsRestart_ = false;
    cancelTimer();
    trace("stop");
    reconcile();
}

void FingerprintAuthenticator::suspend(std::function<void()> ready) {
    sleeping_ = true;
    quiesced_ = std::move(ready);
    cancelTimer();  // no attempts while asleep; resume() starts afresh
    trace("suspend: ending scan, keeping claim");
    reconcile();
}

void FingerprintAuthenticator::resume() {
    sleeping_ = false;
    quiesced_ = nullptr;  // SleepMonitor's own timeout already let sleep proceed
    silentFailures_ = 0;
    needsRestart_ = false;
    trace("resume");
    reconcile();
}

// The one place calls are issued. Order matters: end a scan before anything
// else, release before re-claiming, and only then start the next attempt.
void FingerprintAuthenticator::reconcile() {
    if (inflight_) { return; }  // its completion calls back in here

    if (scanning_ && (!wantScan() || needsRestart_)) {
        inflight_ = true;
        trace("VerifyStop");
        fprintd_.verifyStop(guard(&FingerprintAuthenticator::onVerifyStopped));
        return;
    }
    if (scanning_) { return; }  // a wanted scan is live: wait for VerifyStatus

    if ((claimed_ && (!running_ || needsReclaim_)) || (!claimed_ && releaseFirst_ && wantScan())) {
        inflight_ = true;
        releaseFirst_ = false;
        trace("Release");
        fprintd_.release(guard(&FingerprintAuthenticator::onReleased));
        return;
    }

    // Quiet: no scan, no call in flight. Let a pending suspend proceed.
    if (sleeping_ && quiesced_) {
        auto ready = std::move(quiesced_);
        quiesced_ = nullptr;
        trace("quiesced");
        ready();
    }

    if (!wantScan() || timer_ >= 0) { return; }
    inflight_ = true;
    if (!claimed_) {
        trace("Claim");
        fprintd_.claim(guard(&FingerprintAuthenticator::onClaimed));
    } else {
        trace("VerifyStart");
        fprintd_.verifyStart(guard(&FingerprintAuthenticator::onVerifyStarted));
    }
}

void FingerprintAuthenticator::onClaimed(const FprintdError& error) {
    inflight_ = false;
    if (!error.failed()) {
        claimed_ = true;
        needsReclaim_ = false;
        trace("claimed");
    } else {
        trace("Claim failed: " + error.name + " " + error.message);
        if (wantScan()) {
            if (isFatal(error)) {
                giveUp(describe(error));
            } else {
                // Possibly our own stale claim (a Release that failed): drop it
                // before the next try. Harmless if someone else holds it.
                releaseFirst_ = error.name == fprintd_error::kAlreadyInUse;
                silentFailure(describe(error));
            }
        }
    }
    reconcile();
}

void FingerprintAuthenticator::onVerifyStarted(const FprintdError& error) {
    inflight_ = false;
    if (!error.failed()) {
        scanning_ = true;  // owed a VerifyStop from here on, wanted or not
        trace("scanning");
        if (wantScan()) { notify("Place your finger on the reader", false); }
    } else {
        trace("VerifyStart failed: " + error.name + " " + error.message);
        if (wantScan()) {
            if (isFatal(error)) {
                giveUp(describe(error));
            } else if (silentFailure(describe(error))) {
                // Claim lost (fprintd restarted) or the device is wedged
                // (e.g. still resuming): start over from a fresh claim.
                needsReclaim_ = true;
                if (error.name == fprintd_error::kClaimDevice) { claimed_ = false; }
            }
        }
    }
    reconcile();
}

void FingerprintAuthenticator::onVerifyStopped(const FprintdError& error) {
    inflight_ = false;
    // NoActionInProgress just means fprintd had already ended the scan (it does
    // that itself when the system suspends). Either way nothing is owed now.
    scanning_ = false;
    needsRestart_ = false;
    trace(error.failed() ? "VerifyStop: " + error.name : "stopped");
    reconcile();
}

void FingerprintAuthenticator::onReleased(const FprintdError& error) {
    inflight_ = false;
    claimed_ = false;
    trace(error.failed() ? "Release: " + error.name + " " + error.message : "released");
    reconcile();
}

void FingerprintAuthenticator::onStatus(const std::string& result, bool done) {
    trace("VerifyStatus " + result + (done ? " (done)" : ""));
    if (!scanning_ || needsRestart_) { return; }  // not a live scan of ours
    // While asleep fprintd ends the scan itself ("Cannot run while suspended",
    // reported as verify-unknown-error); suspend() is already stopping it.
    if (!wantScan()) { return; }

    if (result == "verify-match") {
        succeed();
        return;
    }
    if (result == "verify-no-match") {
        silentFailures_ = 0;  // the reader works; this was just the wrong finger
        if (++misses_ >= kMaxMisses) {
            lockOut();
            return;
        }
        notify("Fingerprint not recognised", true);
        restartScan(retryBackoff_);
        return;
    }
    if (const char* prompt = retryPrompt(result)) {
        silentFailures_ = 0;
        notify(prompt, true);
        if (done) { restartScan(retryBackoff_); }
        return;
    }
    if (!done) { return; }  // unknown progress report: the scan continues

    // verify-disconnected, verify-unknown-error, or a status we do not know
    // that ended the scan: no evidence the reader works. Re-claim after a
    // disconnect; a plain error only needs a new scan.
    if (silentFailure(result)) {
        if (result == "verify-disconnected") { needsReclaim_ = true; }
        needsRestart_ = true;
        reconcile();
    }
}

void FingerprintAuthenticator::onVanished() {
    trace("fprintd left the bus");
    // The claim and any scan died with that fprintd instance; a call still in
    // flight to it will fail and complete on its own.
    claimed_ = false;
    scanning_ = false;
    needsRestart_ = false;
    if (wantScan() && !silentFailure("fprintd restarted")) { return; }
    reconcile();
}

bool FingerprintAuthenticator::silentFailure(const std::string& why) {
    if (++silentFailures_ >= kMaxSilentFailures) {
        giveUp(why);
        return false;
    }
    armTimer(retryBackoff_ * (1U << static_cast<unsigned>(silentFailures_ - 1)));
    return true;
}

void FingerprintAuthenticator::restartScan(std::chrono::milliseconds pause) {
    needsRestart_ = true;
    armTimer(pause);  // the next VerifyStart waits for this; VerifyStop does not
    reconcile();
}

void FingerprintAuthenticator::succeed() {
    trace("match");
    running_ = false;
    cancelTimer();
    report(Result::Success, "");
    reconcile();  // VerifyStop, then Release
}

void FingerprintAuthenticator::giveUp(const std::string& why) {
    trace("unavailable: " + why);
    running_ = false;
    cancelTimer();
    report(Result::Unavailable, why);
    reconcile();
}

void FingerprintAuthenticator::lockOut() {
    trace("locked out after " + std::to_string(misses_) + " wrong fingers");
    running_ = false;
    cancelTimer();
    report(Result::LockedOut, kLockedOutMessage);
    reconcile();  // VerifyStop, then Release: the reader is handed back
}

void FingerprintAuthenticator::armTimer(std::chrono::milliseconds delay) {
    cancelTimer();
    timer_ = loop_.addTimer(delay.count(), false, [this] {
        timer_ = -1;  // one-shot timers are removed by the loop before firing
        reconcile();
    });
}

void FingerprintAuthenticator::cancelTimer() {
    if (timer_ >= 0) {
        loop_.removeTimer(timer_);
        timer_ = -1;
    }
}

// Results and prompts are posted, never called inline: the handlers unlock or
// repaint, and must not run inside a D-Bus callback of ours. A result from a
// run that stop() (or a later start()) ended is dropped.
void FingerprintAuthenticator::report(Result result, std::string message) {
    if (!done_) { return; }
    loop_.post([run = std::weak_ptr<uint64_t>(run_), id = *run_, cb = done_, result,
                message = std::move(message)] {
        auto current = run.lock();
        if (!current || *current != id) { return; }
        cb(result, message);
    });
}

void FingerprintAuthenticator::notify(std::string message, bool isError) {
    if (!info_) { return; }
    loop_.post([run = std::weak_ptr<uint64_t>(run_), id = *run_, cb = info_,
                message = std::move(message), isError] {
        auto current = run.lock();
        if (!current || *current != id) { return; }
        cb(message, isError);
    });
}

void FingerprintAuthenticator::trace(const std::string& event) const {
    if (trace_) { trace_(event); }
}

}  // namespace qypr
