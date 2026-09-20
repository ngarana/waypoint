// KeyboardLayoutIndicator.cpp - Active keyboard-layout display implementation.
#include "ui/indicators/KeyboardLayoutIndicator.hpp"

#include "system/KeyboardLayout.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

namespace {
constexpr const char* kGlyph = "󰌌";  // nf-md-keyboard
}  // namespace

KeyboardLayoutIndicator::KeyboardLayoutIndicator(const SystemBackends& backends)
    : StatusIndicator("keyboard-layout", Zone::Right, 175),
      backend_(backends.keyboardLayout) {
    // Hidden until the Seat reports a layout and more than one is configured;
    // never present on the lock screen (backend is null).
    visible = false;
}

std::string KeyboardLayoutIndicator::icon() const {
    return kGlyph;
}

std::string KeyboardLayoutIndicator::themedIcon() const {
    // input-keyboard symbolic; the active layout label is rendered alongside so
    // the user still gets the actual locale code.
    return "input-keyboard-symbolic";
}

std::string KeyboardLayoutIndicator::label() const {
    return backend_ ? backend_->label() : "";
}

std::string KeyboardLayoutIndicator::tooltip() const {
    if (!backend_ || !backend_->available()) return "Keyboard layout";
    return "Keyboard layout: " + backend_->name();
}

void KeyboardLayoutIndicator::onBackendUpdate() {
    // Only meaningful with a choice of layouts — a single layout is inert.
    visible = backend_ && backend_->available() && backend_->count() > 1;
}

REGISTER_INDICATOR("keyboard-layout", Zone::Right, 175, KeyboardLayoutIndicator)

}  // namespace qypr
