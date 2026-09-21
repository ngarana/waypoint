#include "waylaunch/solar.h"

#include <ctime>

#include "core/SolarCalc.hpp"

namespace waylaunch {
namespace {

// Minutes since local midnight for "now"; -1 when the clock is unreadable
// (never in practice — keeps the pure decision total).
int now_minutes() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_r(&now, &local) == nullptr) return -1;
    return (local.tm_hour * 60) + local.tm_min;
}

int day_of_year() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_r(&now, &local) == nullptr) return -1;
    return local.tm_yday;
}

constexpr std::chrono::hours kFixRefreshInterval{1};

} // namespace

std::string effective_theme_mode(const std::string& configured,
                                 const std::optional<solar_window>& window, int now_min) {
    if (configured == "light") return "light";
    if (configured != "auto" || !window.has_value() || now_min < 0) return "dark";
    if (now_min >= window->sunrise_min && now_min < window->sunset_min) return "light";
    return "dark";
}

std::optional<solar_window> solar_window_for_today(double latitude_deg, double longitude_deg) {
    auto times = qypr::solarTimesForDate(latitude_deg, longitude_deg, qypr::localDateNow(),
                                         qypr::localTzOffsetMin());
    if (!times.has_value()) return std::nullopt;
    return solar_window{.sunrise_min = times->sunriseMin, .sunset_min = times->sunsetMin};
}

void solar_tracker::warm() {
    if (started_) return;
    started_ = true;
    refresh_if_needed();
}

std::string solar_tracker::effective_mode(const std::string& configured) {
    if (configured != "auto") return effective_theme_mode(configured, std::nullopt, 0);
    // Lazy start: transient overlays reach this on their first poll tick,
    // after first paint, so GeoClue activation never delays the popup.
    if (!started_) { started_ = true; }
    refresh_if_needed();
    return effective_theme_mode(configured, window_, now_minutes());
}

void solar_tracker::refresh_if_needed() {
    if (!started_) return;
    const auto now = std::chrono::steady_clock::now();
    // One D-Bus roundtrip per hour at most: a denied/absent GeoClue must not
    // be re-dialed every poll tick, and a held fix only needs hourly travel
    // updates. The day rollover below still runs on every call.
    const bool due = last_attempt_ == std::chrono::steady_clock::time_point{} ||
                     last_attempt_ + kFixRefreshInterval < now;
    if (due) {
        last_attempt_ = now;
        if (!geoclue_.fix().has_value() && !geoclue_.start()) return;
        geoclue_.refresh();
    }
    const auto& fix = geoclue_.fix();
    if (!fix.has_value()) return;
    int yday = day_of_year();
    if (window_yday_ != yday) {
        window_ = solar_window_for_today(fix->latitude, fix->longitude);
        window_yday_ = yday;
    }
}

} // namespace waylaunch
