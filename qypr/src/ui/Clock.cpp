#include "ui/Clock.hpp"

#include <ctime>

#include "ui/Theme.hpp"

namespace qypr {

namespace {
TextStyle timeStyle(const theme::State& theme) {
    return {theme.font.family, static_cast<double>(theme.font.sizeClock), PANGO_WEIGHT_BOLD,
            theme.colors.text};
}
TextStyle dateStyle(const theme::State& theme) {
    return {theme.font.family, static_cast<double>(theme.font.sizeDate), PANGO_WEIGHT_BOLD,
            theme.colors.textSubtle};
}

std::string formatNow(const char* fmt) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[128];
    std::strftime(buf, sizeof(buf), fmt, &tm);
    return buf;
}
}  // namespace

std::string Clock::timeString() const {
    return formatNow("%H:%M");
}
std::string Clock::dateString() const {
    return formatNow("%A, %B %-d");
}

Size Clock::measure(Painter& p) const {
    Size t = p.measureText(timeString(), timeStyle(theme()));
    Size d = p.measureText(dateString(), dateStyle(theme()));
    return {std::max(t.w, d.w), t.h + theme().spacing.small + d.h};
}

void Clock::draw(Painter& p, double centerX, double topY) const {
    const double shadowA = theme().effects.shadowOpacity;
    const double shadowOff = theme().effects.shadowOffset;

    std::string time = timeString();
    Size t = p.measureText(time, timeStyle(theme()));
    p.drawTextShadowed(centerX, topY, time, timeStyle(theme()), HAlign::Center, shadowA, shadowOff);

    double dateY = topY + t.h + theme().spacing.small;
    p.drawTextShadowed(centerX, dateY, dateString(), dateStyle(theme()), HAlign::Center, shadowA,
                       shadowOff);
}

}  // namespace qypr
