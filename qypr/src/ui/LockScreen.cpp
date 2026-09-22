#include "ui/LockScreen.hpp"

#include <utility>

#include "core/Interfaces.hpp"
#include "ui/AudioController.hpp"

namespace qypr {

LockScreen::LockScreen(EventLoop& loop, RenderHost& host, PamAuthenticator& pam,
                       SystemActions& power)
    : host_(host),
      controller_(loop, host, pam),
      power_(host, power, layout_),
      renderer_(controller_, layout_, power_),
      input_(controller_, power_, renderer_.notifications(), host) {
    controller_.setOnTick([this] {
        if (audio_ != nullptr) { audio_->refresh(); }
    });
    controller_.setOnCollapse([this] { power_.collapse(); });
}

void LockScreen::setAudioController(AudioController* audio) {
    audio_ = audio;
    renderer_.setAudioController(audio);
    input_.setAudioController(audio);
}

void LockScreen::setNotifications(std::vector<Notification> notes) {
    renderer_.notifications().update(std::move(notes));
}

void LockScreen::draw(cairo_t* cr, int width, int height, int scale) {
    renderer_.draw(cr, width, height, scale);
}

bool LockScreen::isAnimating() const {
    const int64_t now = nowMs();
    if (controller_.animating(now)) { return true; }
    return renderer_.animating();
}

void LockScreen::setTheme(const theme::State& state) {
    theme::ThemeAware::setTheme(state);
    controller_.setTheme(state);
    renderer_.setTheme(state);
}

void LockScreen::handleTextInput(const std::string& utf8) {
    input_.handleTextInput(utf8);
}

void LockScreen::handleSpecialKey(uint32_t keysym, uint32_t modifiers) {
    input_.handleSpecialKey(keysym, modifiers);
}

void LockScreen::handlePointerMotion(int width, int height, double x, double y) {
    input_.handlePointerMotion(width, height, x, y);
}

void LockScreen::handlePointerButton(int width, int height, double x, double y, uint32_t button,
                                     bool pressed) {
    input_.handlePointerButton(width, height, x, y, button, pressed);
}

void LockScreen::handlePointerLeave() {
    input_.handlePointerLeave();
}

}  // namespace qypr
