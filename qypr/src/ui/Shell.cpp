// Shell.cpp - Root UI compositor implementation
#include "ui/Shell.hpp"
#include "ui/ShellInputRouter.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>

#include "core/EventLoop.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"
#include "video/VideoPlayer.hpp"
#include "wayland/Seat.hpp"  // Mod flags

namespace qypr {

Shell::Shell(EventLoop& loop, RenderHost& host, PamAuthenticator& pam, SystemActions& power,
             const SystemBackends& backends)
    : loop_(loop),
      host_(host),
      lockScreen_(loop, host, pam, power),
      statusBar_(loop, host, backends),
      dnd_(backends.dnd) {
    if (dnd_) {
        dnd_->addListener([this] { applyNotificationFilter(); });
    }
    restartIdleTimer();
}

void Shell::setAudioController(AudioController* audio) {
    lockScreen_.setAudioController(audio);
}

void Shell::setTheme(const theme::State& state) {
    theme::ThemeAware::setTheme(state);
    lockScreen_.setTheme(state);
    statusBar_.setTheme(state);
}

void Shell::setVideoPlayer(VideoPlayer* video) {
    video_ = video;
}

void Shell::setNotifications(std::vector<Notification> notes) {
    pendingNotes_ = std::move(notes);
    applyNotificationFilter();
}

void Shell::applyNotificationFilter() {
    const bool suppress = dnd_ && dnd_->enabled();
    lockScreen_.setNotifications(suppress ? std::vector<Notification>{} : pendingNotes_);
    host_.invalidate();
}

void Shell::setIdleTimeout(int64_t ms) {
    idleTimeoutMs_ = ms;
    restartIdleTimer();
}

void Shell::draw(cairo_t* cr, int width, int height, int scale) {
    Painter p(cr);
    const int64_t now = nowMs();
    const double r = lockScreen_.getReveal(now);

    // 1. Video background (or fallback gradient if no video).
    if (video_ && video_->hasFrame()) {
        video_->draw(cr, width, height);
    } else {
        p.verticalGradient(width, height, Color::fromHex("#1e1e2e"), Color::fromHex("#181825"),
                           Color::fromHex("#11111b"));
    }

    // Background darken overlay
    p.fillRect({0, 0, static_cast<double>(width), static_cast<double>(height)},
               Color::rgba(0, 0, 0, lerp(0.15, 0.35, r)));

    // 2. Children (peers): lockscreen first, status bar on top. The bar
    // follows the lockscreen's reveal state so both read as one UI: dimmed
    // to 0.4 while idle, full opacity when revealed (STATUS_BAR.md "bar
    // idle dim"). Shell mediates — the bar itself knows nothing of reveal.
    lockScreen_.draw(cr, width, height, scale);
    statusBar_.layout(width, height);
    const double barAlpha = lerp(0.4, 1.0, r);
    if (barAlpha > 0.999) {
        statusBar_.draw(p, now);
    } else {
        p.pushGroup();
        statusBar_.draw(p, now);
        p.popGroupWithAlpha(barAlpha);
    }

    // 3. Deep-idle dim veil covers everything.
    const double dim = clamp01(dimAnim_.value(now));
    if (dim > 0.001) {
        p.fillRect({0, 0, static_cast<double>(width), static_cast<double>(height)},
                   Color::rgba(0, 0, 0, dim));
    }
}

bool Shell::isAnimating() const {
    const int64_t now = nowMs();
    if (dimAnim_.active(now)) return true;
    if (statusBar_.animating(now)) return true;
    return lockScreen_.isAnimating();
}

void Shell::onTextInput(const std::string& utf8) {
    wakeFromIdle();
    ShellInputRouter::routeTextInput(lockScreen_, utf8);
}

void Shell::onSpecialKey(uint32_t keysym, uint32_t modifiers) {
    wakeFromIdle();
    ShellInputRouter::routeSpecialKey(statusBar_, lockScreen_, keysym, modifiers);
}

void Shell::onPointerMotion(int w, int h, double x, double y) {
    wakeFromIdle();
    ShellInputRouter::routePointerMotion(statusBar_, lockScreen_, w, h, x, y, nowMs());
}

void Shell::onPointerButton(int w, int h, double x, double y, uint32_t button, bool pressed) {
    wakeFromIdle();
    ShellInputRouter::routePointerButton(statusBar_, lockScreen_, w, h, x, y, button, pressed,
                                         nowMs());
}

void Shell::onPointerScroll(int w, int h, double x, double y, double dx, double dy) {
    (void)w;
    (void)h;
    wakeFromIdle();
    ShellInputRouter::routePointerScroll(statusBar_, x, y, dx, dy);
}

void Shell::onPointerLeave() {
    ShellInputRouter::routePointerLeave(statusBar_, lockScreen_, nowMs());
}

void Shell::wakeFromIdle() {
    if (idle_) {
        idle_ = false;
        if (video_) video_->resume();
        dimAnim_.animateTo(0.0, 1500, ease::inOutQuad);
    }
    restartIdleTimer();
}

void Shell::restartIdleTimer() {
    if (idleTimer_ >= 0) loop_.removeTimer(idleTimer_);
    idleTimer_ = loop_.addTimer(idleTimeoutMs_, false, [this] {
        idleTimer_ = -1;
        enterIdle();
    });
}

void Shell::enterIdle() {
    if (idle_) return;
    idle_ = true;
    if (video_) video_->pause();
    dimAnim_.animateTo(1.0, 1500, ease::inOutQuad);
    host_.invalidate();
}

}  // namespace qypr
