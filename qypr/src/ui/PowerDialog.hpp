// PowerDialog.hpp - Compact power-action confirmation popover.
//
// Inspired by GNOME's End Session Dialog and Windows 11's inline confirmations:
// no full-screen scrim, no centred modal. Instead a small glass card that
// appears anchored to the left edge of the power pill, at the height of the
// button that was clicked.
//
//   ┌─────────────────────────────┐  ← popover card, anchored left of pill
//   │  ⏼  Shut down?             │
//   │  ████████████░░░░  10s     │  ← countdown bar; expires → auto-cancel
//   │  [Cancel]      [Shut down] │
//   └─────────────────────────────┘
//
// The card fades in/out. Clicking outside or letting the countdown expire
// both dismiss without acting (safe default). Only an explicit Confirm click
// executes the power action.

#pragma once

#include <functional>
#include <string>

#include "core/Types.hpp"
#include "ui/Theme.hpp"

namespace qypr {

class Painter;

class PowerDialog {
public:
    bool active() const { return visible_ || fadeAnim_.target() > 0.001; }

    // Show the popover anchored to the left of the pill.
    // anchorRect: the bounding rect of the button that was clicked (for vertical
    //             alignment). pillLeft: left edge of the pill (card right-aligns here).
    void show(const std::string& icon, const std::string& title, const std::string& confirmLabel,
              std::function<void()> onConfirm, const Rect& anchorRect, double pillLeft);

    void dismiss();
    void confirm();

    bool handlePress(double x, double y, int64_t now);
    void updateHover(double x, double y, int64_t now);

    void draw(Painter& p, int w, int h, int64_t now);

    bool animating(int64_t now) const {
        return fadeAnim_.active(now) || confirmScale_.active(now) || cancelScale_.active(now);
    }

    // Auto-confirm countdown duration in ms. 0 = disabled.
    int autoConfirmMs = 8000;

    // Exposed so the .cpp helper can reference them without a full include cycle.
    static constexpr double kCardW = 260.0;
    static constexpr double kCardH = 104.0;

private:
    // Fixed card dimensions (kCardW/kCardH are public above).
    static constexpr double kBtnH = 32.0;
    static constexpr double kBarH = 3.0;
    static constexpr double kPad = 14.0;

    Rect cancelBtnRect(const Rect& card) const;
    Rect confirmBtnRect(const Rect& card) const;
    Rect barRect(const Rect& card) const;

    std::string icon_;
    std::string title_;
    std::string confirmLabel_;
    std::function<void()> onConfirm_;

    bool visible_ = false;
    Rect cardBounds_;       // cached for hit-testing
    double anchoredX_ = 0;  // card's right edge
    double anchoredY_ = 0;  // card's top edge

    Animated fadeAnim_{0};
    Animated confirmScale_{1.0};
    Animated cancelScale_{1.0};
    Animated barAnim_{1.0};  // 1 → 0 over autoConfirmMs (remaining time)

    bool confirmHovered_ = false;
    bool cancelHovered_ = false;
};

}  // namespace qypr
