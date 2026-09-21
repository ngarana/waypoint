// SolarCalc.cpp - NOAA sunrise/sunset implementation.
#include "core/SolarCalc.hpp"

#include <array>
#include <cmath>
#include <ctime>
#include <numbers>

namespace qypr {

namespace {
constexpr double kZenithDeg = 90.833;  // sunrise/sunset zenith (refraction + solar disc)
constexpr double kDayMinutes = 1440.0;

constexpr double degToRad(double deg) {
    return (deg * std::numbers::pi) / 180.0;
}
constexpr double radToDeg(double rad) {
    return (rad * 180.0) / std::numbers::pi;
}

// Day of year (1–366) for a civil date.
int dayOfYear(CivilDate date) {
    constexpr std::array<int, 12> kMonthDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (date.year % 4 == 0 && date.year % 100 != 0) || date.year % 400 == 0;
    int day = date.day;
    for (int m = 1; m < date.month; ++m) {
        day += kMonthDays.at(static_cast<size_t>(m - 1)) + ((m == 2 && leap) ? 1 : 0);
    }
    return day;
}

// Normalise an hour value into [0, 24).
double wrapHours(double hours) {
    while (hours < 0.0) { hours += 24.0; }
    while (hours >= 24.0) { hours -= 24.0; }
    return hours;
}

// UTC hour of the sunrise/sunset event for a day-of-year at
// (latitudeDeg, longitudeDeg). Returns < 0 when the sun never crosses the
// zenith (polar day/night) — the caller degrades to the fixed hours.
double eventUtcHour(int dayOfYear, double latitudeDeg, double longitudeDeg, bool isSunrise) {
    const double lngHour = longitudeDeg / 15.0;
    const double t = dayOfYear + (((isSunrise ? 6.0 : 18.0) - lngHour) / 24.0);
    const double meanAnomaly = (0.9856 * t) - 3.289;
    double sunLon = meanAnomaly + (1.916 * std::sin(degToRad(meanAnomaly))) +
                    (0.020 * std::sin(degToRad(2.0 * meanAnomaly))) + 282.634;
    while (sunLon < 0.0) { sunLon += 360.0; }
    while (sunLon >= 360.0) { sunLon -= 360.0; }
    double rightAsc = radToDeg(std::atan(0.91764 * std::tan(degToRad(sunLon))));
    while (rightAsc < 0.0) { rightAsc += 360.0; }
    while (rightAsc >= 360.0) { rightAsc -= 360.0; }
    // Match the ascension to the longitude's quadrant, then read in hours.
    rightAsc += (std::floor(sunLon / 90.0) * 90.0) - (std::floor(rightAsc / 90.0) * 90.0);
    rightAsc /= 15.0;
    const double sinDec = 0.39782 * std::sin(degToRad(sunLon));
    const double cosDec = std::cos(std::asin(sinDec));
    const double latRad = degToRad(latitudeDeg);
    const double cosHour = (std::cos(degToRad(kZenithDeg)) - (sinDec * std::sin(latRad))) /
                           (cosDec * std::cos(latRad));
    if (cosHour > 1.0 || cosHour < -1.0) { return -1.0; }
    double hourAngle = radToDeg(std::acos(cosHour));
    if (isSunrise) { hourAngle = 360.0 - hourAngle; }
    hourAngle /= 15.0;
    return wrapHours((hourAngle + rightAsc - (0.06571 * t) - 6.622) - lngHour);
}
}  // namespace

std::optional<SolarTimes> solarTimesForDate(double latitudeDeg, double longitudeDeg, CivilDate date,
                                            int tzOffsetMin) {
    const int day = dayOfYear(date);
    const double riseUtc = eventUtcHour(day, latitudeDeg, longitudeDeg, /*isSunrise=*/true);
    const double setUtc = eventUtcHour(day, latitudeDeg, longitudeDeg, /*isSunrise=*/false);
    if (riseUtc < 0.0 || setUtc < 0.0) { return std::nullopt; }
    const double offsetHours = tzOffsetMin / 60.0;
    const auto toLocalMin = [offsetHours](double utcHour) {
        const int mins = static_cast<int>(std::lround(wrapHours(utcHour + offsetHours) * 60.0));
        return mins % static_cast<int>(kDayMinutes);  // lround(24h) wraps to 0
    };
    return SolarTimes{.sunriseMin = toLocalMin(riseUtc), .sunsetMin = toLocalMin(setUtc)};
}

CivilDate localDateNow() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    return CivilDate{.year = local.tm_year + 1900, .month = local.tm_mon + 1, .day = local.tm_mday};
}

int localTzOffsetMin() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    return static_cast<int>(local.tm_gmtoff / 60);
}

}  // namespace qypr
