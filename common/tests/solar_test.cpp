// solar_test.cpp - Unit tests for shared core/SolarCalc.
// Assert-based, like the other shared suites (fails loud, no framework).
// Golden values verified against an independent sunrise-equation
// implementation to ±1 min (equator equinox and Berlin solstices agree;
// longitude shifts the window by exactly the meridian math).
#include "core/SolarCalc.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

void near(int got, int want, int tol, const char* what) {
    if (std::abs(got - want) > tol) {
        std::printf("FAIL %s: got %d, want %d ± %d\n", what, got, want, tol);
        assert(false);
    }
}

void test_equinox() {
    // Equator, March equinox: ~06:04/18:11 UTC (equation-of-time shifts both
    // off the round hour).
    const auto t =
        qypr::solarTimesForDate(0.0, 0.0, qypr::CivilDate{.year = 2026, .month = 3, .day = 20}, 0);
    assert(t.has_value());
    near(t->sunriseMin, (6 * 60) + 4, 6, "equinox sunrise");
    near(t->sunsetMin, (18 * 60) + 11, 6, "equinox sunset");
    std::printf("[PASS] equinox golden\n");
}

void test_solstices() {
    // Berlin midsummer (CEST = UTC+120): long day, ~04:43/21:33 local.
    const auto summer = qypr::solarTimesForDate(
        52.52, 13.40, qypr::CivilDate{.year = 2026, .month = 6, .day = 21}, 120);
    assert(summer.has_value());
    near(summer->sunriseMin, (4 * 60) + 43, 15, "summer sunrise");
    near(summer->sunsetMin, (21 * 60) + 33, 15, "summer sunset");
    assert(summer->sunsetMin - summer->sunriseMin > 16 * 60);
    // Berlin midwinter (CET = UTC+60): short day, ~08:15/15:54 local.
    const auto winter = qypr::solarTimesForDate(
        52.52, 13.40, qypr::CivilDate{.year = 2026, .month = 12, .day = 21}, 60);
    assert(winter.has_value());
    near(winter->sunriseMin, (8 * 60) + 15, 15, "winter sunrise");
    near(winter->sunsetMin, (15 * 60) + 54, 15, "winter sunset");
    assert(winter->sunsetMin - winter->sunriseMin < 9 * 60);
    std::printf("[PASS] solstice goldens\n");
}

void test_longitude_shift() {
    // 15° east ≈ an hour earlier on the UTC clock (same meridian math).
    const auto base =
        qypr::solarTimesForDate(0.0, 0.0, qypr::CivilDate{.year = 2026, .month = 3, .day = 20}, 0);
    const auto east =
        qypr::solarTimesForDate(0.0, 15.0, qypr::CivilDate{.year = 2026, .month = 3, .day = 20}, 0);
    assert(base.has_value() && east.has_value());
    near(base->sunriseMin - east->sunriseMin, 60, 5, "longitude sunrise shift");
    near(base->sunsetMin - east->sunsetMin, 60, 5, "longitude sunset shift");
    std::printf("[PASS] longitude shift\n");
}

void test_polar_nullopt() {
    // Polar night (Tromsø, December) and polar day (Tromsø, June): the sun
    // never crosses the zenith → nullopt, and the caller falls back to the
    // configured fixed hours.
    assert(!qypr::solarTimesForDate(69.65, 18.96,
                                    qypr::CivilDate{.year = 2026, .month = 12, .day = 21}, 60)
                .has_value());
    assert(!qypr::solarTimesForDate(69.65, 18.96,
                                    qypr::CivilDate{.year = 2026, .month = 6, .day = 21}, 120)
                .has_value());
    std::printf("[PASS] polar nullopt\n");
}

}  // namespace

int main() {
    test_equinox();
    test_solstices();
    test_longitude_shift();
    test_polar_nullopt();
    printf("All SolarCalc unit tests passed successfully!\n");
    return 0;
}
