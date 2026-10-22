#include "ui/LockFactsRow.hpp"

#include <algorithm>

#include "render/Painter.hpp"
#include "ui/Theme.hpp"

namespace qypr {

namespace {

TextStyle labelStyle(const theme::State& theme) {
    return {theme.font.family, static_cast<double>(theme.font.sizeSmall), PANGO_WEIGHT_MEDIUM,
            theme.colors.textMuted, theme.lock.facts.tracking};
}

TextStyle valueStyle(const theme::State& theme) {
    return {theme.font.family, static_cast<double>(theme.font.sizeSmall), PANGO_WEIGHT_BOLD,
            theme.colors.text, theme.lock.facts.tracking};
}

TextStyle separatorStyle(const theme::State& theme) {
    return {theme.font.family, static_cast<double>(theme.font.sizeSmall), PANGO_WEIGHT_NORMAL,
            theme.colors.textMuted.withAlpha(theme.lock.facts.separatorAlpha), 1.0};
}

}  // namespace

Size LockFactsRow::measure(Painter& p) const {
    if (items.empty()) { return {0, 0}; }
    const auto& f = theme().lock.facts;
    double width = 0;
    double height = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const Item& item = items.at(i);
        const Size label = p.measureText(item.label, labelStyle(theme()));
        const Size value = p.measureText(item.value, valueStyle(theme()));
        width += (f.dotRadius * 2) + f.dotGap + label.w + f.labelGap + value.w;
        height = std::max({height, label.h, value.h});
        if (i + 1 < items.size()) {
            width += (2 * f.itemGap) + p.measureText("/", separatorStyle(theme())).w;
        }
    }
    return {width, height};
}

void LockFactsRow::draw(Painter& p, int64_t /*now*/) {
    if (items.empty()) { return; }
    const theme::State& t = theme();
    const auto& f = t.lock.facts;
    const double shadowAlpha = t.effects.shadowOpacity;
    double x = bounds.x;
    const double cy = bounds.y + (bounds.h / 2.0);

    for (size_t i = 0; i < items.size(); ++i) {
        const Item& item = items.at(i);
        const Color dot = item.dot.a > 0 ? item.dot : t.colors.primary;

        p.fillCircle(x + f.dotRadius, cy, f.dotRadius, dot);
        x += (f.dotRadius * 2) + f.dotGap;

        const Size label = p.measureText(item.label, labelStyle(t));
        p.drawTextShadowed(x, cy - (label.h / 2.0), item.label, labelStyle(t), HAlign::Left,
                           shadowAlpha, 1);
        x += label.w + f.labelGap;

        const Size value = p.measureText(item.value, valueStyle(t));
        p.drawTextShadowed(x, cy - (value.h / 2.0), item.value, valueStyle(t), HAlign::Left,
                           shadowAlpha, 1);
        x += value.w;

        if (i + 1 < items.size()) {
            const Size sep = p.measureText("/", separatorStyle(t));
            x += f.itemGap;
            p.drawTextShadowed(x, cy - (sep.h / 2.0), "/", separatorStyle(t), HAlign::Left,
                               shadowAlpha, 1);
            x += sep.w + f.itemGap;
        }
    }
}

}  // namespace qypr
