#include "ui/StatusMessage.hpp"

#include "ui/Theme.hpp"

namespace qypr {

namespace {
TextStyle style(bool isError) {
    return {theme::font::family, theme::font::size, PANGO_WEIGHT_MEDIUM,
            isError ? theme::color::error : theme::color::textSubtle};
}
}  // namespace

Size StatusMessage::measure(Painter& p) const {
    if (message.empty()) return {0, 0};
    return p.measureText(message, style(isError));
}

void StatusMessage::draw(Painter& p, double centerX, double topY) const {
    if (message.empty()) return;
    p.drawTextShadowed(centerX, topY, message, style(isError), HAlign::Center,
                       theme::effects::shadowOpacity, 1);
}

}  // namespace qypr
