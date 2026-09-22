#include "ui/LockInput.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>

#include "core/Interfaces.hpp"
#include "ui/AudioController.hpp"
#include "ui/LockController.hpp"
#include "ui/Notification.hpp"
#include "ui/PowerMenuController.hpp"
#include "wayland/Seat.hpp"  // Mod flags

namespace qypr {

namespace {
constexpr uint32_t kBtnLeft = 0x110;
}

LockInput::LockInput(LockController& controller, PowerMenuController& power,
                     NotificationView& notifications, Invalidator& host)
    : controller_(controller),
      power_(power),
      notifications_(notifications),
      host_(host) {}

void LockInput::handleTextInput(const std::string& utf8) {
    if (power_.modalActive()) { return; }
    controller_.appendText(utf8);
}

void LockInput::handleSpecialKey(uint32_t sym, uint32_t modifiers) {
    if (power_.modalActive()) {
        if (sym == XKB_KEY_Escape) {
            power_.dismissModal();
        } else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
            power_.confirmModal();
        }
        host_.invalidate();
        return;
    }

    switch (sym) {
        case XKB_KEY_Escape:
            controller_.clearPassword();
            controller_.collapse();
            return;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
            controller_.submitPassword();
            return;
        case XKB_KEY_BackSpace:
            controller_.backspace();
            return;
        default:
            break;
    }
    if ((modifiers & MOD_CTRL) != 0U && (sym == XKB_KEY_u || sym == XKB_KEY_U)) {
        controller_.clearPassword();
        controller_.wake();
        return;
    }
    controller_.wake();
}

void LockInput::handlePointerMotion(int width, int height, double x, double y) {
    power_.layout(width, height);
    if (power_.modalActive()) {
        power_.handleModalMotion(x, y, nowMs());
        return;
    }

    controller_.wake();
    if (pointerDown_ && audio_ != nullptr) { audio_->handleDrag(x, y); }
    if (notifications_.active()) { notifications_.updateHover(x, y, nowMs()); }
    power_.handleMotion(x, y, nowMs());
    if (audio_ != nullptr && controller_.revealed() && audio_->active()) {
        audio_->updateHover(x, y, nowMs());
    } else if (audio_ != nullptr) {
        audio_->clearHover(nowMs());
    }
    host_.invalidate();
}

void LockInput::handlePointerButton(int width, int height, double x, double y, uint32_t button,
                                    bool pressed) {
    if (button != kBtnLeft) { return; }
    power_.layout(width, height);

    if (!pressed) {
        pointerDown_ = false;
        if (audio_ != nullptr) { audio_->handleRelease(); }
        return;
    }

    if (power_.modalActive()) {
        power_.handleModalPress(x, y, nowMs());
        return;
    }

    pointerDown_ = true;
    const bool wasRevealed = controller_.revealed();
    controller_.wake();

    // Notification cards are a foreground overlay; a card consumes its own
    // press before any underlying audio or power control sees it.
    if (notifications_.active() && notifications_.handlePress(x, y, nowMs())) {
        host_.invalidate();
        return;
    }
    if (!wasRevealed) { return; }

    if (audio_ != nullptr && audio_->handlePress(x, y, nowMs())) {
        host_.invalidate();
        return;
    }
    if (power_.handlePress(x, y, nowMs())) { return; }
}

void LockInput::handlePointerLeave() {
    const int64_t now = nowMs();
    power_.handleLeave(now);
    if (audio_ != nullptr) { audio_->clearHover(now); }
    notifications_.clearHover(now);
    host_.invalidate();
}

}  // namespace qypr
