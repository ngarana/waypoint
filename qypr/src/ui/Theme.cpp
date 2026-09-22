// Theme.cpp - Runtime theme loading from bar.conf [theme] section.
#include "ui/Theme.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include "core/Config.hpp"
#include "render/MatugenTokens.hpp"

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

// Current local wall-clock hour (0–23), for the auto palette mode.
int localHourNow() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    return local.tm_hour;
}
}  // namespace

void loadTheme(const Config& cfg) {
    // Reset to compiled defaults before applying overrides
    color::background = Color::fromHex("#0d0e15");
    color::surface = Color::fromHex("#181a24");
    color::surfaceHover = Color::fromHex("#222534");
    color::glass = Color::fromHex("#181a24").withAlpha(0.65);
    color::glassHover = Color::fromHex("#222534").withAlpha(0.75);
    color::glassBorder = Color::fromHex("#ffffff").withAlpha(0.08);
    color::primary = Color::fromHex("#89b4fa");
    color::primaryGlow = Color::fromHex("#4089b4fa");
    color::text = Color::fromHex("#cdd6f4");
    color::textSubtle = Color::fromHex("#a6adc8");
    color::textMuted = Color::fromHex("#6c7086");
    color::error = Color::fromHex("#f38ba8");
    color::success = Color::fromHex("#a6e3a1");
    color::warning = Color::fromHex("#fab387");  // canonical: waylaunch peach (Stage 1.4)

    font::family = "Inter";
    font::iconFamily = "CaskaydiaCove Nerd Font";
    font::size = 16;
    font::sizeLarge = 22;
    font::sizeClock = 64;
    font::sizeDate = 18;

    // Menu-bar backdrop resets to compiled-in alpha/enabled defaults; the tint
    // and border *colours* are derived from the resolved palette further below
    // (after the colour overrides), so the strip follows the active theme.
    statusbar::barTintAlpha = 0.80;
    statusbar::barBorderAlpha = 0.08;
    statusbar::barBorderEnabled = true;
    statusbar::panelSurfaceAlpha = 1.0;

    // Statusbar geometry resets to compiled-in defaults.
    statusbar::height = 36.0;
    statusbar::topMargin = spacing::large;
    statusbar::sideMargin = spacing::xlarge;
    statusbar::cornerRadius = 12.0;
    statusbar::iconSize = 16.0;
    statusbar::clockIconSize = 16.0;
    statusbar::symbolicIconSize = 18.0;
    statusbar::iconSpacing = 18.0;
    statusbar::padding = 14.0;
    statusbar::separatorWidth = 1.0;
    statusbar::qsPanelWidth = 380.0;
    statusbar::qsTileSize = 110.0;
    statusbar::qsTileHeight = 64.0;
    statusbar::qsTileGap = 8.0;
    statusbar::qsSliderHeight = 40.0;
    statusbar::qsPadding = 16.0;
    statusbar::qsCornerRadius = 16.0;

    // Icon rendering resets to Auto (themed-when-available, else Nerd Font glyph).
    icons::mode = icons::Mode::Auto;

    // Effects reset to compiled-in defaults. shadowOpacity is deliberately
    // reset first: a preceding light-palette load may have disabled the shadow
    // (0.0) — a later dark load must restore it unless an explicit key is set.
    effects::shadowOpacity = 0.6;
    effects::shadowOffset = 2;

    // ─── Palette mode ────────────────────────────────────────────────
    constexpr const char* kS = "theme";
    // dark | light | auto. Resolved here (auto follows the solar window when
    // a GeoClue fix is cached, else the fixed hours below) and re-resolved
    // by paletteAutoTick() while the bar runs. The solar cache is NOT reset
    // here — the fix outlives config reloads; BarApp refreshes it.
    palette::mode = "dark";
    palette::resolved = "dark";
    palette::sunriseHour = 7;
    palette::sunsetHour = 19;
    palette::location = "auto";
    if (cfg.has(kS, "palette-mode")) {
        const std::string m = cfg.getString(kS, "palette-mode", "dark");
        if (m == "light" || m == "dark" || m == "auto") {
            palette::mode = m;
        } else {
            std::fprintf(stderr, "qypr: theme: unknown palette-mode '%s' (want dark|light|auto)\n",
                         m.c_str());
        }
    }
    if (cfg.has(kS, "palette-location")) {
        const std::string loc = cfg.getString(kS, "palette-location", "auto");
        if (loc == "auto" || loc == "off") {
            palette::location = loc;
        } else {
            std::fprintf(stderr, "qypr: theme: unknown palette-location '%s' (want auto|off)\n",
                         loc.c_str());
        }
    }
    overrideInt(palette::sunriseHour, cfg, kS, "palette-sunrise");
    overrideInt(palette::sunsetHour, cfg, kS, "palette-sunset");
    palette::resolved =
        palette::mode == "auto"
            ? resolveAutoPaletteMode(localHourNow(), effectiveSunriseHour(), effectiveSunsetHour())
            : palette::mode;

    // ─── Matugen palette ─────────────────────────────────────────────
    // Apply the matugen-generated Material You palette (if configured) for the
    // resolved mode *before* the explicit [theme] keys below, so hand-tuned
    // colours still win. A palette that names only some tokens leaves the rest
    // at the compiled defaults, so partial outputs degrade gracefully. A light
    // palette falls back to the dark file when no light file is configured.
    std::string palettePath = resolveColorsPath(cfg, palette::isLight());
    if (palettePath.empty() && palette::isLight()) { palettePath = resolveColorsPath(cfg, false); }
    if (!palettePath.empty()) { applyColorsFile(palettePath); }

    // ─── Colors ──────────────────────────────────────────────────────
    overrideColor(color::background, cfg, kS, "background");
    overrideColor(color::surface, cfg, kS, "surface");
    overrideColor(color::surfaceHover, cfg, kS, "surface-hover");
    overrideColor(color::glass, cfg, kS, "glass");
    overrideColor(color::glassHover, cfg, kS, "glass-hover");
    overrideColor(color::glassBorder, cfg, kS, "glass-border");
    overrideColor(color::primary, cfg, kS, "primary");
    overrideColor(color::primaryGlow, cfg, kS, "primary-glow");
    overrideColor(color::text, cfg, kS, "text");
    overrideColor(color::textSubtle, cfg, kS, "text-subtle");
    overrideColor(color::textMuted, cfg, kS, "text-muted");
    overrideColor(color::error, cfg, kS, "error");
    overrideColor(color::success, cfg, kS, "success");
    overrideColor(color::warning, cfg, kS, "warning");

    // ─── Fonts ───────────────────────────────────────────────────────
    if (cfg.has(kS, "font-family")) { font::family = cfg.getString(kS, "font-family", ""); }
    if (cfg.has(kS, "icon-family")) { font::iconFamily = cfg.getString(kS, "icon-family", ""); }
    overrideInt(font::size, cfg, kS, "font-size");
    overrideInt(font::sizeLarge, cfg, kS, "font-size-large");
    overrideInt(font::sizeClock, cfg, kS, "font-size-clock");
    overrideInt(font::sizeDate, cfg, kS, "font-size-date");

    // ─── Spacing ─────────────────────────────────────────────────────
    overrideInt(spacing::small, cfg, kS, "spacing-small");
    overrideInt(spacing::medium, cfg, kS, "spacing-medium");
    overrideInt(spacing::large, cfg, kS, "spacing-large");
    overrideInt(spacing::xlarge, cfg, kS, "spacing-xlarge");

    // ─── Radius ──────────────────────────────────────────────────────
    overrideInt(radius::small, cfg, kS, "radius-small");
    overrideInt(radius::medium, cfg, kS, "radius-medium");
    overrideInt(radius::large, cfg, kS, "radius-large");

    // ─── Animation ───────────────────────────────────────────────────
    overrideInt(anim::fast, cfg, kS, "anim-fast");
    overrideInt(anim::medium, cfg, kS, "anim-medium");
    overrideInt(anim::slow, cfg, kS, "anim-slow");
    overrideInt(anim::reveal, cfg, kS, "anim-reveal");

    // ─── Style preset ───────────────────────────────────────────────
    // `style` selects popover rendering only:
    //   "glass"  — frosted translucent cards (default).
    //   "solid"  — opaque cards, same hue, no translucency.
    // The menu-bar backdrop (tint + hairline) is a separate, always-available
    // feature driven by the bar-tint/bar-border keys below; it is no longer tied
    // to a preset. "macos" is kept as a backward-compatible alias for "glass" so
    // an existing config keeps working.
    if (cfg.has(kS, "style")) { style::mode = cfg.getString(kS, "style", "glass"); }
    if (style::mode == "macos") { style::mode = "glass"; }

    // ─── Icon style ─────────────────────────────────────────────────
    // auto (default) | symbolic | glyph. See theme::icons::Mode.
    if (cfg.has(kS, "icon-style")) {
        const std::string m = cfg.getString(kS, "icon-style", "auto");
        if (m == "symbolic") {
            icons::mode = icons::Mode::Symbolic;
        } else if (m == "glyph" || m == "nerd" || m == "font") {
            icons::mode = icons::Mode::Glyph;
        } else {
            icons::mode = icons::Mode::Auto;
        }
    }

    // ─── Effects ─────────────────────────────────────────────────────
    overrideDouble(effects::shadowOpacity, cfg, kS, "shadow-opacity");
    overrideInt(effects::shadowOffset, cfg, kS, "shadow-offset");
    // A light palette puts dark text on a bright surface, where a black offset
    // shadow would read as a ghost shade behind every glyph. Disable the drop
    // shadow unless the user set an explicit opacity, which always wins.
    if (palette::isLight() && !cfg.has(kS, "shadow-opacity")) { effects::shadowOpacity = 0.0; }

    // ─── Statusbar ───────────────────────────────────────────────────
    overrideDouble(statusbar::height, cfg, kS, "bar-height");
    overrideDouble(statusbar::iconSize, cfg, kS, "bar-icon-size");
    statusbar::symbolicIconSize = 18.0;
    overrideDouble(statusbar::symbolicIconSize, cfg, kS, "bar-symbolic-icon-size");
    statusbar::clockIconSize = statusbar::iconSize;
    overrideDouble(statusbar::clockIconSize, cfg, kS, "bar-clock-icon-size");
    overrideDouble(statusbar::iconSpacing, cfg, kS, "bar-icon-spacing");
    overrideDouble(statusbar::padding, cfg, kS, "bar-padding");
    overrideDouble(statusbar::cornerRadius, cfg, kS, "bar-corner-radius");
    overrideDouble(statusbar::popoverRadius, cfg, kS, "popover-radius");

    // Menu-bar backdrop: translucent tint + hairline border. The tint and border
    // colours default to the resolved theme palette (background/text) so the strip
    // follows whatever colours are configured; each can be overridden explicitly.
    statusbar::barTint = color::background;
    statusbar::barBorder = color::text;
    overrideColor(statusbar::barTint, cfg, kS, "bar-tint");
    overrideDouble(statusbar::barTintAlpha, cfg, kS, "bar-tint-alpha");
    overrideColor(statusbar::barBorder, cfg, kS, "bar-border-color");
    overrideDouble(statusbar::barBorderAlpha, cfg, kS, "bar-border-alpha");
    statusbar::barBorderEnabled = cfg.getBool(kS, "bar-border", statusbar::barBorderEnabled);

    // ─── Notification ────────────────────────────────────────────────
    overrideInt(notification::cardWidth, cfg, kS, "notification-card-width");
    overrideInt(notification::iconSize, cfg, kS, "notification-icon-size");
    overrideInt(notification::padding, cfg, kS, "notification-padding");
    overrideInt(notification::gap, cfg, kS, "notification-gap");
    overrideInt(notification::titleSize, cfg, kS, "notification-title-size");
    overrideInt(notification::bodySize, cfg, kS, "notification-body-size");
    overrideInt(notification::radius, cfg, kS, "notification-radius");

    // ─── Audio ───────────────────────────────────────────────────────
    overrideInt(audio::buttonSize, cfg, kS, "audio-button-size");
    overrideInt(audio::buttonIconSize, cfg, kS, "audio-button-icon-size");
    overrideInt(audio::minWidth, cfg, kS, "audio-min-width");
    overrideInt(audio::maxWidth, cfg, kS, "audio-max-width");
    overrideInt(audio::progressHeight, cfg, kS, "audio-progress-height");
    overrideInt(audio::volumeSliderWidth, cfg, kS, "audio-volume-slider-width");
}

