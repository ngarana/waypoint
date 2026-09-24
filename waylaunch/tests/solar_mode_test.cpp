#include "waylaunch/solar.h"

#include <cassert>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <unistd.h>

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

void test_tracker_uses_shared_cache() {
    char cacheRoot[] = "/tmp/waylaunch-solar-cache-XXXXXX";
    char* created = ::mkdtemp(cacheRoot);
    assert(created != nullptr);
    const char* oldCacheHomeValue = std::getenv("XDG_CACHE_HOME");
    const std::optional<std::string> oldCacheHome =
        oldCacheHomeValue == nullptr ? std::nullopt : std::optional<std::string>(oldCacheHomeValue);
    assert(::setenv("XDG_CACHE_HOME", created, 1) == 0);

    const qypr::CachedSolarLocation cached{
        .location = {.latitude = 1.286389, .longitude = 36.817223},
        .fetchedAt = static_cast<int64_t>(std::time(nullptr)),
    };
    assert(qypr::storeSolarLocationCache(cached));

    solar_tracker tracker;
    tracker.warm();
    const auto window = solar_window_for_today(cached.location.latitude, cached.location.longitude);
    assert(window.has_value());
    const std::time_t timestamp = std::time(nullptr);
    std::tm local{};
    assert(::localtime_r(&timestamp, &local) != nullptr);
    const int minute = (local.tm_hour * 60) + local.tm_min;
    assert(tracker.effective_mode("auto") == effective_theme_mode("auto", window, minute));

    if (oldCacheHome) {
        assert(::setenv("XDG_CACHE_HOME", oldCacheHome->c_str(), 1) == 0);
    } else {
        assert(::unsetenv("XDG_CACHE_HOME") == 0);
    }
    const std::string cacheFile = std::string(created) + "/qypr/solar-location";
    assert(::unlink(cacheFile.c_str()) == 0);
    assert(::rmdir((std::string(created) + "/qypr").c_str()) == 0);
    assert(::rmdir(created) == 0);
    std::cout << "[PASS] solar tracker reads shared cache\n";
}

int main() {
    test_static_modes_pass_through();
    test_auto_follows_window();
    test_auto_without_window_is_dark();
    test_window_for_today_sane();
    test_tracker_uses_shared_cache();
    std::cout << "solar_mode_test: all passed\n";
    return 0;
}
