#include "waylaunch/solar.h"

#include <cassert>
#include <iostream>

using namespace waylaunch;

void test_static_modes_pass_through() {
    assert(effective_theme_mode("dark", std::nullopt, 720) == "dark");
    assert(effective_theme_mode("light", std::nullopt, 720) == "light");
    // Unknown values stay dark, matching MatugenTheme's own fallback.
    assert(effective_theme_mode("sometimes", std::nullopt, 720) == "dark");
    // A cached window never overrides an explicit static mode.
    solar_window midday{.sunrise_min = 360, .sunset_min = 1200};
    assert(effective_theme_mode("dark", midday, 720) == "dark");
    assert(effective_theme_mode("light", midday, 0) == "light");
    std::cout << "[PASS] static modes pass through\n";
}

void test_auto_follows_window() {
    solar_window day{.sunrise_min = 360, .sunset_min = 1200}; // 06:00–20:00
    assert(effective_theme_mode("auto", day, 359) == "dark");
    assert(effective_theme_mode("auto", day, 360) == "light");
    assert(effective_theme_mode("auto", day, 720) == "light");
    assert(effective_theme_mode("auto", day, 1199) == "light");
    assert(effective_theme_mode("auto", day, 1200) == "dark");
    assert(effective_theme_mode("auto", day, 0) == "dark");
    std::cout << "[PASS] auto follows window\n";
}

void test_auto_without_window_is_dark() {
    // No fix yet, polar day/night, or unreadable clock: dark, like today.
    assert(effective_theme_mode("auto", std::nullopt, 720) == "dark");
    assert(effective_theme_mode("auto", std::nullopt, 0) == "dark");
    solar_window day{.sunrise_min = 360, .sunset_min = 1200};
    assert(effective_theme_mode("auto", day, -1) == "dark");
    std::cout << "[PASS] auto without window is dark\n";
}

void test_window_for_today_sane() {
    // Structural only (the day length varies by season; exact minutes belong
    // to the shared SolarCalc goldens in common/tests/solar_test.cpp).
    // Berlin is never polar, so a window always exists.
    auto berlin = solar_window_for_today(52.52, 13.40);
    assert(berlin.has_value());
    assert(berlin->sunrise_min >= 0 && berlin->sunrise_min < berlin->sunset_min);
    assert(berlin->sunset_min < 1440);
    const int length = berlin->sunset_min - berlin->sunrise_min;
    assert(length > 7 * 60 && length < 17 * 60); // Berlin extremes: ~7h40–16h50
    std::cout << "[PASS] window for today sane\n";
}

int main() {
    test_static_modes_pass_through();
    test_auto_follows_window();
    test_auto_without_window_is_dark();
    test_window_for_today_sane();
    std::cout << "solar_mode_test: all passed\n";
    return 0;
}
