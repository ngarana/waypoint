#pragma once

// Solar day/night resolution for the waylaunch theme mode.
//
// The launcher, switcher, power overlay, and dropdown strip share one
// [theme] mode ("dark" | "light" | "auto"). "auto" follows the solar window
// at the location cached by qypr-bar. This process only reads that private
// cache; it never opens a location service connection or performs network
// lookups. Missing or polar locations retain the dark fallback.

#include <chrono>
#include <optional>
#include <string>

#include "core/SolarLocationCache.hpp"

namespace waylaunch {

// Minutes since local midnight; always a same-day window (SolarCalc never
// produces midnight-spanning windows).
struct solar_window {
    int sunrise_min = 0;
    int sunset_min = 0;
};

// Pure decision: "light" when configured == "auto" and now_min falls inside
// the window, "dark" otherwise (including configured dark, unknown values,
// and auto without a window). Unit-tested without I/O or clock.
std::string effective_theme_mode(const std::string& configured,
                                 const std::optional<solar_window>& window, int now_min);

// Today's solar window for a fix; nullopt on polar day/night.
std::optional<solar_window> solar_window_for_today(double latitude_deg, double longitude_deg);

// Long-lived cache reader: checks for location updates at most once per
// minute and recalculates when the location, local day, or UTC offset changes.
// Safe to call on every UI poll tick.
class solar_tracker {
  public:
    // Idempotent initial cache read. Does no D-Bus or network I/O.
    void warm();
    std::string effective_mode(const std::string& configured);

  private:
    void refresh_if_needed();

    bool started_ = false;
    std::optional<qypr::SolarLocation> location_;
    std::optional<solar_window> window_;
    int window_year_ = -1;
    int window_yday_ = -1;
    int window_tz_offset_min_ = 0;
    int64_t cache_fetched_at_ = 0;
    std::chrono::steady_clock::time_point last_cache_check_;
};

} // namespace waylaunch
