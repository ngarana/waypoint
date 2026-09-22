// LockScreen.hpp - Lock UI: layout, reveal state machine, input, power menu.
//
// Port of LockScreen.qml. Owns the lock-specific UI (clock, password, status,
// audio, notifications, power pill). Input is delegated from Shell via the
// handle*() methods. Idle/dim and video background are managed by Shell.

#pragma once

#include <array>
#include <string>

#include "auth/PamAuthenticator.hpp"
#include "core/Interfaces.hpp"
#include "core/SecureBuffer.hpp"
#include "ui/ActionButton.hpp"
#include "ui/Clock.hpp"
#include "ui/Notification.hpp"
#include "ui/PasswordField.hpp"
#include "ui/ConfirmPopover.hpp"
#include "ui/StatusMessage.hpp"
#include "ui/Theme.hpp"

namespace qypr {

class EventLoop;
class SystemActions;
class AudioController;

class LockScreen : public theme::ThemeAware {
public:
    LockScreen(EventLoop& loop, RenderHost& host, PamAuthenticator& pam, SystemActions& power);

    // Optional audio panel, injected once MPRIS is available.
    void setAudioController(AudioController* audio) { audio_ = audio; }

    // Windows 11-style notification cards (bottom-left). The owner pushes the
    // current set whenever it changes; the view reconciles by id.
    void setNotifications(std::vector<Notification> notes) {
        notifications_.update(std::move(notes));
    }

    // Render the whole UI at a given output size.
    void draw(cairo_t* cr, int width, int height, int scale);
    bool isAnimating() const;

    // ThemeAware: cascade to every owned widget (audio is borrowed — its
    // owner binds it).
    void setTheme(const theme::State& state) override;

    // Input handlers delegated by Shell
    void handleTextInput(const std::string& utf8);
    void handleSpecialKey(uint32_t keysym, uint32_t modifiers);
    void handlePointerMotion(int w, int h, double x, double y);
    void handlePointerButton(int w, int h, double x, double y, uint32_t button, bool pressed);
    void handlePointerLeave();

    double getReveal(int64_t now) const { return revealAnim_.value(now); }

    // True while a modal (power confirmation dialog) should consume all
    // input. Queried by Shell for routing; LockScreen knows nothing about
    // what else exists.
    bool modalActive() const { return confirmPopover_.active(); }

    // Called by Shell when any input arrives to drive the reveal state machine
    // (without touching idle state, which Shell manages).
    void wake();

#ifdef TESTING
    // Test seam: lets unit tests drive the password field without a real
    // keyboard. Not compiled in production builds.
    SecureBuffer& password() { return password_; }
#endif

private:
    void reveal();
    void collapse();
    void restartHideTimer();
    void submitPassword();
    void onAuthResult(PamAuthenticator::Result result, const std::string& message);
    void updateHover(int w, int h, double x, double y);
    void expandPower();    // open the pill (anchor clicked)
    void collapsePower();  // close the pill
    // Show the power confirmation popover for button index i, anchored to the pill.
    void showPowerConfirm(int index, int w, int h);

    // Pure geometry shared by draw() and hit-testing.
    // powerRowRect: the full expanded pill rect (all 5 buttons worth of space).
    // powerButtonRect(i): centre rect for action button i (0-3, left-to-right)
    //   inside the row — valid only when revealed.
    // powerAnchorRect: the single trigger/power button, always bottom-right.
    Rect powerRowRect(int w, int h) const;
    Rect powerButtonRect(int index, int w, int h) const;
    Rect powerAnchorRect(int w, int h) const;

    EventLoop& loop_;
    RenderHost& host_;
    PamAuthenticator& pam_;
    SystemActions& power_;
    AudioController* audio_ = nullptr;

    // State
    bool revealed_ = false;
    bool unlocking_ = false;
    // The in-progress input. A SecureBuffer, not a std::string: it never
    // reallocates (a growth would free a block still holding a prefix of the
    // secret), it is mlock()ed, and Backspace/clear wipe the bytes they drop.
    // QL-3 in docs/LOCK_SECURITY_REVIEW.md.
    SecureBuffer password_;
    std::string statusMessage_;
    bool hasError_ = false;
    Animated revealAnim_{0};

    // Power pill expand state — driven exclusively by clicking the anchor button,
    // independent of the general reveal state machine.
    bool powerExpanded_ = false;
    Animated powerExpandAnim_{0};

    // Widgets
    Clock clock_;
    PasswordField passwordField_;
    StatusMessage status_;
    NotificationView notifications_;
    std::array<ActionButton, 4> powerButtons_;
    ActionButton alwaysPower_;
    ConfirmPopover confirmPopover_;

    bool pointerDown_ = false;
    int hideTimer_ = -1;
    int clockTimer_ = -1;
    int lastW_ = 1920;  // last known output width  (updated every draw)
    int lastH_ = 1080;  // last known output height
};

}  // namespace qypr
