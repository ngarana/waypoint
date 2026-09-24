// ClockIndicator.cpp - Status bar clock indicator implementation.
#include "ui/indicators/ClockIndicator.hpp"

#include <array>
#include <cstdlib>
#include <ctime>

#include "core/Config.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

namespace {
// strftime patterns. Overridable via `[clock] format` / `tooltip-format`;
// qypr-lock passes no config and keeps these.
constexpr const char* kDefaultFormat = "%a %b %-d   %-I:%M %p";
constexpr const char* kDefaultTooltipFormat = "%A, %B %-d";

std::string formatNow(const std::string& fmt) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::array<char, 256> buf{};
    // strftime returns 0 both for "empty result" and "didn't fit"; either way an
    // empty string is the honest answer for a format we cannot render.
    const size_t n = std::strftime(buf.data(), buf.size(), fmt.c_str(), &tm);
    return n == 0 ? std::string() : std::string(buf.data(), n);
}

std::string strf(const std::tm& tm, const char* fmt) {
    std::array<char, 64> buf{};
    const size_t n = std::strftime(buf.data(), buf.size(), fmt, &tm);
    return {buf.data(), n};
}

// Current local time in an IANA zone. Saves/restores TZ around tzset() (safe on
// the single UI thread). A short label ("Asia/Tokyo" → "Tokyo") comes free.
std::string zoneTime(const std::string& zone, const char* fmt) {
    const char* old = std::getenv("TZ");
    const std::string saved = old ? old : "";
    const bool had = old != nullptr;
    setenv("TZ", zone.c_str(), 1);
    tzset();
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_r(&t, &lt);
    std::string out = strf(lt, fmt);
    if (had)
        setenv("TZ", saved.c_str(), 1);
    else
        unsetenv("TZ");
    tzset();
    return out;
}

std::string zoneLabel(const std::string& zone) {
    std::string s = zone.substr(zone.find_last_of('/') + 1);
    for (char& c : s)
        if (c == '_') c = ' ';
    return s;
}

// ── Calendar popover ────────────────────────────────────────────────────────
constexpr double kPad = 14.0;
constexpr double kWeekColW = 26.0;
constexpr double kCellW = 36.0;
constexpr double kCellH = 30.0;
constexpr double kHeaderH = 38.0;
constexpr double kWeekdayH = 22.0;
constexpr int kRows = 6;
constexpr double kTzRowH = 22.0;

class CalendarPopover : public DetailedPopover {
public:
    explicit CalendarPopover(std::vector<std::string> zones) : zones_(std::move(zones)) {}

    [[nodiscard]] double contentWidth() const override { return kPad * 2 + kWeekColW + 7 * kCellW; }
    [[nodiscard]] double contentHeight() const override {
        double h = kPad * 2 + kHeaderH + kWeekdayH + kRows * kCellH;
        if (!zones_.empty()) h += 8.0 + static_cast<double>(zones_.size()) * kTzRowH + 4.0;
        return h;
    }

