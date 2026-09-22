// BarSurfaceController.hpp - Manages overlay height and keyboard interactivity for bar surfaces.
#pragma once

#include "core/EventLoop.hpp"
#include "core/Interfaces.hpp"

namespace qypr {

class BarSurfaceController {
public:
    BarSurfaceController(IBarSurfaceHost& host, EventLoop& loop, int idleHeight);

    void setIdleHeight(int height);
    int idleHeight() const { return idleHeight_; }

    // Synchronize surface height with statusBar.overlayHeight().
    // Returns true if a change was dispatched.
    bool syncOverlay(int statusBarOverlayHeight);

    // Synchronize layer-shell keyboard grab with statusBar.wantsKeyboard().
    // Returns true if a change was dispatched.
    bool syncKeyboard(bool wantsKeyboard);

    int currentOverlayHeight() const { return overlayHeight_; }
    bool isKeyboardActive() const { return kbActive_; }

private:
    IBarSurfaceHost& host_;
    EventLoop& loop_;
    int idleHeight_ = 0;
    int overlayHeight_ = 0;
    bool kbActive_ = false;
};

}  // namespace qypr
