#include "ui/LockController.hpp"

#include <utility>

#include "core/EventLoop.hpp"
#include "core/Interfaces.hpp"

namespace qypr {

namespace {
constexpr int kHideTimeoutMs = 15000;
}

LockController::LockController(EventLoop& loop, RenderHost& host, PamAuthenticator& pam)
    : loop_(loop),
      host_(host),
      pam_(pam) {
    // Repaint once a second so the clock and any injected lock-screen poller
    // stay current. The renderer owns the clock; this owns the lifecycle.
    tickTimer_ = loop_.addTimer(1000, true, [this] {
        if (onTick_) { onTick_(); }
        host_.invalidate();
    });
}

LockController::~LockController() {
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
    return {.revealed = revealed_,
            .unlocking = unlocking_,
            .hasError = hasError_,
            .statusMessage = statusMessage_,
            .reveal = clamp01(revealAnim_.value(now))};
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
            host_.requestUnlock();
            break;
        case PamAuthenticator::Result::Failure:
            statusMessage_ = "Authentication failed";
            hasError_ = true;
            password_.clear();
            break;
        case PamAuthenticator::Result::Error:
            statusMessage_ = message.empty() ? "Authentication error" : ("Error: " + message);
            hasError_ = true;
            password_.clear();
            break;
    }
    host_.invalidate();
}

}  // namespace qypr
