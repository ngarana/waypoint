#include "ui/PasswordField.hpp"

#include <algorithm>
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

    Color border = focused ? theme().colors.primary : theme().colors.glassBorder;

    // Focus glow ring behind the pill.
    if (focused) {
        Rect glow{r.x - 3, r.y - 3, r.w + 6, r.h + 6};
        p.strokeRoundedRect(glow, radius + 3, theme().colors.primaryGlow, 2);
    }

    p.fillRoundedRect(r, radius, theme().colors.glass);
    p.strokeRoundedRect(r, radius, border, 1);

    // Lock glyph, left-aligned and vertically centred.
    TextStyle icon{theme().font.iconFamily, static_cast<double>(theme().font.size),
                   PANGO_WEIGHT_NORMAL, theme().colors.text.withAlpha(0.7)};
    double iconX = r.x + theme().spacing.large;
    Size iconSz = p.measureText(kLockGlyph, icon);
    p.drawText(iconX, r.cy() - iconSz.h / 2.0, kLockGlyph, icon, HAlign::Left);

    // Masked input or placeholder.
    double textX = iconX + iconSz.w + theme().spacing.medium;
    const double textWidth = std::max(0.0, r.x + r.w - textX - theme().spacing.large);
    if (charCount > 0) {
        std::string dots;
        dots.reserve(static_cast<size_t>(charCount) * 3);
        for (int i = 0; i < charCount; ++i) dots += "•";  // bullet
        TextStyle t{theme().font.family, static_cast<double>(theme().font.size),
                    PANGO_WEIGHT_NORMAL, theme().colors.text};
        Size ts = p.measureText(dots, t, textWidth);
        p.drawText(textX, r.cy() - ts.h / 2.0, dots, t, HAlign::Left, textWidth);
    } else {
        TextStyle ph{theme().font.family, static_cast<double>(theme().font.size),
                     PANGO_WEIGHT_NORMAL, theme().colors.textMuted};
        Size ps = p.measureText(kPlaceholder, ph, textWidth);
        p.drawText(textX, r.cy() - ps.h / 2.0, kPlaceholder, ph, HAlign::Left, textWidth);
    }
}

}  // namespace qypr
