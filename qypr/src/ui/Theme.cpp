// Theme.cpp - Runtime theme loading from bar.conf [theme] section.
//
// Palette-file path resolution, format decoding, and token→colour mapping
// live in PaletteSource / PaletteReader (QYPR_DECOMPOSITION_PLAN step 4).
// This TU owns only: compiled defaults, typed [theme] overrides, and the
// AutoPalette day/night value.
#include "ui/Theme.hpp"

#include <cstdio>
#include <ctime>
#include <string>

#include "core/Config.hpp"
#include "ui/PaletteReader.hpp"
#include "ui/PaletteSource.hpp"

namespace qypr::theme {

namespace {
// Override a Color from config if the key exists.
void overrideColor(Color& target, const Config& cfg, const char* section, const char* key) {
    if (cfg.has(section, key)) { target = Color::fromHex(cfg.getString(section, key, "")); }
}

// Override an int from config if the key exists.
void overrideInt(int& target, const Config& cfg, const char* section, const char* key) {
    if (cfg.has(section, key)) { target = cfg.getInt(section, key, target); }
}

// Override a double from config if the key exists.
void overrideDouble(double& target, const Config& cfg, const char* section, const char* key) {
    if (cfg.has(section, key)) { target = cfg.getDouble(section, key, target); }
}

}  // namespace

// Current local wall-clock hour (0–23), for the auto palette mode.
int localHourNow() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    return local.tm_hour;
}

