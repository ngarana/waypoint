// StatusMessage.hpp - Authentication status line.

#pragma once

#include <string>

#include "render/Painter.hpp"
#include "ui/Theme.hpp"

namespace qypr {

class StatusMessage : public theme::ThemeAware {
public:
    std::string message;
    bool isError = false;
    // Inline treatment: the tracked micro-label the auth card shows in its
    // footer, as opposed to the standalone status line under the clock.
    bool compact = false;

    Size measure(Painter& p) const;
    // Draw the line with its top at topY; x is the anchor per `align`.
    void draw(Painter& p, double x, double topY, HAlign align = HAlign::Center) const;
};

}  // namespace qypr
