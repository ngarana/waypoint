#include "ui/StatusMessage.hpp"

#include "ui/Theme.hpp"

namespace qypr {

namespace {
TextStyle style(const theme::State& theme, bool isError) {
    return {theme.font.family, static_cast<double>(theme.font.size), PANGO_WEIGHT_MEDIUM,
            isError ? theme.colors.error : theme.colors.textSubtle};
}
}  // namespace

Size StatusMessage::measure(Painter& p) const {
    if (message.empty()) return {0, 0};
    return p.measureText(message, style(theme(), isError));
}

void StatusMessage::draw(Painter& p, double centerX, double topY) const {
    if (message.empty()) return;
    p.drawTextShadowed(centerX, topY, message, style(theme(), isError), HAlign::Center,
                       theme().effects.shadowOpacity, 1);
}

}  // namespace qypr
