#pragma once

// Solar day/night resolution for the waylaunch theme mode.
//
// The launcher, switcher, power overlay, and dropdown strip share one
// [theme] mode ("dark" | "light" | "auto"). "auto" follows the solar window
// at the GeoClue fix (city accuracy only, same source as the qypr bar) and
// falls back to dark when location is off, denied, absent, or polar —
// matching the bar's fallback semantics without sharing its code.

#include <chrono>
#include <optional>
#include <string>

#include "waylaunch/geoclue.h"

namespace waylaunch {

// Minutes since local midnight; always a same-day window (SolarCalc never
// produces midnight-spanning windows).
struct solar_window {
    int sunrise_min = 0;
    int sunset_min = 0;
};

// Pure decision: "light" when configured == "auto" and now_min falls inside
// the window, "dark" otherwise (including configured dark, unknown values,
// and auto without a window). Unit-tested without D-Bus or clock.
std::string effective_theme_mode(const std::string& configured,
                                 const std::optional<solar_window>& window, int now_min);

// Today's solar window for a fix; nullopt on polar day/night.
std::optional<solar_window> solar_window_for_today(double latitude_deg, double longitude_deg);

// Long-lived owner: holds the GeoClue client, refreshes the fix at most
// hourly and recomputes the window when the day rolls over. Cheap enough to
// call on every poll tick; performs D-Bus roundtrips only in those windows.
class solar_tracker {
  public:
    // Idempotent: open the client and take the first fix now. Call at daemon
    // startup (dropdown); transient overlays skip it and converge on the
    // first poll tick instead, so GeoClue activation never delays first paint.
    void warm();
    std::string effective_mode(const std::string& configured);

  private:
    void refresh_if_needed();

    geoclue_client geoclue_;
    bool started_ = false;
    std::optional<solar_window> window_;
    int window_yday_ = -1;
    // Last fix attempt (success or failure): bounds D-Bus traffic to one
    // roundtrip per hour however the calls interleave.
    std::chrono::steady_clock::time_point last_attempt_;
};

} // namespace waylaunch