    void draw(Painter& p, int64_t now) override {
        Rect b = getBounds();
        b.y += (growUp ? 1.0 : -1.0) * (1.0 - openProgress_.value(now)) * 6.0;
        if (!drawSharedBackdrop(p, b, theme().statusbar.popoverRadius))
            p.fillRoundedRectSource(b, theme().statusbar.popoverRadius, theme().panelSurface());

        // Displayed month = current month + monthOffset_.
        std::time_t t = std::time(nullptr);
        std::tm lt{};
        localtime_r(&t, &lt);
        const int todayY = lt.tm_year + 1900;
        const int todayM = lt.tm_mon;
        const int todayD = lt.tm_mday;
        int total = todayY * 12 + todayM + monthOffset_;
        int dy = total / 12;
        int dm = total % 12;
        if (dm < 0) {
            dm += 12;
            --dy;
        }

        // ── Header: ‹  Month Year  › ─────────────────────────────────────────
        std::tm title{};
        title.tm_year = dy - 1900;
        title.tm_mon = dm;
        title.tm_mday = 1;
        title.tm_hour = 12;
        std::mktime(&title);
        const std::string head = strf(title, "%B %Y");

        const double hy = b.y + kPad;
        TextStyle nav{theme().font.family, 20.0, PANGO_WEIGHT_NORMAL, theme().colors.text};
        prev_ = {b.x + kPad, hy - 2.0, 28.0, 28.0};
        next_ = {b.x + b.w - kPad - 28.0, hy - 2.0, 28.0, 28.0};
        if (prev_.contains(hoverX_, hoverY_))
            p.fillRoundedRect(prev_, 8.0, theme().colors.glassHover);
        if (next_.contains(hoverX_, hoverY_))
            p.fillRoundedRect(next_, 8.0, theme().colors.glassHover);
        p.drawText(prev_.x, hy, "‹", nav, HAlign::Left);
        p.drawText(next_.x + 6.0, hy, "›", nav, HAlign::Left);

        TextStyle ht{theme().font.family, 15.0, PANGO_WEIGHT_BOLD, theme().colors.text};
        p.drawText(b.x + b.w / 2.0, hy, head, ht, HAlign::Center);

        // A "Today" reset, only while off the current month.
        today_ = {0, 0, 0, 0};
        if (monthOffset_ != 0) {
            TextStyle tt{theme().font.family, 11.0, PANGO_WEIGHT_NORMAL, theme().colors.primary};
            const Size ts = p.measureText("Today", tt);
            today_ = {b.x + b.w / 2.0 - ts.w / 2.0 - 6.0, hy + 20.0, ts.w + 12.0, 18.0};
            p.drawText(b.x + b.w / 2.0, hy + 21.0, "Today", tt, HAlign::Center);
        }

        // ── Weekday header (Mon-first) ───────────────────────────────────────
        static const std::array<const char*, 7> wd = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
        const double gridX = b.x + kPad;
        double wy = b.y + kPad + kHeaderH;
        TextStyle wds{theme().font.family, 11.0, PANGO_WEIGHT_BOLD, theme().colors.textSubtle};
        for (int c = 0; c < 7; ++c) {
            const double cx = gridX + kWeekColW + c * kCellW + kCellW / 2.0;
            Color col = c >= 5 ? theme().colors.primary.withAlpha(0.7) : theme().colors.textSubtle;
            p.drawText(cx, wy, wd.at(static_cast<size_t>(c)),
                       {wds.family, wds.size, wds.weight, col}, HAlign::Center);
        }

        // ── Day grid ─────────────────────────────────────────────────────────
        // Walk a single tm from the Monday on/before the 1st, one day per cell.
        int firstWday = (title.tm_wday + 6) % 7;  // title still = day 1; Mon=0
        std::tm cell{};
        cell.tm_year = dy - 1900;
        cell.tm_mon = dm;
        cell.tm_mday = 1 - firstWday;
        cell.tm_hour = 12;
        std::mktime(&cell);

        const double gy = b.y + kPad + kHeaderH + kWeekdayH;
        for (int r = 0; r < kRows; ++r) {
            // Week number from the row's Monday.
            TextStyle wn{theme().font.family, 10.0, PANGO_WEIGHT_NORMAL,
                         theme().colors.textSubtle.withAlpha(0.6)};
            p.drawText(gridX + kWeekColW / 2.0, gy + r * kCellH + (kCellH - 12.0) / 2.0,
                       strf(cell, "%V"), wn, HAlign::Center);

            for (int c = 0; c < 7; ++c) {
                const bool inMonth = cell.tm_mon == dm;
                const bool isToday =
                    inMonth && cell.tm_mday == todayD && dy == todayY && dm == todayM;
                const double cx = gridX + kWeekColW + c * kCellW + kCellW / 2.0;
                const double cyTop = gy + r * kCellH;

                if (isToday) {
                    p.fillCircle(cx, cyTop + kCellH / 2.0, 13.0, theme().colors.primary);
                }
                Color fg = theme().colors.text;
                if (isToday) {
                    fg = theme().colors.background;
                } else if (!inMonth) {
                    fg = theme().colors.textSubtle.withAlpha(0.35);
                } else if (c >= 5) {
                    fg = theme().colors.primary.withAlpha(0.85);
                }
                TextStyle ds{theme().font.family, 12.0,
                             isToday ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL, fg};
                const std::string d = std::to_string(cell.tm_mday);
                const Size dsz = p.measureText(d, ds);
                p.drawText(cx, cyTop + (kCellH - dsz.h) / 2.0, d, ds, HAlign::Center);

                cell.tm_mday += 1;
                std::mktime(&cell);
            }
        }

        // ── Secondary timezones ──────────────────────────────────────────────
        if (!zones_.empty()) {
            double ty = gy + kRows * kCellH + 8.0;
            p.fillRectSource({b.x + kPad, ty, b.w - kPad * 2, 1.0}, theme().panelSurfaceHover());
            ty += 6.0;
            for (const auto& z : zones_) {
                TextStyle ls{theme().font.family, 12.0, PANGO_WEIGHT_NORMAL, theme().colors.text};
                TextStyle rs{theme().font.family, 12.0, PANGO_WEIGHT_BOLD,
                             theme().colors.textSubtle};
                p.drawText(b.x + kPad, ty + 3.0, zoneLabel(z), ls, HAlign::Left);
                p.drawText(b.x + b.w - kPad, ty + 3.0, zoneTime(z, "%H:%M"), rs, HAlign::Right);
                ty += kTzRowH;
            }
        }
    }

