// LockRenderer.hpp - Cairo rendering for the lock surface.

#pragma once

#include <functional>

#include "ui/AuthPanel.hpp"
#include "ui/Clock.hpp"
#include "ui/LockController.hpp"
#include "ui/LockFactsRow.hpp"
#include "ui/Notification.hpp"
#include "ui/Theme.hpp"

namespace qypr {

class AudioController;
class LockController;
class LockLayout;
class Painter;
class PowerMenuController;

class LockRenderer : public theme::ThemeAware {
public:
    LockRenderer(LockController& controller, LockLayout& layout, PowerMenuController& power);

    void setTheme(const theme::State& state) override;
    void setAudioController(AudioController* audio) { audio_ = audio; }
    void setNotificationDismissHandler(std::function<void(const Notification&)> handler);

    NotificationView& notifications() { return notifications_; }
    // The auth card, so the input layer can hit-test its unlock affordance.
    AuthPanel& auth() { return auth_; }
    void draw(cairo_t* cr, int width, int height, int scale);
    bool animating() const;

private:
    // The facts line under the clock: what the lock screen actually knows
    // about the session it guards.

    LockController& controller_;
    LockLayout& layout_;
    PowerMenuController& power_;
    AudioController* audio_ = nullptr;

    Clock clock_;
    LockFactsRow facts_;
    AuthPanel auth_;
    NotificationView notifications_;
};

}  // namespace qypr
