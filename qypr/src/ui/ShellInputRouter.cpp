// ShellInputRouter.cpp - Implementation of UI event routing.
#include "ui/ShellInputRouter.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>

#include "ui/LockScreen.hpp"
#include "ui/statusbar/StatusBar.hpp"
#include "wayland/Seat.hpp"

namespace qypr {

void ShellInputRouter::routeTextInput(LockScreen& lock, const std::string& utf8) {
    lock.handleTextInput(utf8);
}

bool ShellInputRouter::routeSpecialKey(StatusBar& bar, LockScreen& lock, uint32_t keysym,
                                       uint32_t modifiers) {
    // Tab cycles keyboard focus through status bar indicators.
    if (keysym == XKB_KEY_Tab || keysym == XKB_KEY_ISO_Left_Tab) {
        const bool reverse = keysym == XKB_KEY_ISO_Left_Tab || ((modifiers & MOD_SHIFT) != 0U);
        if (!bar.cycleFocus(reverse)) { bar.clearFocus(); }
        return true;
    }

    // Escape/Enter/arrows for an open popover or a focused indicator.
    if (bar.handleKey(keysym)) { return true; }

    lock.handleSpecialKey(keysym, modifiers);
    return false;
}

void ShellInputRouter::routePointerMotion(StatusBar& bar, LockScreen& lock, int w, int h, double x,
                                          double y, int64_t nowMs) {
    // Both children track hover; neither consumes motion exclusively.
    bar.handlePointerMotion(x, y, nowMs);
    lock.handlePointerMotion(w, h, x, y);
}

bool ShellInputRouter::routePointerButton(StatusBar& bar, LockScreen& lock, int w, int h, double x,
                                          double y, uint32_t button, bool pressed, int64_t nowMs) {
    // Priority (STATUS_BAR.md event routing): a lockscreen modal (power
    // dialog) consumes everything; otherwise the status bar gets first claim.
    if (!lock.modalActive() && bar.handlePointerButton(x, y, button, pressed, nowMs)) {
        return true;
    }
    lock.handlePointerButton(w, h, x, y, button, pressed);
    return false;
}

void ShellInputRouter::routePointerScroll(StatusBar& bar, double x, double y, double dx,
                                          double dy) {
    // Only the status bar scrolls (volume/brightness adjust, popover lists).
    bar.handleScroll(x, y, dx, dy);
}

void ShellInputRouter::routePointerLeave(StatusBar& bar, LockScreen& lock, int64_t nowMs) {
    bar.handlePointerLeave(nowMs);
    lock.handlePointerLeave();
}

}  // namespace qypr
