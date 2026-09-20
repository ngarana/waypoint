// Widget.hpp - Minimal base for a laid-out, drawable UI element.
//
// LockScreen positions each widget by setting `bounds` during layout, then
// calls draw(). Interactive widgets add hit-testing on top of `bounds`.

#pragma once

#include "core/Types.hpp"

namespace qypr {

class Painter;

class Widget {
public:
    virtual ~Widget() = default;

    // Paint using the current bounds. `now` drives animations.
    virtual void draw(Painter& p, int64_t now) = 0;

    // True if the widget currently wants to be considered for hit-testing.
    virtual bool interactive() const { return false; }
    virtual bool contains(double px, double py) const { return bounds.contains(px, py); }

    Rect bounds;
    bool visible = true;
};

}  // namespace qypr
