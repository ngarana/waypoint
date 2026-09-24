// ThemeRuntime.cpp - Implementation of theme runtime coordinator.
#include "core/ThemeRuntime.hpp"

#include <filesystem>

#include "core/SolarCalc.hpp"

namespace qypr {

ThemeRuntime::ThemeRuntime(EventLoop& loop)
    : loop_(loop),
      palette_(theme::AutoPalette::fromConfig(Config{}, theme::localHourNow())) {}

ThemeRuntime::~ThemeRuntime() {
    stopMinuteTimer();
    paletteWatcher_.stop();
    paletteLightWatcher_.stop();
}

void ThemeRuntime::init(const Config& config, ThemeChangeCallback onThemeChanged,
                        InvalidateCallback onInvalidate) {
    onThemeChanged_ = std::move(onThemeChanged);
    onInvalidate_ = std::move(onInvalidate);
    palette_ = theme::AutoPalette::fromConfig(config, theme::localHourNow());
    applyTheme(config);
}

void ThemeRuntime::applyTheme(const Config& config) {
    theme_ = theme::loadThemeState(config, palette_);
    if (onThemeChanged_) { onThemeChanged_(theme_); }
}

void ThemeRuntime::watchPalette(const Config& config) {
    paletteWatcher_.stop();
    paletteLightWatcher_.stop();

    const auto arm = [this, &config](ConfigWatcher& w, const std::string& path) {
        if (path.empty()) { return; }
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(path).parent_path();
        if (!std::filesystem::is_directory(parent, ec)) { return; }
        w.watch(path, [this, &config] {
            applyTheme(config);
            if (onInvalidate_) onInvalidate_();
        });
    };

    arm(paletteWatcher_, theme::resolveColorsPath(config, /*lightPalette=*/false));
    if (palette_.mode != "dark") {
        arm(paletteLightWatcher_, theme::resolveColorsPath(config, /*lightPalette=*/true));
    }
}

bool ThemeRuntime::refreshSolarTimes(const std::optional<GeoFix>& fix) {
    if (palette_.location != "auto") {
        palette_.clearSolarTimes();
        return false;
    }
    if (!fix) {
        palette_.clearSolarTimes();
        return false;
    }
    const auto times =
        solarTimesForDate(fix->latitude, fix->longitude, localDateNow(), localTzOffsetMin());
    if (!times) {
        palette_.clearSolarTimes();  // polar day/night: fixed hours carry the mode
        return false;
    }
    palette_.setSolarTimes(times->sunriseMin, times->sunsetMin);
    return true;
}

void ThemeRuntime::startMinuteTimer(const Config& config) {
    stopMinuteTimer();
    minuteTimerId_ = loop_.addTimer(60000, true, [this, &config] {
        if (palette_.tick(theme::localHourNow())) {
            applyTheme(config);
            if (onPaletteTransition_) { onPaletteTransition_(palette_.resolved); }
            if (onInvalidate_) onInvalidate_();
        }
    });
}

void ThemeRuntime::stopMinuteTimer() {
    if (minuteTimerId_ >= 0) {
        loop_.removeTimer(minuteTimerId_);
        minuteTimerId_ = -1;
    }
}

bool ThemeRuntime::tick(int hour, const Config& config) {
    if (palette_.tick(hour)) {
        applyTheme(config);
        if (onPaletteTransition_) { onPaletteTransition_(palette_.resolved); }
        if (onInvalidate_) onInvalidate_();
        return true;
    }
    return false;
}

}  // namespace qypr