// -----------------------------------------------------------------------------
// Matugen (Material You) palette support
//
// matugen (github.com/InioX/matugen) derives a Material Design 3 colour scheme
// from the wallpaper and renders it through user templates. Rather than
// shipping our own template, we read whichever file the user already points
// matugen at and map the well-known M3 token names onto the bar's palette.
// -----------------------------------------------------------------------------

namespace {

// Pull every `token -> #hex` pair out of a palette file's text. Handles the
// formats matugen templates actually produce (they may be mixed in one file):
//   "token": { "hex": "#aabbcc", ... }   matugen's JSON scheme format
//   --token: #aabbcc;                    CSS custom properties
//   @define-color token #aabbcc;         GTK/GDK palette files
//   "token": "#aabbcc"                   flat JSON / quoted CSS values
//   token: #aabbcc;                      plain CSS declarations
// First occurrence of a token wins; names are lower-cased, `--` stripped.
std::map<std::string, std::string> parsePalette(const std::string& content) {
    // #RRGGBB or #AARRGGBB (what Color::fromHex understands).
    static const std::string kHex = "(#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?)";

    std::map<std::string, std::string> tokens;
    const auto record = [&tokens](std::string name, const std::string& hex) {
        if (name == "hex") {
            return;  // noise from the nested-JSON format
        }
        while (!name.empty() && name.front() == '-') { name.erase(0, 1); }
        for (char& c : name) {
            if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c + 32); }
            if (c == '_') {
                c = '-';  // matugen emits on_surface / surface_container_high
            }
        }
        if (!name.empty()) {
            tokens.emplace(name, hex);  // first occurrence wins
        }
    };

    // 1. Nested matugen JSON scheme: "token": { ..., "hex": "#..." }.
    static const std::regex kNested(R"rx("([A-Za-z0-9_-]+)"\s*:\s*\{[^{}]*?"hex"\s*:\s*")rx" +
                                        kHex + "\"",
                                    std::regex::optimize);
    // 2. @define-color, CSS custom properties, and quoted flat JSON values.
    static const std::regex kCss(R"rx(@define-color\s+([A-Za-z0-9_-]+)\s+)rx" + kHex +
                                     R"rx(|--([A-Za-z0-9_-]+)\s*:\s*)rx" + kHex +
                                     R"rx(|"([A-Za-z0-9_-]+)"\s*:\s*")rx" + kHex + "\"",
                                 std::regex::optimize);
    // 3. Plain unquoted CSS declarations: `background: #112233;`
    static const std::regex kPlain("([A-Za-z0-9_-]+)\\s*:\\s*" + kHex, std::regex::optimize);

    for (std::sregex_iterator it(content.begin(), content.end(), kNested), last; it != last; ++it) {
        record((*it)[1].str(), (*it)[2].str());
    }
    for (std::sregex_iterator it(content.begin(), content.end(), kCss), last; it != last; ++it) {
        const std::smatch& m = *it;
        if (m[1].matched) { record(m[1].str(), m[2].str()); }
        if (m[3].matched) { record(m[3].str(), m[4].str()); }
        if (m[5].matched) { record(m[5].str(), m[6].str()); }
    }
    for (std::sregex_iterator it(content.begin(), content.end(), kPlain), last; it != last; ++it) {
        record((*it)[1].str(), (*it)[2].str());
    }
    return tokens;
}

}  // namespace

