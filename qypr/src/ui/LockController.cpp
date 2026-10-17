#include "ui/LockController.hpp"

#include <utility>

#include "core/EventLoop.hpp"
#include "core/Interfaces.hpp"

namespace qypr {

namespace {
constexpr int kHideTimeoutMs = 15000;
}

LockController::LockController(EventLoop& loop, RenderHost& host, PamAuthenticator& pam,
                               FingerprintAuthenticator* fp)
    : loop_(loop),
      host_(host),
      pam_(pam),
      fp_(fp) {
    // Repaint once a second so the clock and any injected lock-screen poller
    // stay current. The renderer owns the clock; this owns the lifecycle.
    tickTimer_ = loop_.addTimer(1000, true, [this] {
        if (onTick_) { onTick_(); }
        host_.invalidate();
    });
    // SDDM-style info channel: surface PAM_TEXT_INFO / PAM_ERROR_MSG from the
    // password stack (e.g. account notices) on the status line.
    pam_.setInfoHandler(
        [this](std::string message, bool isError) { onPamInfo(std::move(message), isError); });
}

LockController::~LockController() {
    stopFingerprint();
    if (hideTimer_ >= 0) { loop_.removeTimer(hideTimer_); }
    if (tickTimer_ >= 0) { loop_.removeTimer(tickTimer_); }
}

void LockController::setTheme(const theme::State& state) {
    theme::ThemeAware::setTheme(state);
}

void LockController::wake() {
    if (!revealed_) {
        revealed_ = true;
        revealAnim_.animateTo(1.0, theme().anim.reveal, ease::inOutQuad);
    }
    restartHideTimer();
    host_.invalidate();
}

void LockController::collapse() {
    revealed_ = false;
    revealAnim_.animateTo(0.0, theme().anim.reveal, ease::inOutQuad);
    if (onCollapse_) { onCollapse_(); }
    if (hideTimer_ >= 0) {
        loop_.removeTimer(hideTimer_);
        hideTimer_ = -1;
    }
    host_.invalidate();
}

LockSnapshot LockController::snapshot(int64_t now) const {
    return {
        .revealed = revealed_,
        .unlocking = unlocking_,
        .hasError = hasError_,
        .statusMessage = statusMessage_,
        .reveal = clamp01(revealAnim_.value(now)),
        .fingerprint = fingerprint_,
        .fingerprintMessage = fingerprintMessage_,
    };
}

void LockController::appendText(std::string_view utf8) {
    password_.append(utf8);
    wake();
}

void LockController::backspace() {
    password_.popBack();
    wake();
}

void LockController::clearPassword() {
    password_.clear();
}

void LockController::submitPassword() {
    if (password_.empty() || pam_.busy()) { return; }
    statusMessage_ = "Authenticating...";
    hasError_ = false;
    // Move the fixed-capacity buffer into the PAM worker. The moved-from
    // source is immediately usable again, and the worker wipes its copy when
    // it exits (QL-3).
    pam_.authenticate(std::move(password_),
                      [this](PamAuthenticator::Result result, const std::string& message) {
                          onAuthResult(result, message);
                      });
    host_.invalidate();
}

void LockController::restartHideTimer() {
    if (hideTimer_ >= 0) { loop_.removeTimer(hideTimer_); }
    hideTimer_ = loop_.addTimer(kHideTimeoutMs, false, [this] {
        hideTimer_ = -1;
        if (password_.empty()) { collapse(); }
    });
}

void LockController::onAuthResult(PamAuthenticator::Result result, const std::string& message) {
    switch (result) {
        case PamAuthenticator::Result::Success:
            statusMessage_ = "Unlocking...";
            hasError_ = false;
            unlocking_ = true;
            stopFingerprint();  // password won the race; drop the pending finger
            host_.requestUnlock();
            break;
        case PamAuthenticator::Result::Failure:
            statusMessage_ = "Authentication failed";
            hasError_ = true;
            password_.clear();
            // Fingerprint keeps waiting in the background (if enabled), so a
            // typo does not force the user back to typing.
            break;
        case PamAuthenticator::Result::Error:
            statusMessage_ = message.empty() ? "Authentication error" : ("Error: " + message);
            hasError_ = true;
            password_.clear();
            break;
    }
    host_.invalidate();
}

void LockController::startFingerprint() {
    if (fp_ == nullptr || !fingerprintEnabled_) { return; }
    if (fp_->running()) { return; }
    fingerprint_ = FingerprintState::Waiting;
    fingerprintMessage_.clear();
    fingerprintError_ = false;
    fp_->start(
        [this](FingerprintAuthenticator::Result result, const std::string& message) {
            onFingerprintResult(result, message);
        },
        [this](const std::string& message, bool isError) { onFingerprintInfo(message, isError); });
}

void LockController::stopFingerprint() {
    if (fp_ != nullptr) { fp_->stop(); }
    if (fingerprint_ != FingerprintState::Disabled) {
        fingerprint_ = FingerprintState::Disabled;
        fingerprintMessage_.clear();
        fingerprintError_ = false;
    }
}

void LockController::setFingerprintEnabled(bool on) {
    fingerprintEnabled_ = on;
    if (!on) { stopFingerprint(); }
}

void LockController::onFingerprintResult(FingerprintAuthenticator::Result result,
                                         const std::string& message) {
    switch (result) {
        case FingerprintAuthenticator::Result::Success:
            if (unlocking_) { return; }  // password won the race first
            statusMessage_ = "Unlocking...";
            hasError_ = false;
            unlocking_ = true;
            password_.clear();
            host_.requestUnlock();
            break;
        case FingerprintAuthenticator::Result::Unavailable:
            fingerprint_ = FingerprintState::Disabled;
            fingerprintMessage_.clear();
            fingerprintError_ = false;
            // Only clear the status line if it was showing the finger prompt;
            // a password error or "Authenticating..." takes precedence.
            if (!pam_.busy() && password_.empty() && !unlocking_) {
                statusMessage_.clear();
                hasError_ = false;
            }
            (void)message;
            break;
    }
    host_.invalidate();
}

void LockController::onFingerprintInfo(const std::string& message, bool isError) {
    if (fingerprint_ == FingerprintState::Disabled) { return; }
    fingerprint_ = FingerprintState::Prompting;
    fingerprintMessage_ = message;
    fingerprintError_ = isError;
    // The finger prompt is a hint, not an override: while the user is typing
    // or a password attempt is in flight, keep the password status.
    if (!pam_.busy() && password_.empty() && !unlocking_) {
        statusMessage_ = message;
        hasError_ = isError;
    }
    host_.invalidate();
}

void LockController::onPamInfo(std::string message, bool isError) {
    // Password-stack notices while authenticating (SDDM forwards these to the
    // greeter the same way). Outside an attempt there is nothing to annotate.
    if (!pam_.busy() || unlocking_) { return; }
    statusMessage_ = std::move(message);
    hasError_ = isError;
    host_.invalidate();
}

}  // namespace qypr
