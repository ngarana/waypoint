#include "ui/PasswordField.hpp"

#include <string>

#include "render/Painter.hpp"
#include "ui/Theme.hpp"

namespace qypr {

namespace {
constexpr const char* kLockGlyph = "";  // Nerd Font padlock
constexpr const char* kPlaceholder = "Enter password...";
}  // namespace

void PasswordField::draw(Painter& p, int64_t) {
    const Rect& r = bounds;
    const double radius = r.h / 2.0;

    Color border = focused ? theme::color::primary : theme::color::glassBorder;

    // Focus glow ring behind the pill.
    if (focused) {
        Rect glow{r.x - 3, r.y - 3, r.w + 6, r.h + 6};
        p.strokeRoundedRect(glow, radius + 3, theme::color::primaryGlow, 2);
    }

    p.fillRoundedRect(r, radius, theme::color::glass);
    p.strokeRoundedRect(r, radius, border, 1);

    // Lock glyph, left-aligned and vertically centred.
    TextStyle icon{theme::font::iconFamily, theme::font::size, PANGO_WEIGHT_NORMAL,
                   theme::color::text.withAlpha(0.7)};
    double iconX = r.x + theme::spacing::large;
    Size iconSz = p.measureText(kLockGlyph, icon);
    p.drawText(iconX, r.cy() - iconSz.h / 2.0, kLockGlyph, icon, HAlign::Left);

    // Masked input or placeholder.
    double textX = iconX + iconSz.w + theme::spacing::medium;
    if (charCount > 0) {
        std::string dots;
        for (int i = 0; i < charCount; ++i) dots += "•";  // bullet
        TextStyle t{theme::font::family, theme::font::size, PANGO_WEIGHT_NORMAL,
                    theme::color::text};
        Size ts = p.measureText(dots, t);
        p.drawText(textX, r.cy() - ts.h / 2.0, dots, t, HAlign::Left);
    } else {
        TextStyle ph{theme::font::family, theme::font::size, PANGO_WEIGHT_NORMAL,
                     theme::color::textMuted};
        Size ps = p.measureText(kPlaceholder, ph);
        p.drawText(textX, r.cy() - ps.h / 2.0, kPlaceholder, ph, HAlign::Left);
    }
}

}  // namespace qypr