namespace {

// Material You token -> bar colour. Each entry tries its candidate tokens in
// order and keeps the current value when none are present, so narrow custom
// templates (a few `--background`/`--primary` lines) still theme the bar.
struct PaletteMapping {
    Color* target;
    std::vector<const char*> tokens;
    // >= 0: alpha applied when the matched hex carries none of its own
    // (#RRGGBB). < 0: keep whatever alpha the hex provided (default 1.0).
    double alpha = -1.0;
};

const std::vector<PaletteMapping>& paletteMappings() {
    static const std::vector<PaletteMapping> kMappings = {
        // Core surfaces & text. `background` prefers a dedicated background
        // token, then falls back to the M3 `surface` tone.
        {.target = &color::background,
         .tokens = {"background", "surface", "surface-container-lowest"}},
        {.target = &color::surface,
         .tokens = {"surface-container", "surface-container-low", "secondary-container",
                    "surface"}},
        {.target = &color::surfaceHover,
         .tokens = {"surface-container-high", "surface-container-highest", "surface-container"}},
        {.target = &color::primary, .tokens = {"primary", "primary-fixed", "primary-container"}},
        // The glow is a translucent primary unless the template ships its own
        // 8-digit (AARRGGBB) value.
        {.target = &color::primaryGlow, .tokens = {"primary-glow", "primary"}, .alpha = 0.25},
        {.target = &color::text,
         .tokens = {"on-surface", "foreground", "on-primary-container", "text"}},
        {.target = &color::textSubtle,
         .tokens = {"on-surface-variant", "outline", "text-secondary"}},
        {.target = &color::textMuted, .tokens = {"outline", "outline-variant", "text-muted"}},
        {.target = &color::error, .tokens = {"error"}},
        // M3 has no success/warning hues; tertiary (and secondary for a
        // warning-ish tone) are the usual stand-ins. `success`/`warning`
        // custom keys win when a template defines them.
        {.target = &color::success, .tokens = {"success", "tertiary"}},
        {.target = &color::warning, .tokens = {"warning", "secondary"}},
        // Accent strip (app tiles, status dots, pager chips). Matugen palettes
        // are effectively two-accent, so accents fan out from the M3 roles.
        {.target = &color::blue, .tokens = {"blue", "primary"}},
        {.target = &color::lavender, .tokens = {"lavender", "primary-container"}},
        {.target = &color::mauve, .tokens = {"mauve", "tertiary"}},
        {.target = &color::pink, .tokens = {"pink", "tertiary"}},
        {.target = &color::red, .tokens = {"red", "error"}},
        {.target = &color::peach, .tokens = {"peach", "secondary"}},
        {.target = &color::yellow, .tokens = {"yellow", "warning"}},
        {.target = &color::green, .tokens = {"green", "success"}},
        {.target = &color::teal, .tokens = {"teal", "secondary-container"}},
        {.target = &color::sky, .tokens = {"sky", "secondary"}},
        {.target = &color::maroon, .tokens = {"maroon", "error-container"}},
    };
    return kMappings;
}

}  // namespace

