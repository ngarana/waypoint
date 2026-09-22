// ShellInputRouter.hpp - Input event routing between StatusBar and LockScreen.
#pragma once

#include <cstdint>
#include <string>

namespace qypr {

class StatusBar;
class LockScreen;

class ShellInputRouter {
public:
    static void routeTextInput(LockScreen& lock, const std::string& utf8);

    // Returns true if event was handled/consumed by the status bar.
    static bool routeSpecialKey(StatusBar& bar, LockScreen& lock, uint32_t keysym,
                                uint32_t modifiers);

    static void routePointerMotion(StatusBar& bar, LockScreen& lock, int w, int h, double x,
                                   double y, int64_t nowMs);

    // Routes pointer button. If lockscreen modal is active, lock takes priority.
    // Otherwise status bar gets first claim.
    static bool routePointerButton(StatusBar& bar, LockScreen& lock, int w, int h, double x,
                                   double y, uint32_t button, bool pressed, int64_t nowMs);

    static void routePointerScroll(StatusBar& bar, double x, double y, double dx, double dy);

    static void routePointerLeave(StatusBar& bar, LockScreen& lock, int64_t nowMs);
};

}  // namespace qypr
