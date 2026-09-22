// LockController.hpp - Authentication and lock-screen state policy.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "auth/PamAuthenticator.hpp"
#include "core/SecureBuffer.hpp"
#include "ui/Theme.hpp"

namespace qypr {

class EventLoop;
class RenderHost;

struct LockSnapshot {
    bool revealed = false;
    bool unlocking = false;
    bool hasError = false;
    std::string statusMessage;
    double reveal = 0.0;
};

// Owns the lock policy and authentication state. It deliberately exposes no
// power or rendering capability: a successful PAM result is the only path to
// RenderHost::requestUnlock(). The secret remains a SecureBuffer throughout.
class LockController : public theme::ThemeAware {
public:
    LockController(EventLoop& loop, RenderHost& host, PamAuthenticator& pam);
    ~LockController() override;

    LockController(const LockController&) = delete;
    LockController& operator=(const LockController&) = delete;

    void setTheme(const theme::State& state) override;

    void wake();
    void collapse();
    bool revealed() const { return revealed_; }
    bool unlocking() const { return unlocking_; }
    bool hasError() const { return hasError_; }
    size_t passwordLength() const { return password_.size(); }
    double reveal(int64_t now) const { return revealAnim_.value(now); }
    bool animating(int64_t now) const { return revealAnim_.active(now); }
    LockSnapshot snapshot(int64_t now) const;

    // Commands used by LockInput. Keeping these here ensures all mutations of
    // the secret and all authentication transitions remain in one policy seam.
    void appendText(std::string_view utf8);
    void backspace();
    void clearPassword();
    void submitPassword();

    // The host supplies the optional per-second work (currently MPRIS polling)
    // without making the controller depend on the audio view.
    void setOnTick(std::function<void()> callback) { onTick_ = std::move(callback); }
    // Collapse also closes the power pill, which is owned by a separate policy
    // object. The controller invokes this hook from its timer and Escape path.
    void setOnCollapse(std::function<void()> callback) { onCollapse_ = std::move(callback); }

#ifdef TESTING
    // Test-only seam. Production code can only observe the password length.
    SecureBuffer& password() { return password_; }
#endif

private:
    void restartHideTimer();
    void onAuthResult(PamAuthenticator::Result result, const std::string& message);

    EventLoop& loop_;
    RenderHost& host_;
    PamAuthenticator& pam_;
    int hideTimer_ = -1;
    int tickTimer_ = -1;
    std::function<void()> onTick_;
    std::function<void()> onCollapse_;

    bool revealed_ = false;
    bool unlocking_ = false;
    SecureBuffer password_;
    std::string statusMessage_;
    bool hasError_ = false;
    Animated revealAnim_{0};
};

}  // namespace qypr
