// SolarCalc.hpp - Sunrise/sunset from latitude/longitude (NOAA algorithm).
//
// Pure date math for the bar's auto palette mode: given a GeoClue fix and a
// civil date, yields sunrise/sunset as minutes since local midnight. No I/O,
// no clock (callers pass "today" in), so the golden-value tests need no
// mocking. Accuracy is ~±1 minute — plenty for a theme switch.
//
// Returns std::nullopt on polar day/night (the sun never crosses the
// 90.833° zenith): the caller falls back to the configured fixed hours.
#pragma once

#include <optional>

namespace qypr {

struct CivilDate {
    int year = 1970;
    int month = 1;
    int day = 1;
};

struct SolarTimes {
    int sunriseMin = 0;  // minutes since local midnight, [0, 1440)
    int sunsetMin = 0;
};

std::optional<SolarTimes> solarTimesForDate(double latitudeDeg, double longitudeDeg, CivilDate date,
                                            int tzOffsetMin);

// "Today" helpers for the bar (localtime-based, DST-aware via tm_gmtoff).
CivilDate localDateNow();
// Minutes east of UTC for the local zone (negative west of Greenwich).
int localTzOffsetMin();

}  // namespace qypr
