#include "ui/LockFactsRow.hpp"

#include <algorithm>

#include "render/Painter.hpp"
#include "ui/Theme.hpp"

namespace qypr {

namespace {

constexpr double kDotRadius = 3.0;
constexpr double kDotGap = 8.0;
constexpr double kItemGap = 14.0;

TextStyle labelStyle(const theme::State& theme) {
    return {theme.font.family, 11.0, PANGO_WEIGHT_MEDIUM, theme.colors.textMuted, 1.4};
}

TextStyle valueStyle(const theme::State& theme) {
    return {theme.font.family, 11.0, PANGO_WEIGHT_BOLD, theme.colors.text, 1.4};
}

TextStyle separatorStyle(const theme::State& theme) {
    return {theme.font.family, 11.0, PANGO_WEIGHT_NORMAL, theme.colors.textMuted.withAlpha(0.45),
            1.0};
}

}  // namespace

Size LockFactsRow::measure(Painter& p) const {
    if (items.empty()) { return {0, 0}; }
    double width = 0;
    double height = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const Item& item = items.at(i);
        const Size label = p.measureText(item.label, labelStyle(theme()));
        const Size value = p.measureText(item.value, valueStyle(theme()));
        width += kDotRadius * 2 + kDotGap + label.w + 6 + value.w;
        height = std::max({height, label.h, value.h});
        if (i + 1 < items.size()) {
            width += kItemGap + p.measureText("/", separatorStyle(theme())).w;
        }
    }
    return {width, height};
}

void LockFactsRow::draw(Painter& p, int64_t /*now*/) {
    if (items.empty()) { return; }
    const theme::State& t = theme();
    const double shadowAlpha = t.effects.shadowOpacity;
    double x = bounds.x;
    const double cy = bounds.y + (bounds.h / 2.0);

    for (size_t i = 0; i < items.size(); ++i) {
        const Item& item = items.at(i);
        const Color dot = item.dot.a > 0 ? item.dot : t.colors.primary;

        p.fillCircle(x + kDotRadius, cy, kDotRadius, dot);
        x += (kDotRadius * 2) + kDotGap;

        const Size label = p.measureText(item.label, labelStyle(t));
        p.drawTextShadowed(x, cy - (label.h / 2.0), item.label, labelStyle(t), HAlign::Left,
                           shadowAlpha, 1);
        x += label.w + 6;

        const Size value = p.measureText(item.value, valueStyle(t));
        p.drawTextShadowed(x, cy - (value.h / 2.0), item.value, valueStyle(t), HAlign::Left,
                           shadowAlpha, 1);
        x += value.w;

        if (i + 1 < items.size()) {
            const Size sep = p.measureText("/", separatorStyle(t));
            x += kItemGap;
            p.drawTextShadowed(x, cy - (sep.h / 2.0), "/", separatorStyle(t), HAlign::Left,
                               shadowAlpha, 1);
            x += sep.w;
        }
    }
}

}  // namespace qypr
