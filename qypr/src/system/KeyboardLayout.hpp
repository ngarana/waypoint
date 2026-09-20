// KeyboardLayout.hpp - Active xkb keyboard-layout state for the layout indicator.
//
// A tiny push-only state holder. The Wayland Seat decodes the compositor's
// active layout group (from wl_keyboard.modifiers + the compiled keymap) and
// calls update(); KeyboardLayoutIndicator reads it back on the next repaint.
//
// Bar-only by convention: the lock screen leaves SystemBackends::keyboardLayout
// null, so the indicator self-hides there like every other Phase 15 utility.
//
// Display-only: a Wayland client cannot switch the compositor's layout (that is
// a compositor keybind), so there is no setter that changes hardware state —
// update() only mirrors what the Seat observed. No poll, no daemon.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace qypr {

class KeyboardLayout {
public:
    KeyboardLayout() = default;

    // Called (via BarApp::onLayoutChanged) when the Seat reports the active
    // layout group. Coalesces redundant reports so we don't repaint on every
    // modifier event.
    void update(const std::string& name, uint32_t index, uint32_t count);

    // False until the first report — the indicator stays hidden meanwhile.
    bool available() const { return available_; }
    // Number of configured layouts; the indicator hides itself when < 2 (a
    // single-layout keyboard has nothing worth showing).
    uint32_t count() const { return count_; }
    uint32_t index() const { return index_; }
    // Full xkb description, e.g. "English (US)" — the tooltip text.
    const std::string& name() const { return name_; }
    // Compact bar label, e.g. "US" — derived from name() (see shortLabel()).
    std::string label() const { return shortLabel(name_); }

    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

    // Pure derivation of a 2–3 char bar label from an xkb layout description.
    // Static + dependency-free so it is unit-testable without a real keymap.
    static std::string shortLabel(const std::string& xkbName);

private:
    std::string name_;
    uint32_t index_ = 0;
    uint32_t count_ = 0;
    bool available_ = false;
    std::function<void()> onChange_;
};

}  // namespace qypr
