// PasswordField.hpp - Glassmorphic password pill (port of PasswordField.qml).
//
// Renders the frosted pill, focus glow, lock glyph, and masked input. It holds
// no text itself: LockScreen owns the secret and passes only the char count.

#pragma once

#include "ui/Widget.hpp"

namespace qypr {

class Painter;

class PasswordField : public Widget {
public:
    static constexpr double kHeight = 56;

    int charCount = 0;
    bool focused = true;

    void draw(Painter& p, int64_t now) override;
};

}  // namespace qypr