State loadThemeState(const Config& cfg, const AutoPalette& palette) {
    State state;
    // Reset to compiled defaults before applying overrides
    state.colors.background = Color::fromHex("#0d0e15");
    state.colors.surface = Color::fromHex("#181a24");
    state.colors.surfaceHover = Color::fromHex("#222534");
    state.colors.glass = Color::fromHex("#181a24").withAlpha(0.65);
    state.colors.glassHover = Color::fromHex("#222534").withAlpha(0.75);
    state.colors.glassBorder = Color::fromHex("#ffffff").withAlpha(0.08);
    state.colors.primary = Color::fromHex("#89b4fa");
    state.colors.primaryGlow = Color::fromHex("#4089b4fa");
    state.colors.text = Color::fromHex("#cdd6f4");
    state.colors.textSubtle = Color::fromHex("#a6adc8");
    state.colors.textMuted = Color::fromHex("#6c7086");
    state.colors.error = Color::fromHex("#f38ba8");
    state.colors.success = Color::fromHex("#a6e3a1");
    state.colors.warning = Color::fromHex("#fab387");  // canonical: waylaunch peach (Stage 1.4)

    state.font.family = "Inter";
    state.font.iconFamily = "CaskaydiaCove Nerd Font";
    state.font.size = 16;
    state.font.sizeLarge = 22;
    state.font.sizeClock = 96;
    state.font.sizeDate = 18;

    // Menu-bar backdrop resets to compiled-in alpha/enabled defaults; the tint
    // and border *colours* are derived from the resolved palette further below
    // (after the colour overrides), so the strip follows the active theme.
    state.statusbar.barTintAlpha = 0.80;
    state.statusbar.barBorderAlpha = 0.08;
    state.statusbar.barBorderEnabled = true;
    state.statusbar.panelSurfaceAlpha = 1.0;

    // Statusbar geometry resets to compiled-in defaults.
    state.statusbar.height = 36.0;
    state.statusbar.topMargin = state.spacing.large;
    state.statusbar.sideMargin = state.spacing.xlarge;
    state.statusbar.cornerRadius = 12.0;
    state.statusbar.iconSize = 16.0;
    state.statusbar.clockIconSize = 16.0;
    state.statusbar.symbolicIconSize = 18.0;
    state.statusbar.iconSpacing = 18.0;
    state.statusbar.padding = 14.0;
    state.statusbar.separatorWidth = 1.0;
    state.statusbar.qsPanelWidth = 380.0;
    state.statusbar.qsTileSize = 110.0;
    state.statusbar.qsTileHeight = 64.0;
    state.statusbar.qsTileGap = 8.0;
    state.statusbar.qsSliderHeight = 40.0;
    state.statusbar.qsPadding = 16.0;
    state.statusbar.qsCornerRadius = 16.0;

    // Icon rendering resets to Auto (themed-when-available, else Nerd Font glyph).
    state.icons.mode = icons::Mode::Auto;

    // Effects reset to compiled-in defaults. shadowOpacity is deliberately
    // reset first: a preceding light-palette load may have disabled the shadow
    // (0.0) — a later dark load must restore it unless an explicit key is set.
    state.effects.shadowOpacity = 0.6;
    state.effects.shadowOffset = 2;

    // ─── Palette mode ────────────────────────────────────────────────
    // The owner's AutoPalette arrives parsed and resolved (re-parse it from
    // the config when the mode keys may have changed, then tick it after
    // refreshing the solar cache). loadTheme only READS it here.
    constexpr const char* kS = "theme";

    // ─── Matugen palette ─────────────────────────────────────────────
    // Apply the matugen-generated Material You palette (if configured) for the
    // resolved mode *before* the explicit [theme] keys below, so hand-tuned
    // colours still win. A palette that names only some tokens leaves the rest
    // at the compiled defaults, so partial outputs degrade gracefully. A light
    // palette falls back to the dark file when no light file is configured.
    std::string palettePath = resolveColorsPath(cfg, palette.isLight());
    if (palettePath.empty() && palette.isLight()) { palettePath = resolveColorsPath(cfg, false); }
    if (!palettePath.empty()) { applyColorsFile(palettePath, state); }

    // These are derived surfaces, not independent palette roles. Rebuild them
    // after Matugen has supplied the core roles so every glass card, slider
    // popup, and lock overlay follows the active palette. Explicit [theme]
    // glass/glass-hover/glass-border keys below still take precedence.
    state.colors.glass = state.colors.surface.withAlpha(0.65);
    state.colors.glassHover = state.colors.surfaceHover.withAlpha(0.75);
    state.colors.glassBorder = state.colors.text.withAlpha(0.08);

    // ─── Colors ──────────────────────────────────────────────────────
    overrideColor(state.colors.background, cfg, kS, "background");
    overrideColor(state.colors.surface, cfg, kS, "surface");
    overrideColor(state.colors.surfaceHover, cfg, kS, "surface-hover");
    overrideColor(state.colors.glass, cfg, kS, "glass");
    overrideColor(state.colors.glassHover, cfg, kS, "glass-hover");
    overrideColor(state.colors.glassBorder, cfg, kS, "glass-border");
    overrideColor(state.colors.primary, cfg, kS, "primary");
    overrideColor(state.colors.primaryGlow, cfg, kS, "primary-glow");
    overrideColor(state.colors.text, cfg, kS, "text");
    overrideColor(state.colors.textSubtle, cfg, kS, "text-subtle");
    overrideColor(state.colors.textMuted, cfg, kS, "text-muted");
    overrideColor(state.colors.error, cfg, kS, "error");
    overrideColor(state.colors.success, cfg, kS, "success");
    overrideColor(state.colors.warning, cfg, kS, "warning");

    // ─── Fonts ───────────────────────────────────────────────────────
    if (cfg.has(kS, "font-family")) { state.font.family = cfg.getString(kS, "font-family", ""); }
    if (cfg.has(kS, "icon-family")) {
        state.font.iconFamily = cfg.getString(kS, "icon-family", "");
    }
    overrideInt(state.font.size, cfg, kS, "font-size");
    overrideInt(state.font.sizeLarge, cfg, kS, "font-size-large");
    overrideInt(state.font.sizeClock, cfg, kS, "font-size-clock");
    overrideInt(state.font.sizeDate, cfg, kS, "font-size-date");

    // ─── Spacing ─────────────────────────────────────────────────────
    overrideInt(state.spacing.small, cfg, kS, "spacing-small");
    overrideInt(state.spacing.medium, cfg, kS, "spacing-medium");
    overrideInt(state.spacing.large, cfg, kS, "spacing-large");
    overrideInt(state.spacing.xlarge, cfg, kS, "spacing-xlarge");

    // ─── Radius ──────────────────────────────────────────────────────
    overrideInt(state.radius.small, cfg, kS, "radius-small");
    overrideInt(state.radius.medium, cfg, kS, "radius-medium");
    overrideInt(state.radius.large, cfg, kS, "radius-large");

    // ─── Animation ───────────────────────────────────────────────────
    overrideInt(state.anim.fast, cfg, kS, "anim-fast");
    overrideInt(state.anim.medium, cfg, kS, "anim-medium");
    overrideInt(state.anim.slow, cfg, kS, "anim-slow");
    overrideInt(state.anim.reveal, cfg, kS, "anim-reveal");

    // ─── Style preset ───────────────────────────────────────────────
    // `style` selects popover rendering only:
    //   "glass"  — frosted translucent cards (default).
    //   "solid"  — opaque cards, same hue, no translucency.
    // The menu-bar backdrop (tint + hairline) is a separate, always-available
    // feature driven by the bar-tint/bar-border keys below; it is no longer tied
    // to a preset. "macos" is kept as a backward-compatible alias for "glass" so
    // an existing config keeps working.
    if (cfg.has(kS, "style")) { state.style.mode = cfg.getString(kS, "style", "glass"); }
    if (state.style.mode == "macos") { state.style.mode = "glass"; }

    // ─── Icon style ─────────────────────────────────────────────────
    // auto (default) | symbolic | glyph. See theme::icons::Mode.
    if (cfg.has(kS, "icon-style")) {
        const std::string m = cfg.getString(kS, "icon-style", "auto");
        if (m == "symbolic") {
            state.icons.mode = icons::Mode::Symbolic;
        } else if (m == "glyph" || m == "nerd" || m == "font") {
            state.icons.mode = icons::Mode::Glyph;
        } else {
            state.icons.mode = icons::Mode::Auto;
        }
    }

    // ─── Effects ─────────────────────────────────────────────────────
    overrideDouble(state.effects.shadowOpacity, cfg, kS, "shadow-opacity");
    overrideInt(state.effects.shadowOffset, cfg, kS, "shadow-offset");
    // A light palette puts dark text on a bright surface, where a black offset
    // shadow would read as a ghost shade behind every glyph. Disable the drop
    // shadow unless the user set an explicit opacity, which always wins.
    if (palette.isLight() && !cfg.has(kS, "shadow-opacity")) { state.effects.shadowOpacity = 0.0; }

    // ─── Statusbar ───────────────────────────────────────────────────
    overrideDouble(state.statusbar.height, cfg, kS, "bar-height");
    overrideDouble(state.statusbar.iconSize, cfg, kS, "bar-icon-size");
    state.statusbar.symbolicIconSize = 18.0;
    overrideDouble(state.statusbar.symbolicIconSize, cfg, kS, "bar-symbolic-icon-size");
    state.statusbar.clockIconSize = state.statusbar.iconSize;
    overrideDouble(state.statusbar.clockIconSize, cfg, kS, "bar-clock-icon-size");
    overrideDouble(state.statusbar.iconSpacing, cfg, kS, "bar-icon-spacing");
    overrideDouble(state.statusbar.padding, cfg, kS, "bar-padding");
    overrideDouble(state.statusbar.cornerRadius, cfg, kS, "bar-corner-radius");
    overrideDouble(state.statusbar.popoverRadius, cfg, kS, "popover-radius");

    // Menu-bar backdrop: translucent tint + hairline border. The tint and border
    // colours default to the resolved theme palette (background/text) so the strip
    // follows whatever colours are configured; each can be overridden explicitly.
    state.statusbar.barTint = state.colors.background;
    state.statusbar.barBorder = state.colors.text;
    overrideColor(state.statusbar.barTint, cfg, kS, "bar-tint");
    overrideDouble(state.statusbar.barTintAlpha, cfg, kS, "bar-tint-alpha");
    overrideColor(state.statusbar.barBorder, cfg, kS, "bar-border-color");
    overrideDouble(state.statusbar.barBorderAlpha, cfg, kS, "bar-border-alpha");
    state.statusbar.barBorderEnabled =
        cfg.getBool(kS, "bar-border", state.statusbar.barBorderEnabled);

    // ─── Notification ────────────────────────────────────────────────
    overrideInt(state.notification.cardWidth, cfg, kS, "notification-card-width");
    overrideInt(state.notification.iconSize, cfg, kS, "notification-icon-size");
    overrideInt(state.notification.padding, cfg, kS, "notification-padding");
    overrideInt(state.notification.gap, cfg, kS, "notification-gap");
    overrideInt(state.notification.titleSize, cfg, kS, "notification-title-size");
    overrideInt(state.notification.bodySize, cfg, kS, "notification-body-size");
    overrideInt(state.notification.radius, cfg, kS, "notification-radius");

    // ─── Audio ───────────────────────────────────────────────────────
    overrideInt(state.audio.buttonSize, cfg, kS, "audio-button-size");
    overrideInt(state.audio.buttonIconSize, cfg, kS, "audio-button-icon-size");
    overrideInt(state.audio.minWidth, cfg, kS, "audio-min-width");
    overrideInt(state.audio.maxWidth, cfg, kS, "audio-max-width");
    overrideInt(state.audio.progressHeight, cfg, kS, "audio-progress-height");
    overrideInt(state.audio.volumeSliderWidth, cfg, kS, "audio-volume-slider-width");
    return state;
}

