#include "ui/StatusMessage.hpp"

#include "ui/Theme.hpp"

namespace qypr {

namespace {
TextStyle style(const theme::State& theme, bool isError, bool compact) {
    if (compact) {
        return {theme.font.monoFamily, 11.0, PANGO_WEIGHT_BOLD,
                isError ? theme.colors.error : theme.colors.textSubtle, 1.2};
    }
    return {theme.font.family, static_cast<double>(theme.font.size), PANGO_WEIGHT_MEDIUM,
            isError ? theme.colors.error : theme.colors.textSubtle};
}
}  // namespace

Size StatusMessage::measure(Painter& p) const {
    if (message.empty()) return {0, 0};
    return p.measureText(message, style(theme(), isError, compact));
}

void StatusMessage::draw(Painter& p, double x, double topY, HAlign align) const {
    if (message.empty()) return;
    const TextStyle s = style(theme(), isError, compact);
    if (compact) {
        // Inline in a glass card: no drop shadow, the surface already
        // separates the text from the background.
        p.drawText(x, topY, message, s, align);
        return;
    }
    p.drawTextShadowed(x, topY, message, s, align, theme().effects.shadowOpacity, 1);
}

}  // namespace qypr
