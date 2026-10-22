// LockInput.hpp - Input mapping for the lock screen.

#pragma once

#include <cstdint>
#include <string>

namespace qypr {

class AudioController;
class AuthPanel;
class Invalidator;
class LockController;
class NotificationView;
class PowerMenuController;

// Maps platform events to lock commands and delegates hit-testing to the
// corresponding policy/view seam. It contains no authentication or power
// implementation of its own.
class LockInput {
public:
    LockInput(LockController& controller, PowerMenuController& power,
              NotificationView& notifications, AuthPanel& auth, Invalidator& host);

    void setAudioController(AudioController* audio) { audio_ = audio; }

    void handleTextInput(const std::string& utf8);
    void handleSpecialKey(uint32_t keysym, uint32_t modifiers);
    void handlePointerMotion(int width, int height, double x, double y);
    void handlePointerButton(int width, int height, double x, double y, uint32_t button,
                             bool pressed);
    void handlePointerLeave();

private:
    LockController& controller_;
    PowerMenuController& power_;
    NotificationView& notifications_;
    AuthPanel& auth_;
    Invalidator& host_;
    AudioController* audio_ = nullptr;
};

}  // namespace qypr
