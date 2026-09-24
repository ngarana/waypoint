// PopoverLayout.hpp - Small, font-independent sizing helpers for popovers.
#pragma once

#include <algorithm>
#include <string_view>

namespace qypr::popover_layout {

// Pango is the authoritative renderer, but contentHeight/contentWidth are
// queried before a Painter is available. Count UTF-8 code points and use a
// conservative advance estimate so a popover grows for real content while
// still relying on Pango ellipsizing at the maximum width.
inline size_t codePointCount(std::string_view text) {
    size_t count = 0;
    for (const unsigned char c : text) {
        if ((c & 0xc0U) != 0x80U) { ++count; }
    }
    return count;
}

inline double estimatedTextWidth(std::string_view text, double fontSize) {
    double width = 0.0;
    for (const unsigned char c : text) {
        if ((c & 0xc0U) != 0x80U) { width += (c == ' ' ? 0.34 : 0.58) * fontSize; }
    }
    return width;
}

inline double boundedWidth(double minimum, double maximum, double required) {
    return std::clamp(required, minimum, maximum);
}

}  // namespace qypr::popover_layout
