// LockRenderer.hpp - Cairo rendering for the lock surface.

#pragma once

#include <functional>

#include "ui/Clock.hpp"
#include "ui/Notification.hpp"
#include "ui/PasswordField.hpp"
#include "ui/StatusMessage.hpp"
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
    void draw(cairo_t* cr, int width, int height, int scale);
    bool animating() const;

private:
    LockController& controller_;
    LockLayout& layout_;
    PowerMenuController& power_;
    AudioController* audio_ = nullptr;

    Clock clock_;
    PasswordField passwordField_;
    StatusMessage status_;
    NotificationView notifications_;
};

}  // namespace qypr
