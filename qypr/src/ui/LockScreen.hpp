// LockScreen.hpp - Public composition host for the lock UI.
//
// Authentication/state, input routing, layout, rendering, and power policy
// live in dedicated seams. This class preserves the host-facing API consumed
// by Shell and App while composing those pieces.

#pragma once

#include <string>
#include <vector>

#include "core/SecureBuffer.hpp"
#include "ui/LockController.hpp"
#include "ui/LockInput.hpp"
#include "ui/LockLayout.hpp"
#include "ui/LockRenderer.hpp"
#include "ui/PowerMenuController.hpp"

namespace qypr {

class AudioController;
class EventLoop;
class PamAuthenticator;
class RenderHost;
class SystemActions;

class LockScreen : public theme::ThemeAware {
public:
    LockScreen(EventLoop& loop, RenderHost& host, PamAuthenticator& pam, SystemActions& power);

    // Optional audio panel, injected once MPRIS is available.
    void setAudioController(AudioController* audio);

    // Windows 11-style notification cards (bottom-left).
    void setNotifications(std::vector<Notification> notes);

    void draw(cairo_t* cr, int width, int height, int scale);
    bool isAnimating() const;

    void setTheme(const theme::State& state) override;

    // Input handlers delegated by Shell.
    void handleTextInput(const std::string& utf8);
    void handleSpecialKey(uint32_t keysym, uint32_t modifiers);
    void handlePointerMotion(int w, int h, double x, double y);
    void handlePointerButton(int w, int h, double x, double y, uint32_t button, bool pressed);
    void handlePointerLeave();

    double getReveal(int64_t now) const { return controller_.reveal(now); }
    bool modalActive() const { return power_.modalActive(); }
    void wake() { controller_.wake(); }

#ifdef TESTING
    // Test seams retain the old lock-screen test vocabulary while the state
    // itself remains owned by LockController.
    SecureBuffer& password() { return controller_.password(); }
    void submitPassword() { controller_.submitPassword(); }
    bool hasError() const { return controller_.hasError(); }
    bool unlocking() const { return controller_.unlocking(); }
#endif

private:
    RenderHost& host_;
    AudioController* audio_ = nullptr;

    LockController controller_;
    LockLayout layout_;
    PowerMenuController power_;
    LockRenderer renderer_;
    LockInput input_;
};

}  // namespace qypr
