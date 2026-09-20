#include "ui/LockScreen.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <string_view>

#include "core/EventLoop.hpp"
#include "power/PowerManager.hpp"
#include "render/Painter.hpp"
#include "ui/AudioController.hpp"
#include "ui/Theme.hpp"
#include "wayland/Seat.hpp"  // Mod flags

namespace qypr {

namespace {
constexpr uint32_t kBtnLeft = 0x110;
constexpr int kHideTimeoutMs = 15000;
constexpr double kButtonDiameter = 52;

size_t utf8Count(std::string_view s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}
}  // namespace

LockScreen::LockScreen(EventLoop& loop, RenderHost& host, PamAuthenticator& pam,
                       PowerManager& power)
    : loop_(loop),
      host_(host),
      pam_(pam),
      power_(power) {
    struct Cfg {
        const char* icon;
        const char* label;
    };
    const Cfg cfg[4] = {
        {"⏾", "Suspend"},       // ⏾ moon/sleep
        {"󰒲", "Hibernate"},  // nerd font hibernate
        {"↻", "Reboot"},        // ↻ reload
        {"⏼", "Shutdown"},      // ⏼ power-off
    };
    for (int i = 0; i < 4; ++i) {
        auto& b = powerButtons_[i];
        b.icon = cfg[i].icon;
        b.label = cfg[i].label;
        b.diameter = kButtonDiameter;
    }
    powerButtons_[0].onClick = [this] {
        showPowerConfirm(0, lastW_, lastH_);
    };
    powerButtons_[1].onClick = [this] {
        showPowerConfirm(1, lastW_, lastH_);
    };
    powerButtons_[2].onClick = [this] {
        showPowerConfirm(2, lastW_, lastH_);
    };
    powerButtons_[3].onClick = [this] {
        showPowerConfirm(3, lastW_, lastH_);
    };

    alwaysPower_.icon = "⏻";  // ⏻
    alwaysPower_.label = "Power";
    alwaysPower_.diameter = kButtonDiameter;
    // Anchor toggles the pill open/closed; also reveals the UI if not yet shown.
    alwaysPower_.onClick = [this] {
        reveal();
        if (powerExpanded_)
            collapsePower();
        else
            expandPower();
    };

    // Repaint once a second so the clock stays current; also re-poll MPRIS.
    // (Notifications are pushed via setNotifications, not polled.)
    clockTimer_ = loop_.addTimer(1000, true, [this] {
        if (audio_) audio_->refresh();
        host_.invalidate();
    });
}

// -----------------------------------------------------------------------------
// Reveal state machine
// -----------------------------------------------------------------------------
void LockScreen::wake() {
    if (!revealed_) {
        revealed_ = true;
        revealAnim_.animateTo(1.0, theme::anim::reveal, ease::inOutQuad);
    }
    restartHideTimer();
    host_.invalidate();
}

void LockScreen::reveal() {
    wake();
}

void LockScreen::collapse() {
    revealed_ = false;
    revealAnim_.animateTo(0.0, theme::anim::reveal, ease::inOutQuad);
    collapsePower();  // closing the UI always collapses the pill too
    if (hideTimer_ >= 0) {
        loop_.removeTimer(hideTimer_);
        hideTimer_ = -1;
    }
    host_.invalidate();
}

void LockScreen::expandPower() {
    if (powerExpanded_) return;
    powerExpanded_ = true;
    powerExpandAnim_.animateTo(1.0, theme::anim::medium, ease::inOutQuad);
    host_.invalidate();
}

void LockScreen::collapsePower() {
    if (!powerExpanded_) return;
    powerExpanded_ = false;
    powerExpandAnim_.animateTo(0.0, theme::anim::medium, ease::inOutQuad);
    host_.invalidate();
}

void LockScreen::showPowerConfirm(int index, int w, int h) {
    struct Cfg {
        const char* icon;
        const char* label;
        const char* confirmLabel;
    };
    static const Cfg cfg[4] = {
        {"⏾", "Suspend", "Suspend"},
        {"󰒲", "Hibernate", "Hibernate"},
        {"↻", "Reboot", "Reboot"},
        {"⏼", "Shut down", "Shut down"},
    };

    std::function<void()> action;
    switch (index) {
        case 0:
            action = [this] {
                power_.suspend();
            };
            break;
        case 1:
            action = [this] {
                power_.hibernate();
            };
            break;
        case 2:
            action = [this] {
                power_.reboot();
            };
            break;
        case 3:
            action = [this] {
                power_.shutdown();
            };
            break;
        default:
            return;
    }

    // Anchor the popover to the left edge of the pill, vertically centred
    // on the anchor (trigger) button — which stays visible after the pill collapses.
    Rect anchor = powerAnchorRect(w, h);
    Rect fullCol = powerRowRect(w, h);
    double pillLeft = fullCol.x;

    powerDialog_.show(cfg[index].icon, cfg[index].label, cfg[index].confirmLabel, std::move(action),
                      anchor, pillLeft);
    collapsePower();
    host_.invalidate();
}

void LockScreen::restartHideTimer() {
    if (hideTimer_ >= 0) loop_.removeTimer(hideTimer_);
    hideTimer_ = loop_.addTimer(kHideTimeoutMs, false, [this] {
        hideTimer_ = -1;
        if (password_.empty()) collapse();
    });
}

// -----------------------------------------------------------------------------
// Authentication
// -----------------------------------------------------------------------------
void LockScreen::submitPassword() {
    if (password_.empty() || pam_.busy()) return;
    statusMessage_ = "Authenticating...";
    hasError_ = false;
    // The buffer is *moved* into the PAM worker (the destination wipes it when
    // the thread exits); `password_` comes back empty and still usable, so a
    // failed attempt can be retyped. No std::string copy of the secret is made
    // anywhere on this path (QL-3).
    pam_.authenticate(std::move(password_),
                      [this](PamAuthenticator::Result r, std::string m) { onAuthResult(r, m); });
    host_.invalidate();
}

void LockScreen::onAuthResult(PamAuthenticator::Result result, const std::string& message) {
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

// -----------------------------------------------------------------------------
// Keyboard (delegated from Shell)
// -----------------------------------------------------------------------------
void LockScreen::handleTextInput(const std::string& utf8) {
    if (powerDialog_.active()) return;  // ignore typing while confirm dialog is up
    password_.append(utf8);
    reveal();
}

void LockScreen::handleSpecialKey(uint32_t sym, uint32_t modifiers) {
    // If the confirmation dialog is up, only Escape and Enter are meaningful.
    if (powerDialog_.active()) {
        if (sym == XKB_KEY_Escape) {
            powerDialog_.dismiss();
            host_.invalidate();
        } else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
            powerDialog_.confirm();
            host_.invalidate();
        }
        return;  // block all other keystrokes while dialog is shown
    }

    switch (sym) {
        case XKB_KEY_Escape:
            password_.clear();
            collapse();
            return;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
            submitPassword();
            return;
        case XKB_KEY_BackSpace:
            password_.popBack();
            reveal();
            return;
        default:
            break;
    }
    if ((modifiers & MOD_CTRL) && (sym == XKB_KEY_u || sym == XKB_KEY_U)) {
        password_.clear();  // Ctrl+U clears the field
        reveal();
        return;
    }
    reveal();  // any other key still counts as activity
}

// -----------------------------------------------------------------------------
// Pointer (delegated from Shell)
// -----------------------------------------------------------------------------
void LockScreen::handlePointerMotion(int w, int h, double x, double y) {
    if (powerDialog_.active()) {
        powerDialog_.updateHover(x, y, nowMs());
        host_.invalidate();
        return;
    }
    reveal();
    if (pointerDown_ && audio_) audio_->handleDrag(x, y);
    if (notifications_.active()) notifications_.updateHover(x, y, nowMs());
    updateHover(w, h, x, y);
}

void LockScreen::handlePointerButton(int w, int h, double x, double y, uint32_t button,
                                     bool pressed) {
    if (button != kBtnLeft) return;

    if (!pressed) {  // release ends any volume drag
        pointerDown_ = false;
        if (audio_) audio_->handleRelease();
        return;
    }

    // While the confirmation dialog is showing, let it consume the click.
    if (powerDialog_.active()) {
        powerDialog_.handlePress(x, y, nowMs());
        host_.invalidate();
        return;
    }

    pointerDown_ = true;
    bool wasRevealed = revealed_;
    reveal();

    // Notification cards are a foreground overlay; dismiss on click.
    if (notifications_.active() && notifications_.handlePress(x, y, nowMs())) {
        host_.invalidate();
        return;
    }

    if (!wasRevealed) return;  // first interaction only reveals

    // Audio panel sits above the power row; give it first refusal.
    if (audio_ && audio_->handlePress(x, y, nowMs())) {
        host_.invalidate();
        return;
    }

    for (int i = 0; i < 4; ++i) {
        powerButtons_[i].bounds = powerButtonRect(i, w, h);
        if (powerExpanded_ && powerButtons_[i].contains(x, y)) {
            powerButtons_[i].click();
            return;
        }
    }
    // Also test the anchor button (it's always interactive).
    alwaysPower_.bounds = powerAnchorRect(w, h);
    if (alwaysPower_.contains(x, y)) {
        alwaysPower_.click();
        return;
    }
}

void LockScreen::handlePointerLeave() {
    int64_t now = nowMs();
    for (auto& b : powerButtons_) b.setHovered(false, now);
    alwaysPower_.setHovered(false, now);
    if (audio_) audio_->clearHover(now);
    notifications_.clearHover(now);
    host_.invalidate();
}

void LockScreen::updateHover(int w, int h, double x, double y) {
    int64_t now = nowMs();
    // Action buttons are only interactive when the pill is expanded.
    for (int i = 0; i < 4; ++i) {
        powerButtons_[i].bounds = powerButtonRect(i, w, h);
        powerButtons_[i].setHovered(powerExpanded_ && powerButtons_[i].contains(x, y), now);
    }
    // Anchor button is always interactive.
    alwaysPower_.bounds = powerAnchorRect(w, h);
    alwaysPower_.setHovered(alwaysPower_.contains(x, y), now);
    if (audio_ && revealed_ && audio_->active())
        audio_->updateHover(x, y, now);
    else if (audio_)
        audio_->clearHover(now);
    host_.invalidate();
}

// -----------------------------------------------------------------------------
// Layout geometry
// -----------------------------------------------------------------------------
//
// The power controls live in a pill anchored to the bottom-right corner.
// The anchor (⏻) is always visible. On reveal the pill grows upward,
// exposing the action buttons stacked above it.
//
//  ┌────┐                 ┌────┐
//  │[Sus]│                │    │  ← collapsed (idle): just the anchor
//  │[Hib]│  expanded  →   │ ⏻ │
//  │[Reb]│                └────┘
//  │[Sdn]│
//  │ ⏻  │  ← anchor always at the bottom
//  └────┘
//
// The bottom edge is fixed; the top edge slides upward as revealAnim_ → 1.
// Action button 0 = topmost, kNumAction-1 = directly above the anchor.
//
namespace {
constexpr int kNumAction = 4;
constexpr double kPillPad = 8.0;        // padding inside pill between edge and button centres
constexpr double kPillRadius = 9999.0;  // fully rounded pill (capsule)
}  // namespace

// Full expanded column rect (all kNumAction action buttons + anchor).
Rect LockScreen::powerRowRect(int w, int h) const {
    const double d = kButtonDiameter;
    const double sp = theme::spacing::medium;  // tighter vertical gap
    const double fullH = (kNumAction + 1) * d + kNumAction * sp + kPillPad * 2.0;
    const double pillW = d + kPillPad * 2.0;
    const double right = w - theme::spacing::xlarge;
    const double bottom = h - theme::spacing::xlarge;
    return {right - pillW, bottom - fullH, pillW, fullH};
}

// Rect for action button i inside the expanded pill.
// i=0 is topmost; i=kNumAction-1 is directly above the anchor.
Rect LockScreen::powerButtonRect(int index, int w, int h) const {
    const double d = kButtonDiameter;
    const double sp = theme::spacing::medium;
    Rect col = powerRowRect(w, h);
    double cx = col.cx();
    // Top of the first button: col.y + kPillPad + d/2
    double cy = col.y + kPillPad + d / 2.0 + index * (d + sp);
    return {cx - d / 2.0, cy - d / 2.0, d, d};
}

// The anchor button: always the bottommost slot in the pill.
Rect LockScreen::powerAnchorRect(int w, int h) const {
    const double d = kButtonDiameter;
    const double sp = theme::spacing::medium;
    Rect col = powerRowRect(w, h);
    double cx = col.cx();
    double cy = col.y + kPillPad + d / 2.0 + kNumAction * (d + sp);
    return {cx - d / 2.0, cy - d / 2.0, d, d};
}

// -----------------------------------------------------------------------------
// Render
// -----------------------------------------------------------------------------
void LockScreen::draw(cairo_t* cr, int width, int height, int) {
    lastW_ = width;
    lastH_ = height;
    Painter p(cr);
    const int64_t now = nowMs();
    const double r = clamp01(revealAnim_.value(now));
    const double cx = width / 2.0;

    // Background darken overlay — intensity varies with reveal state.
    p.fillRect({0, 0, static_cast<double>(width), static_cast<double>(height)},
               Color::rgba(0, 0, 0, lerp(0.15, 0.35, r)));

    // Clock — pushed up slightly to give the form more breathing room below.
    const double clockTop = height * 0.18;
    Size cs = clock_.measure(p);
    clock_.draw(p, cx, clockTop);
    const double clockBottom = clockTop + cs.h;

    // Centre column width — shared by the password field, status text, and audio
    // panel so all three elements align on the same left/right edges.
    const double colWidth =
        std::min(width - theme::spacing::xlarge * 2.0, static_cast<double>(theme::audio::maxWidth));

    // Password field — same width as the audio panel, centred.
    const double pwTop = clockBottom + theme::spacing::xlarge;  // tighter than xxlarge
    passwordField_.bounds = {cx - colWidth / 2.0, pwTop, colWidth, PasswordField::kHeight};
    passwordField_.charCount = static_cast<int>(utf8Count(password_.view()));

    const double statusTop =
        passwordField_.bounds.y + passwordField_.bounds.h + theme::spacing::small;
    status_.message = statusMessage_;
    status_.isError = hasError_;
    Size statusSize = status_.measure(p);

    // Position all power buttons at their expanded-state positions.
    for (int i = 0; i < 4; ++i) powerButtons_[i].bounds = powerButtonRect(i, width, height);
    alwaysPower_.bounds = powerAnchorRect(width, height);
    // Switch the anchor icon so it doesn't duplicate any action button:
    // shows ✕ (close) while expanded, ⏻ (power) while collapsed.
    alwaysPower_.icon = powerExpanded_ ? "✕" : "⏻";

    // Fade the revealed group in/out as one.
    auto withAlpha = [&](double a, auto&& fn) {
        if (a <= 0.01) return;
        if (a >= 0.999) {
            fn();
            return;
        }
        p.pushGroup();
        fn();
        p.popGroupWithAlpha(a);
    };

    withAlpha(r, [&] { passwordField_.draw(p, now); });
    withAlpha(r, [&] { status_.draw(p, cx, statusTop); });
    if (audio_ && audio_->active()) {
        double audioTop = statusTop + statusSize.h + theme::spacing::small;
        withAlpha(r, [&] { audio_->draw(p, now, cx, audioTop, colWidth); });
    }

    // --- Power pill (bottom-right, expands upward on reveal) ---
    {
        const double pe = clamp01(powerExpandAnim_.value(now));
        const Rect fullCol = powerRowRect(width, height);
        const Rect anchorR = powerAnchorRect(width, height);

        // Collapsed pill: just tall enough for the anchor button + padding.
        // Expanded pill: full column height covering all 4 action buttons + anchor.
        const double collapsedH = anchorR.h + kPillPad * 2.0;
        const double expandedH = fullCol.h;
        const double pillH = lerp(collapsedH, expandedH, pe);
        // Bottom edge is fixed; top edge rises as the pill grows.
        const double pillBottom = fullCol.y + fullCol.h;
        const double pillX = fullCol.x;
        const double pillW = fullCol.w;
        const Rect pillRect{pillX, pillBottom - pillH, pillW, pillH};
        const double pillCorner = pillW / 2.0;  // fully rounded ends (capsule)

        // The pill is always visible at 0.7 opacity; revealed UI bumps it to 1.0.
        const double pillAlpha = lerp(0.7, 1.0, r);

        p.pushGroup();

        // Clip everything inside the animated pill shape so nothing bleeds out.
        cairo_t* cr_ctx = p.cr();
        cairo_save(cr_ctx);
        {
            cairo_new_path(cr_ctx);
            cairo_arc(cr_ctx, pillRect.x + pillCorner, pillRect.y + pillCorner, pillCorner, M_PI,
                      3.0 * M_PI / 2.0);
            cairo_arc(cr_ctx, pillRect.x + pillRect.w - pillCorner, pillRect.y + pillCorner,
                      pillCorner, 3.0 * M_PI / 2.0, 0.0);
            cairo_arc(cr_ctx, pillRect.x + pillRect.w - pillCorner,
                      pillRect.y + pillRect.h - pillCorner, pillCorner, 0.0, M_PI / 2.0);
            cairo_arc(cr_ctx, pillRect.x + pillCorner, pillRect.y + pillRect.h - pillCorner,
                      pillCorner, M_PI / 2.0, M_PI);
            cairo_close_path(cr_ctx);
            cairo_clip(cr_ctx);
        }

        // Pill background.
        p.fillRoundedRect(pillRect, pillCorner, theme::color::glass);
        p.strokeRoundedRect(pillRect, pillCorner, theme::color::glassBorder, 1.0);

        // Action buttons — fade in as the pill expands (gated on pe, not r).
        if (pe > 0.01) {
            p.pushGroup();
            for (auto& b : powerButtons_) b.draw(p, now);
            p.popGroupWithAlpha(clamp01(pe));
        }

        // Anchor (trigger) button — always rendered inside the pill.
        alwaysPower_.draw(p, now);

        cairo_restore(cr_ctx);  // remove clip
        p.popGroupWithAlpha(pillAlpha);
    }

    // Windows 11-style notification cards, always visible on the lock screen
    // (bottom-left), below the idle dim veil.
    {
        const double left = theme::spacing::xlarge;
        const double bottom = height - theme::spacing::xlarge;
        const double maxW = std::min(static_cast<double>(width) - 2 * theme::spacing::xlarge,
                                     static_cast<double>(theme::notification::cardWidth));
        notifications_.draw(p, now, left, bottom, maxW);
    }

    // Power confirmation dialog — drawn above all UI, below the idle dim.
    if (powerDialog_.active()) powerDialog_.draw(p, width, height, now);
}

bool LockScreen::isAnimating() const {
    const int64_t now = nowMs();
    if (revealAnim_.active(now)) return true;
    if (powerExpandAnim_.active(now)) return true;
    for (const auto& b : powerButtons_)
        if (b.animating(now)) return true;
    if (alwaysPower_.animating(now)) return true;
    if (powerDialog_.animating(now)) return true;
    if (notifications_.active() && notifications_.animating(now)) return true;
    return audio_ && audio_->animating(now);
}

}  // namespace qypr
