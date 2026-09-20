#include "ui/Clock.hpp"

#include <ctime>

#include "ui/Theme.hpp"

namespace qypr {

namespace {
TextStyle timeStyle() {
    return {theme::font::family, theme::font::sizeClock, PANGO_WEIGHT_BOLD, theme::color::text};
}
TextStyle dateStyle() {
    return {theme::font::family, theme::font::sizeDate, PANGO_WEIGHT_BOLD,
            theme::color::textSubtle};
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
    Size t = p.measureText(timeString(), timeStyle());
    Size d = p.measureText(dateString(), dateStyle());
    return {std::max(t.w, d.w), t.h + theme::spacing::small + d.h};
}

void Clock::draw(Painter& p, double centerX, double topY) const {
    const double shadowA = theme::effects::shadowOpacity;
    const double shadowOff = theme::effects::shadowOffset;

    std::string time = timeString();
    Size t = p.measureText(time, timeStyle());
    p.drawTextShadowed(centerX, topY, time, timeStyle(), HAlign::Center, shadowA, shadowOff);

    double dateY = topY + t.h + theme::spacing::small;
    p.drawTextShadowed(centerX, dateY, dateString(), dateStyle(), HAlign::Center, shadowA,
                       shadowOff);
}

}  // namespace qypr
