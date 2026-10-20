#include "ui/Clock.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>

#include "render/Painter.hpp"
#include "ui/Theme.hpp"

namespace qypr {

namespace {

// The lockup's typographic scale, in points. The oversized time is the only
// size the layout drives; the rest follow the theme's spacing scale.
constexpr double kIndexSize = 12.0;
constexpr double kDateSize = 12.0;
constexpr double kStackSize = 12.0;
constexpr double kStackLabelSize = 10.0;
constexpr double kSecondsSize = 11.0;
constexpr double kSlashRatio = 0.42;  // slash size relative to the time size
constexpr double kRuleWidth = 560.0;  // the hairline never runs the full column

TextStyle indexStyle(const theme::State& theme) {
    return {theme.font.monoFamily, kIndexSize, PANGO_WEIGHT_BOLD, theme.colors.primary, 1.6};
}

TextStyle dateStyle(const theme::State& theme) {
    return {theme.font.family, kDateSize, PANGO_WEIGHT_MEDIUM, theme.colors.textSubtle, 1.8};
}

TextStyle timeStyle(const theme::State& theme, double size) {
    return {theme.font.monoFamily, size, PANGO_WEIGHT_LIGHT, theme.colors.text};
}

TextStyle slashStyle(const theme::State& theme, double size) {
    return {theme.font.monoFamily, size * kSlashRatio, PANGO_WEIGHT_LIGHT, theme.colors.primary};
}

TextStyle zoneStyle(const theme::State& theme) {
    return {theme.font.monoFamily, kStackSize, PANGO_WEIGHT_BOLD, theme.colors.primary, 1.2};
}

TextStyle zoneLabelStyle(const theme::State& theme) {
    return {theme.font.family, kStackLabelSize, PANGO_WEIGHT_MEDIUM, theme.colors.textMuted, 1.6};
}

TextStyle secondsStyle(const theme::State& theme) {
    return {theme.font.monoFamily, kSecondsSize, PANGO_WEIGHT_NORMAL, theme.colors.textSubtle, 1.0};
}

std::string formatNow(const char* fmt) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::array<char, 128> buf{};
    std::strftime(buf.data(), buf.size(), fmt, &tm);
    return {buf.data()};
}

std::string uppercase(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); }
    }
    return text;
}

}  // namespace

std::string Clock::dateString() {
    return uppercase(formatNow("%A, %B %-d"));
}

std::string Clock::zoneString() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    const long offset = tm.tm_gmtoff;
    const char sign = offset < 0 ? '-' : '+';
    const long hours = (offset < 0 ? -offset : offset) / 3600;
    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "UTC%c%02ld", sign, hours);
    return {buf.data()};
}

std::string Clock::secondsString() {
    return "." + formatNow("%S") + " SEC";
}

Clock::Metrics Clock::metrics(Painter& p) const {
    const theme::State& t = theme();
    Metrics m;
    m.index = p.measureText("// 01", indexStyle(t));
    m.date = p.measureText(dateString(), dateStyle(t));
    m.hours = p.measureText(formatNow("%H"), timeStyle(t, fontSize));
    m.minutes = p.measureText(formatNow("%M"), timeStyle(t, fontSize));
    m.slash = p.measureText("/", slashStyle(t, fontSize));
    m.zone = p.measureText(zoneString(), zoneStyle(t));
    m.zoneLabel = p.measureText("LOCAL TIME", zoneLabelStyle(t));
    m.seconds = p.measureText(secondsString(), secondsStyle(t));

    const double gap = t.spacing.small;
    const double timeWidth = m.hours.w + gap + m.slash.w + gap + m.minutes.w;
    m.stackWidth = std::max({m.zone.w, m.zoneLabel.w, m.seconds.w});
    m.stackX = timeWidth + t.spacing.large;

    const double metaWidth = m.index.w + gap + m.date.w;
    m.width = std::max({metaWidth, timeWidth, m.stackX + m.stackWidth});
    // The side stack only earns its place when the column still has room
    // for it next to the digits.
    m.stackFits = maxWidth <= 0.0 || (m.stackX + m.stackWidth) <= maxWidth;

    const double ruleGap = t.spacing.medium;
    m.height = m.index.h + t.spacing.small + 1.0 + ruleGap + m.hours.h;
    return m;
}

Size Clock::measure(Painter& p) const {
    const Metrics m = metrics(p);
    return {m.width, m.height};
}

void Clock::draw(Painter& p, int64_t /*now*/) {
    const theme::State& t = theme();
    const Metrics m = metrics(p);
    const double gap = t.spacing.small;
    // The lockup floats directly on the wallpaper: it carries the same drop
    // shadow the other lock-screen text has.
    const double shadowAlpha = t.effects.shadowOpacity;
    const double shadowOffset = t.effects.shadowOffset;

    double y = bounds.y;

    // Index + date line: the block's only "caption" row.
    p.drawTextShadowed(bounds.x, y, "// 01", indexStyle(t), HAlign::Left, shadowAlpha,
                       shadowOffset);
    p.drawTextShadowed(bounds.x + m.index.w + gap, y, dateString(), dateStyle(t), HAlign::Left,
                       shadowAlpha, shadowOffset);
    y += m.index.h + t.spacing.small;

    // Fading hairline: the architectural divider of the mock, drawn as a
    // gradient so it dissolves instead of cutting the column in two.
    const double ruleW = std::min(bounds.w, kRuleWidth);
    p.horizontalGradient({bounds.x, y, ruleW, 1.0}, t.colors.primary.withAlpha(0.55),
                         t.colors.primary.withAlpha(0.0));
    y += 1.0 + t.spacing.medium;

    // The oversized time: hours, an accent slash, minutes.
    double x = bounds.x;
    p.drawTextShadowed(x, y, formatNow("%H"), timeStyle(t, fontSize), HAlign::Left, shadowAlpha,
                       shadowOffset);
    x += m.hours.w + gap;
    p.drawTextShadowed(x, y + ((m.hours.h - m.slash.h) / 2.0), "/", slashStyle(t, fontSize),
                       HAlign::Left, shadowAlpha, shadowOffset);
    x += m.slash.w + gap;
    p.drawTextShadowed(x, y, formatNow("%M"), timeStyle(t, fontSize), HAlign::Left, shadowAlpha,
                       shadowOffset);

    if (!m.stackFits) { return; }

    // Side stack: zone, label and seconds, bottom-aligned with the digits.
    const double stackX = bounds.x + m.stackX;
    const double bottom = y + m.hours.h;
    double sy = bottom - m.seconds.h;
    p.drawTextShadowed(stackX, sy, secondsString(), secondsStyle(t), HAlign::Left, shadowAlpha,
                       shadowOffset);
    sy -= m.zoneLabel.h + 2.0;
    p.drawTextShadowed(stackX, sy, "LOCAL TIME", zoneLabelStyle(t), HAlign::Left, shadowAlpha,
                       shadowOffset);
    sy -= m.zone.h + 4.0;
    p.drawTextShadowed(stackX, sy, zoneString(), zoneStyle(t), HAlign::Left, shadowAlpha,
                       shadowOffset);
}

}  // namespace qypr