AutoPalette AutoPalette::fromConfig(const Config& cfg, int hourNow) {
    constexpr const char* kS = "theme";
    AutoPalette palette;
    if (cfg.has(kS, "palette-mode")) {
        const std::string m = cfg.getString(kS, "palette-mode", "dark");
        if (m == "light" || m == "dark" || m == "auto") {
            palette.mode = m;
        } else {
            std::fprintf(stderr, "qypr: theme: unknown palette-mode '%s' (want dark|light|auto)\n",
                         m.c_str());
        }
    }
    if (cfg.has(kS, "palette-location")) {
        const std::string loc = cfg.getString(kS, "palette-location", "auto");
        if (loc == "auto" || loc == "off") {
            palette.location = loc;
        } else {
            std::fprintf(stderr, "qypr: theme: unknown palette-location '%s' (want auto|off)\n",
                         loc.c_str());
        }
    }
    overrideInt(palette.sunriseHour, cfg, kS, "palette-sunrise");
    overrideInt(palette.sunsetHour, cfg, kS, "palette-sunset");
    palette.resolved = palette.mode == "auto" ? resolveFor(hourNow, palette.effectiveSunriseHour(),
                                                           palette.effectiveSunsetHour())
                                              : palette.mode;
    return palette;
}

std::string AutoPalette::resolveFor(int hour, int sunriseHour, int sunsetHour) {
    return (hour >= sunriseHour && hour < sunsetHour) ? "light" : "dark";
}

void AutoPalette::setSolarTimes(int sunriseMin, int sunsetMin) {
    if (sunriseMin < 0 || sunriseMin >= 1440 || sunsetMin < 0 || sunsetMin >= 1440) {
        clearSolarTimes();
        return;
    }
    useSolar = true;
    solarSunriseMin = sunriseMin;
    solarSunsetMin = sunsetMin;
}

void AutoPalette::clearSolarTimes() {
    useSolar = false;
    solarSunriseMin = 0;
    solarSunsetMin = 0;
}

int AutoPalette::effectiveSunriseHour() const {
    if (location == "auto" && useSolar) { return solarSunriseMin / 60; }
    return sunriseHour;
}

int AutoPalette::effectiveSunsetHour() const {
    if (location == "auto" && useSolar) { return solarSunsetMin / 60; }
    return sunsetHour;
}

bool AutoPalette::tick(int hour) {
    if (mode != "auto") { return false; }
    const std::string want = resolveFor(hour, effectiveSunriseHour(), effectiveSunsetHour());
    if (want == resolved) { return false; }
    resolved = want;
    std::fprintf(stderr, "qypr: theme: auto palette switched to %s\n", want.c_str());
    return true;
}

}  // namespace qypr::theme
