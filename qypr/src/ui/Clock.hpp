// Clock.hpp - Large time + date display with drop shadows.
//
// Port of Clock.qml: HH:mm in an ultra-thin face over "dddd, MMMM d", each
// shadowed for readability over the background.

#pragma once

#include "render/Painter.hpp"

namespace qypr {

class Clock {
public:
    // Total block size (used to place widgets below the clock).
    Size measure(Painter& p) const;

    // Draw the block with its top edge at topY, horizontally centred on centerX.
    void draw(Painter& p, double centerX, double topY) const;

private:
    std::string timeString() const;
    std::string dateString() const;
};

}  // namespace qypr