std::string resolveColorsPath(const Config& cfg, bool lightPalette) {
    std::string raw = lightPalette ? cfg.getString("theme", "colors-file-light", "") : "";
    if (raw.empty()) {
        raw = cfg.getString("theme", lightPalette ? "matugen-light" : "colors-file", "");
    }
    if (!lightPalette && raw.empty()) { raw = cfg.getString("theme", "matugen", ""); }
    // Tolerate quoted values.
    while (!raw.empty() && (raw.front() == '"' || raw.front() == '\'')) { raw.erase(0, 1); }
    while (!raw.empty() && (raw.back() == '"' || raw.back() == '\'')) { raw.pop_back(); }
    if (raw.empty()) { return ""; }

    if (raw.front() == '~') {
        const char* home = std::getenv("HOME");
        if ((home == nullptr) || ((*home) == 0)) {
            return "";  // nowhere to expand against
        }
        raw = std::string(home) + raw.substr(1);
    }

    if (raw.front() != '/') {
        // Relative paths follow `import` semantics: resolved against the
        // directory of the loaded config file.
        std::filesystem::path base = std::filesystem::path(cfg.path()).parent_path();
        if (base.empty()) { base = std::filesystem::path(Config::configDir()); }
        raw = (base / raw).string();
    }
    return raw;
}

int applyColorsFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "qypr: theme: colours file not readable: %s\n", path.c_str());
        return 0;
    }
    const std::string content((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    const auto tokens = parsePalette(content);

    int applied = 0;
    for (const auto& m : paletteMappings()) {
        // Candidates are in priority order — shared first-present-non-empty
        // lookup (common/render/MatugenTokens).
        const std::string hit = pickMatugenToken(tokens, m.tokens);
        if (!hit.empty()) {
            Color c = Color::fromHex(hit);
            if (m.alpha >= 0.0 && hit.size() == 7) {
                c.a = m.alpha;  // no alpha in hex
            }
            *m.target = c;
            ++applied;
        }
    }

    if (applied > 0) {
        std::fprintf(stderr, "qypr: theme: matugen palette applied (%d colours from %s)\n", applied,
                     path.c_str());
    } else {
        std::fprintf(stderr, "qypr: theme: no known colour tokens in %s\n", path.c_str());
    }
    return applied;
}

std::string resolveAutoPaletteMode(int hour, int sunriseHour, int sunsetHour) {
    return (hour >= sunriseHour && hour < sunsetHour) ? "light" : "dark";
}

void setSolarTimes(int sunriseMin, int sunsetMin) {
    if (sunriseMin < 0 || sunriseMin >= 1440 || sunsetMin < 0 || sunsetMin >= 1440) {
        clearSolarTimes();
        return;
    }
    palette::useSolar = true;
    palette::solarSunriseMin = sunriseMin;
    palette::solarSunsetMin = sunsetMin;
}

void clearSolarTimes() {
    palette::useSolar = false;
    palette::solarSunriseMin = 0;
    palette::solarSunsetMin = 0;
}

int effectiveSunriseHour() {
    if (palette::location == "auto" && palette::useSolar) { return palette::solarSunriseMin / 60; }
    return palette::sunriseHour;
}

int effectiveSunsetHour() {
    if (palette::location == "auto" && palette::useSolar) { return palette::solarSunsetMin / 60; }
    return palette::sunsetHour;
}

bool paletteAutoTick() {
    if (palette::mode != "auto") { return false; }
    const std::string want =
        resolveAutoPaletteMode(localHourNow(), effectiveSunriseHour(), effectiveSunsetHour());
    if (want == palette::resolved) { return false; }
    palette::resolved = want;
    std::fprintf(stderr, "qypr: theme: auto palette switched to %s\n", want.c_str());
    return true;
}

}  // namespace qypr::theme
