#include "ui/LockRenderer.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <unistd.h>
#include <vector>

#include "core/Types.hpp"
#include "render/Painter.hpp"
#include "ui/AudioController.hpp"
#include "ui/LockController.hpp"
#include "ui/LockLayout.hpp"
#include "ui/PowerMenuController.hpp"

namespace qypr {

namespace {

// The node name is the only identity the card shows. Resolved once: it cannot
// change while the session is locked, and the lock screen redraws often.
std::string resolveNode() {
    std::array<char, 64> buffer{};
    if (::gethostname(buffer.data(), buffer.size() - 1) != 0) { return {}; }
    return buffer.data();
}

const std::string& nodeName() {
    static const std::string node = resolveNode();
    return node;
}

}  // namespace

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
    facts_.setTheme(state);
    auth_.setTheme(state);
    auth_.status.setTheme(state);
    notifications_.setTheme(state);
    power_.setTheme(state);
    if (audio_ != nullptr) { audio_->setTheme(state); }
}

void LockRenderer::draw(cairo_t* cr, int width, int height, int /*scale*/) {
    Painter p(cr);
    const int64_t now = nowMs();
    const LockSnapshot state = controller_.snapshot(now);

    // The chronometer grows with the output; the theme's size-clock is the
    // baseline the layout scales from.
    clock_.fontSize = layout_.clockFontSize(width, height);
    clock_.maxWidth = layout_.clockColumnWidth(width);

    const double columnWidth = layout_.authColumnWidth(width);
    const Size clockSize = clock_.measure(p);
    const Size factsSize = facts_.measure(p);
    const Size authSize = auth_.measure(p, columnWidth);
    const bool hasMedia = audio_ != nullptr && audio_->active();

    const LockLayout::Result geometry =
        layout_.compute(width, height, clockSize, factsSize, authSize, hasMedia);

    clock_.bounds = geometry.clock;
    facts_.bounds = geometry.facts;

    auth_.bounds = geometry.auth;
    auth_.node = nodeName();
    auth_.charCount = static_cast<int>(controller_.passwordLength());
    auth_.fingerprintActive = state.fingerprint == FingerprintState::Waiting ||
                              state.fingerprint == FingerprintState::Prompting;
    auth_.status.message = state.statusMessage;
    auth_.status.isError = state.hasError;
    auth_.status.compact = true;

    // The clock and its facts line are always visible; the auth card and
    // the media strip fade in with the reveal, so an idle screen shows no
    // input affordance at all.
    clock_.draw(p, now);
    facts_.draw(p, now);

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

    withAlpha(state.reveal, [&] { auth_.draw(p, now); });
    if (hasMedia) {
        withAlpha(state.reveal, [&] { audio_->draw(p, now, geometry.media); });
    }

    power_.layout(width, height);
    power_.draw(p, now, state.reveal);

    // The notification chips form the footer band at the bottom-left, under
    // the chronometer column.
    const double left = layout_.contentMargin();
    const double bottom = height - theme().spacing.xlarge;
    notifications_.draw(p, now, left, bottom, layout_.notificationZoneWidth(width));
}

bool LockRenderer::animating() const {
    const int64_t now = nowMs();
    if (power_.animating(now)) { return true; }
    if (notifications_.active() && notifications_.animating(now)) { return true; }
    return audio_ != nullptr && audio_->animating(now);
}

}  // namespace qypr
