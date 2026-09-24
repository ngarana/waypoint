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

constexpr std::chrono::seconds kCachePollInterval{60};

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
    // Lazy start lets transient overlays read the shared cache after first
    // paint; cache I/O is bounded to once per minute.
    if (!started_) { started_ = true; }
    refresh_if_needed();
    return effective_theme_mode(configured, window_, now_minutes());
}

void solar_tracker::refresh_if_needed() {
    if (!started_) return;
    const auto now = std::chrono::steady_clock::now();
    if (last_cache_check_ == std::chrono::steady_clock::time_point{} ||
        last_cache_check_ + kCachePollInterval <= now) {
        last_cache_check_ = now;
        const auto cached = qypr::loadSolarLocationCache();
        const int64_t fetchedAt = cached ? cached->fetchedAt : 0;
        const std::optional<qypr::SolarLocation> location =
            cached ? std::optional<qypr::SolarLocation>(cached->location) : std::nullopt;
        if (fetchedAt != cache_fetched_at_ || location != location_) {
            cache_fetched_at_ = fetchedAt;
            location_ = location;
            window_year_ = -1;
            window_yday_ = -1;
        }
    }

    if (!location_) {
        window_.reset();
        return;
    }

    std::time_t timestamp = std::time(nullptr);
    std::tm local{};
    if (localtime_r(&timestamp, &local) == nullptr) return;
    const int tzOffsetMin = qypr::localTzOffsetMin();
    if (window_year_ != local.tm_year || window_yday_ != local.tm_yday ||
        window_tz_offset_min_ != tzOffsetMin) {
        window_ = solar_window_for_today(location_->latitude, location_->longitude);
        window_year_ = local.tm_year;
        window_yday_ = local.tm_yday;
        window_tz_offset_min_ = tzOffsetMin;
    }
}

} // namespace waylaunch
