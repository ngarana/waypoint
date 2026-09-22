// ThemeRuntime.hpp - Runtime coordinator for palette parsing, solar refresh,
// and live theme propagation.
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "core/Config.hpp"
#include "core/ConfigWatcher.hpp"
#include "core/EventLoop.hpp"
#include "system/GeoClueBackend.hpp"
#include "ui/PaletteSource.hpp"
#include "ui/Theme.hpp"

namespace qypr {

class ThemeRuntime {
public:
    using ThemeChangeCallback = std::function<void(const theme::State&)>;
    using InvalidateCallback = std::function<void()>;

    explicit ThemeRuntime(EventLoop& loop);
    ~ThemeRuntime();

    ThemeRuntime(const ThemeRuntime&) = delete;
    ThemeRuntime& operator=(const ThemeRuntime&) = delete;

    // Initialise palette from config and publish the initial theme.
    void init(const Config& config, ThemeChangeCallback onThemeChanged,
              InvalidateCallback onInvalidate = nullptr);

    // Watch [theme] colors-file and colors-file-light for live matugen reloads.
    void watchPalette(const Config& config);

    // Recompute solar sunrise/sunset times using the given location fix.
    // Clears cached solar times if location is disabled or fix is invalid.
    bool refreshSolarTimes(const std::optional<GeoFix>& fix);

    // Re-read theme state from config with current palette mode and publish.
    void applyTheme(const Config& config);

    // Register a 60-second periodic timer on the event loop to check time-of-day
    // solar transitions and re-apply theme if mode flipped.
    void startMinuteTimer(const Config& config);
    void stopMinuteTimer();

    // Advance palette time (useful for deterministic unit testing).
    bool tick(int hour, const Config& config);

    const theme::State& state() const { return theme_; }
    const theme::AutoPalette& palette() const { return palette_; }
    theme::AutoPalette& palette() { return palette_; }

private:
    EventLoop& loop_;
    theme::AutoPalette palette_;
    theme::State theme_;
    ConfigWatcher paletteWatcher_{loop_};
    ConfigWatcher paletteLightWatcher_{loop_};
    ThemeChangeCallback onThemeChanged_;
    InvalidateCallback onInvalidate_;
    int minuteTimerId_ = -1;
};

}  // namespace qypr