    bool handleClick(double x, double y) override {
        if (prev_.contains(x, y)) {
            --monthOffset_;
            return true;
        }
        if (next_.contains(x, y)) {
            ++monthOffset_;
            return true;
        }
        if (today_.contains(x, y)) {
            monthOffset_ = 0;
            return true;
        }
        return false;
    }

    bool handleDrag(double x, double y) override {
        hoverX_ = x;
        hoverY_ = y;
        return false;
    }

    bool handleScroll(double, double dy) override {
        monthOffset_ += (dy > 0 ? -1 : 1);  // scroll up = earlier month
        return true;
    }

private:
    std::vector<std::string> zones_;
    int monthOffset_ = 0;
    Rect prev_{0, 0, 0, 0}, next_{0, 0, 0, 0}, today_{0, 0, 0, 0};
    double hoverX_ = -1, hoverY_ = -1;
};
}  // namespace

ClockIndicator::ClockIndicator(const SystemBackends& backends)
    : StatusIndicator("clock", Zone::Left, 0) {
    if (backends.config) {
        format_ = backends.config->getString("clock", "format", kDefaultFormat);
        tooltipFormat_ =
            backends.config->getString("clock", "tooltip-format", kDefaultTooltipFormat);
        timezones_ = backends.config->getList("clock", "timezones");
    }
}

std::unique_ptr<DetailedPopover> ClockIndicator::createDetailedView() {
    return std::make_unique<CalendarPopover>(timezones_);
}

std::string ClockIndicator::timeString() const {
    return formatNow(format_);
}

std::string ClockIndicator::dateString() const {
    return formatNow(tooltipFormat_);
}

std::string ClockIndicator::label() const {
    return cachedTime_.empty() ? timeString() : cachedTime_;
}

std::string ClockIndicator::tooltip() const {
    return dateString();
}

double ClockIndicator::labelFontSize() const {
    return theme().statusbar.clockIconSize;
}

void ClockIndicator::poll(int64_t now) {
    if (now - lastPoll_ >= 1000) {
        cachedTime_ = timeString();
        lastPoll_ = now;
    }
}

REGISTER_INDICATOR("clock", Zone::Left, 0, ClockIndicator)

}  // namespace qypr
