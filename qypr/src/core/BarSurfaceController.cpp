// BarSurfaceController.cpp - Implementation of bar surface manager.
#include "core/BarSurfaceController.hpp"

#include <algorithm>

namespace qypr {

BarSurfaceController::BarSurfaceController(IBarSurfaceHost& host, EventLoop& loop, int idleHeight)
    : host_(host),
      loop_(loop),
      idleHeight_(idleHeight),
      overlayHeight_(idleHeight) {}

void BarSurfaceController::setIdleHeight(int height) {
    idleHeight_ = height;
    if (overlayHeight_ < idleHeight_) { overlayHeight_ = idleHeight_; }
}

bool BarSurfaceController::syncOverlay(int statusBarOverlayHeight) {
    const int want = std::max(idleHeight_, statusBarOverlayHeight);
    if (want == overlayHeight_) { return false; }
    overlayHeight_ = want;
    loop_.post([this, want] { host_.setOverlayHeight(want); });
    return true;
}

bool BarSurfaceController::syncKeyboard(bool wantsKeyboard) {
    if (wantsKeyboard == kbActive_) { return false; }
    kbActive_ = wantsKeyboard;
    host_.setKeyboardInteractive(wantsKeyboard);
    return true;
}

}  // namespace qypr
