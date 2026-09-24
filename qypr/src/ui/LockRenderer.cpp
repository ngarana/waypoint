#include "ui/LockRenderer.hpp"

#include <utility>

#include "core/Types.hpp"
#include "render/Painter.hpp"
#include "ui/AudioController.hpp"
#include "ui/LockController.hpp"
#include "ui/LockLayout.hpp"
#include "ui/PowerMenuController.hpp"

namespace qypr {

LockRenderer::LockRenderer(LockController& controller, LockLayout& layout,
                           PowerMenuController& power)
    : controller_(controller),
      layout_(layout),
      power_(power) {}

void LockRenderer::setNotificationDismissHandler(std::function<void(const Notification&)> handler) {
    notifications_.setOnDismiss(std::move(handler));
}

void LockRenderer::setTheme(const theme::State& state) {
    theme::ThemeAware::setTheme(state);
    layout_.setTheme(state);
    controller_.setTheme(state);
    clock_.setTheme(state);
    passwordField_.setTheme(state);
    status_.setTheme(state);
    notifications_.setTheme(state);
    power_.setTheme(state);
    if (audio_ != nullptr) { audio_->setTheme(state); }
}

void LockRenderer::draw(cairo_t* cr, int width, int height, int /*scale*/) {
    Painter p(cr);
    const int64_t now = nowMs();
    const LockSnapshot state = controller_.snapshot(now);

    const Size clockSize = clock_.measure(p);
    status_.message = state.statusMessage;
    status_.isError = state.hasError;
    const Size statusSize = status_.measure(p);
    const LockLayout::Result geometry = layout_.compute(width, height, clockSize, statusSize);

    // Shell owns the background and its single dim veil. This renderer starts
    // at the lock widgets so the surface is not darkened twice.
    clock_.draw(p, geometry.centerX, geometry.clockTop);

    passwordField_.bounds = geometry.password;
    passwordField_.charCount = static_cast<int>(controller_.passwordLength());

    auto withAlpha = [&](double alpha, auto&& drawFn) {
        if (alpha <= 0.01) { return; }
        if (alpha >= 0.999) {
            drawFn();
            return;
        }
        p.pushGroup();
        drawFn();
        p.popGroupWithAlpha(alpha);
    };

    withAlpha(state.reveal, [&] { passwordField_.draw(p, now); });
    withAlpha(state.reveal, [&] { status_.draw(p, geometry.centerX, geometry.statusTop); });
    if (audio_ != nullptr && audio_->active()) {
        withAlpha(state.reveal, [&] {
            audio_->draw(p, now, geometry.centerX, geometry.audioTop, geometry.columnWidth);
        });
    }

    power_.layout(width, height);
    power_.draw(p, now, state.reveal);

    const double left = theme().spacing.xlarge;
    const double bottom = height - theme().spacing.xlarge;
    const double maxWidth = std::min(static_cast<double>(width) - (2 * theme().spacing.xlarge),
                                     static_cast<double>(theme().notification.cardWidth));
    notifications_.draw(p, now, left, bottom, maxWidth);
}

bool LockRenderer::animating() const {
    const int64_t now = nowMs();
    if (power_.animating(now)) { return true; }
    if (notifications_.active() && notifications_.animating(now)) { return true; }
    return audio_ != nullptr && audio_->animating(now);
}

}  // namespace qypr
