// ActionButton.hpp - Circular glassmorphic icon button (port of ActionButton.qml
// and AudioPlayerButton.qml). Hover scales it via OutBack easing and shows a
// tooltip; disabled buttons dim and stop responding.

#pragma once

#include <functional>
#include <string>

#include "core/Types.hpp"
#include "ui/Theme.hpp"
#include "ui/Widget.hpp"

namespace qypr {

class Painter;

class ActionButton : public Widget {
public:
    std::string icon;
    std::string label;  // tooltip text
    std::string iconFamily = theme::font::iconFamily;
    double diameter = 52;
    double iconSize = 20;
    bool enabled = true;
    std::function<void()> onClick;

    bool interactive() const override { return enabled && visible; }
    bool contains(double px, double py) const override;

    // Update hover state; starts the scale animation when it changes.
    void setHovered(bool hovered, int64_t now);
    bool hovered() const { return hovered_; }

    void draw(Painter& p, int64_t now) override;
    bool animating(int64_t now) const { return scale_.active(now); }

    void click() {
        if (enabled && onClick) onClick();
    }

private:
    bool hovered_ = false;
    Animated scale_{1.0};
};

}  // namespace qypr
