// Theme.hpp - Single source of truth for the visual design.
//
// Direct port of the QML Theme singleton (Catppuccin Mocha + glassmorphism).
// The design lives in theme::State VALUES: hosts own one (loaded from
// bar.conf via loadThemeState), widgets read it through ThemeAware::theme().
// Tests construct States directly — no shared mutable theme exists, so the
// suite is order-independent by construction.
//
// Matugen support: `[theme] colors-file` (alias `matugen`) points at a
// matugen-generated palette (Material You colours derived from the wallpaper).
// loadThemeState() applies it before the explicit colour keys, so hand-tuned
// overrides still win.

#pragma once

#include "core/Types.hpp"

namespace qypr {

class Config;

namespace theme {

namespace color {
inline Color background = Color::fromHex("#1e1e2e");
inline Color surface = Color::fromHex("#313244");
inline Color surfaceHover = Color::fromHex("#45475a");

inline Color glass = Color::fromHex("#40313244");
inline Color glassHover = Color::fromHex("#60454a5a");
inline Color glassBorder = Color::fromHex("#30cdd6f4");

inline Color primary = Color::fromHex("#89b4fa");
inline Color primaryGlow = Color::fromHex("#4089b4fa");

inline Color text = Color::fromHex("#cdd6f4");
inline Color textSubtle = Color::fromHex("#a6adc8");
inline Color textMuted = Color::fromHex("#6c7086");

inline Color error = Color::fromHex("#f38ba8");
inline Color success = Color::fromHex("#a6e3a1");
inline Color warning = Color::fromHex("#fab387");  // canonical: waylaunch peach (Stage 1.4)

// Catppuccin Mocha accents (app tiles, status dots, etc.).
inline Color blue = Color::fromHex("#89b4fa");
inline Color lavender = Color::fromHex("#b4befe");
inline Color mauve = Color::fromHex("#cba6f7");
inline Color pink = Color::fromHex("#f5c2e7");
inline Color red = Color::fromHex("#f38ba8");
inline Color peach = Color::fromHex("#fab387");
inline Color yellow = Color::fromHex("#f9e2af");
inline Color green = Color::fromHex("#a6e3a1");
inline Color teal = Color::fromHex("#94e2d5");
inline Color sky = Color::fromHex("#89dceb");
inline Color maroon = Color::fromHex("#eba0ac");
}  // namespace color

namespace font {
inline int size = 16;
inline int sizeLarge = 24;
inline int sizeClock = 96;
inline int sizeDate = 28;
inline std::string family = "Inter";
inline std::string iconFamily = "CaskaydiaCove Nerd Font";
}  // namespace font

namespace spacing {
inline int small = 8;
inline int medium = 16;
inline int large = 24;
inline int xlarge = 48;
inline int xxlarge = 80;
}  // namespace spacing

namespace radius {
inline int small = 4;
inline int medium = 8;
inline int large = 16;
inline int xlarge = 24;
inline int round = 9999;
}  // namespace radius

namespace anim {
inline int fast = 150;
inline int medium = 300;
inline int slow = 500;
inline int reveal = 600;
}  // namespace anim

namespace effects {
inline double shadowOpacity = 0.6;
inline int shadowOffset = 2;
}  // namespace effects

// Matugen palette mode: which generated palette file feeds the theme, and —
// for "auto" — whether the clock currently calls for light or dark colours.
//   dark  — always [theme] colors-file
//   light — always [theme] colors-file-light (falls back to colors-file)
//   auto  — light between sunrise and sunset, dark outside.
//
// AutoPalette is a VALUE (ARCHITECTURE_REVIEW finding 10): the bar and the
// lock screen each own one, parsed from their config and ticked by their own
// loop. No palette globals exist — tests construct values with explicit
// hours, so the suite is clock- and order-independent. The solar cache
// (setSolarTimes) is fed by the owner's GeoClue fix and survives config
// reloads (re-parsing only replaces the configured keys).
struct AutoPalette {
    std::string mode = "dark";      // configured: dark | light | auto
    std::string resolved = "dark";  // effective mode (== mode when not auto)
    int sunriseHour = 7;            // fallback: light from this hour…
    int sunsetHour = 19;            // …until this hour (local time)
    std::string location = "auto";  // solar source: auto | off
    bool useSolar = false;          // solar cache valid
    int solarSunriseMin = 0;        // minutes since local midnight
    int solarSunsetMin = 0;

    // Parse the [theme] palette keys (warns on unknown values, keeps
    // defaults) and resolve against hourNow.
    static AutoPalette fromConfig(const Config& cfg, int hourNow);
    bool isLight() const { return resolved == "light"; }
    // Effective window bounds (hours): the solar cache when location is
    // "auto" and cached, else the configured fixed hours.
    int effectiveSunriseHour() const;
    int effectiveSunsetHour() const;
    // Pure helper: "light" when sunrise <= hour < sunset, else "dark".
    static std::string resolveFor(int hour, int sunriseHour, int sunsetHour);
    // Re-resolve against hour; true on flip. No-op unless mode == "auto".
    bool tick(int hour);
    // Cache a solar sunrise/sunset window (minutes since local midnight, as
    // produced by solarTimesForDate). Out-of-range input clears the cache:
    // an invalid window must never drive the theme.
    void setSolarTimes(int sunriseMin, int sunsetMin);
    void clearSolarTimes();
};

// Style toggle: "glass" (frosted translucent + sheen + hairline) or "solid"
// (opaque card with the same hue, no translucency). Every fillGlass() call
// reads this branch at runtime, so switching between builds is a single
// bar.conf line (style = solid).
namespace style {
inline std::string mode = "glass";
}  // namespace style

// How status-bar applets render their icon.
//   Symbolic — a freedesktop `*-symbolic` icon pulled from the *active* system
//              icon theme (whatever gtk-icon-theme-name points at) and recoloured
//              to the bar text colour. Adapts to the user's theme on the fly.
//   Glyph    — the built-in Nerd Font glyph (font::iconFamily). Self-contained,
//              looks identical on every machine.
//   Auto     — Symbolic when the active theme actually provides the icon, else
//              the Nerd Font glyph. The robust default: adopts a themed install
//              yet degrades cleanly on a bare one.
namespace icons {
enum class Mode { Auto, Symbolic, Glyph };
inline Mode mode = Mode::Auto;
}  // namespace icons

namespace audio {
inline int buttonSize = 48;
inline int buttonIconSize = 20;
inline int minWidth = 320;
inline int maxWidth = 420;
inline int progressHeight = 4;
inline int volumeSliderWidth = 150;
inline int spacing = 12;
inline int panelPadding = 16;
}  // namespace audio

namespace notification {
inline int cardWidth = 360;
inline int iconSize = 36;
inline int padding = 16;
inline int gap = 12;
inline int titleSize = 14;
inline int bodySize = 13;
inline int maxVisible = 4;
inline int radius = 12;
}  // namespace notification

namespace statusbar {
inline double height = 36.0;
inline double topMargin = spacing::large;
inline double sideMargin = spacing::xlarge;
inline double cornerRadius = 12.0;
inline double iconSize = 16.0;       // Nerd Font glyph point size
inline double clockIconSize = 16.0;  // clock label size (independent of iconSize)
// Render box for themed *-symbolic icons. Independent of the bar height and of
// the glyph iconSize: symbolic SVGs carry internal padding, so they read a touch
// smaller than a glyph at the same nominal size — bump this to enlarge just the
// themed icons without touching the strip. (bar.conf: bar-symbolic-icon-size)
inline double symbolicIconSize = 18.0;
inline double iconSpacing = 18.0;
inline double padding = 14.0;
inline double separatorWidth = 1.0;
inline double qsPanelWidth = 380.0;
inline double qsTileSize = 110.0;
inline double qsTileHeight = 64.0;
inline double qsTileGap = 8.0;
inline double qsSliderHeight = 40.0;
inline double qsPadding = 16.0;
inline double qsCornerRadius = 16.0;
inline double popoverWidth = 280.0;
inline double popoverPadding = 16.0;
inline double popoverRadius = 12.0;
inline double arrowSize = 8.0;

// Menu-bar backdrop: a translucent tint over the wallpaper with a hairline
// separator along the anchored edge. The colours default to the theme's own
// background/text (so the strip follows whatever palette is configured) and are
// each overridable from the [theme] section of bar.conf. loadTheme() derives
// the tint/border defaults after the colours are resolved.
inline Color barTint = color::background;  // tint applied to the strip
inline double barTintAlpha = 0.80;         // 0..1 opacity of the tint
inline Color barBorder = color::text;      // hairline border colour
inline double barBorderAlpha = 0.08;       // 0..1 opacity of the hairline
inline bool barBorderEnabled = true;       // draw the hairline at all

// Opacity for surfaces nested inside standalone-bar overlays (QS tiles and
// popup cards). The lock-screen host leaves this at 1.0; qypr-bar updates it
// alongside barTintAlpha so the whole overlay, not only its outer shell, reads
// as one themed surface.
inline double panelSurfaceAlpha = 1.0;

inline Color panelSurface() {
    return color::surface.withAlpha(panelSurfaceAlpha);
}
inline Color panelSurfaceHover() {
    return color::surfaceHover.withAlpha(panelSurfaceAlpha);
}
inline Color panelBackground() {
    return color::background.withAlpha(panelSurfaceAlpha);
}
}  // namespace statusbar

// The whole visual design as a VALUE (ARCHITECTURE_REVIEW finding 10).
// Hosts (BarApp, Shell) own one, loaded via loadThemeState() at startup and
// reassigned on every re-theme (config reload, solar flip); widgets read it
// through ThemeAware::theme(). Assigning fresh contents into the SAME object
// keeps every outstanding reference valid, so re-themes never re-plumb.
// Member layout mirrors the old per-namespace globals one-to-one, so the
// migration is a mechanical theme::color::X -> theme().colors.X rewrite.
struct State {
    // Defaults are the loadTheme() reset values (the true runtime baseline),
    // not the older header values they replaced.
    struct Colors {
        Color background = Color::fromHex("#0d0e15");
        Color surface = Color::fromHex("#181a24");
        Color surfaceHover = Color::fromHex("#222534");
        Color glass = Color::fromHex("#181a24").withAlpha(0.65);
        Color glassHover = Color::fromHex("#222534").withAlpha(0.75);
        Color glassBorder = Color::fromHex("#ffffff").withAlpha(0.08);
        Color primary = Color::fromHex("#89b4fa");
        Color primaryGlow = Color::fromHex("#4089b4fa");
        Color text = Color::fromHex("#cdd6f4");
        Color textSubtle = Color::fromHex("#a6adc8");
        Color textMuted = Color::fromHex("#6c7086");
        Color error = Color::fromHex("#f38ba8");
        Color success = Color::fromHex("#a6e3a1");
        Color warning = Color::fromHex("#fab387");
        Color blue = Color::fromHex("#89b4fa");
        Color lavender = Color::fromHex("#b4befe");
        Color mauve = Color::fromHex("#cba6f7");
        Color pink = Color::fromHex("#f5c2e7");
        Color red = Color::fromHex("#f38ba8");
        Color peach = Color::fromHex("#fab387");
        Color yellow = Color::fromHex("#f9e2af");
        Color green = Color::fromHex("#a6e3a1");
        Color teal = Color::fromHex("#94e2d5");
        Color sky = Color::fromHex("#89dceb");
        Color maroon = Color::fromHex("#eba0ac");
    };
    struct Fonts {
        int size = 16;
        int sizeLarge = 22;
        int sizeClock = 64;
        int sizeDate = 18;
        std::string family = "Inter";
        std::string iconFamily = "CaskaydiaCove Nerd Font";
    };
    struct Spacing {
        int small = 8;
        int medium = 16;
        int large = 24;
        int xlarge = 48;
        int xxlarge = 80;
    };
    struct Radius {
        int small = 4;
        int medium = 8;
        int large = 16;
        int xlarge = 24;
        int round = 9999;
    };
    struct Anim {
        int fast = 150;
        int medium = 300;
        int slow = 500;
        int reveal = 600;
    };
    struct Effects {
        double shadowOpacity = 0.6;
        int shadowOffset = 2;
    };
    struct Style {
        std::string mode = "glass";
    };
    struct Icons {
        icons::Mode mode = icons::Mode::Auto;
    };
    struct Audio {
        int buttonSize = 48;
        int buttonIconSize = 20;
        int minWidth = 320;
        int maxWidth = 420;
        int progressHeight = 4;
        int volumeSliderWidth = 150;
        int spacing = 12;
        int panelPadding = 16;
    };
    struct Notification {
        int cardWidth = 360;
        int iconSize = 36;
        int padding = 16;
        int gap = 12;
        int titleSize = 14;
        int bodySize = 13;
        int maxVisible = 4;
        int radius = 12;
    };
    struct Statusbar {
        double height = 36.0;
        double topMargin = 24.0;
        double sideMargin = 48.0;
        double cornerRadius = 12.0;
        double iconSize = 16.0;
        double clockIconSize = 16.0;
        double symbolicIconSize = 18.0;
        double iconSpacing = 18.0;
        double padding = 14.0;
        double separatorWidth = 1.0;
        double qsPanelWidth = 380.0;
        double qsTileSize = 110.0;
        double qsTileHeight = 64.0;
        double qsTileGap = 8.0;
        double qsSliderHeight = 40.0;
        double qsPadding = 16.0;
        double qsCornerRadius = 16.0;
        double popoverWidth = 280.0;
        double popoverPadding = 16.0;
        double popoverRadius = 12.0;
        double arrowSize = 8.0;
        Color barTint = Color::fromHex("#1e1e2e");
        double barTintAlpha = 0.80;
        Color barBorder = Color::fromHex("#cdd6f4");
        double barBorderAlpha = 0.08;
        bool barBorderEnabled = true;
        double panelSurfaceAlpha = 1.0;
    };

    Colors colors;
    Fonts font;
    Spacing spacing;
    Radius radius;
    Anim anim;
    Effects effects;
    Style style;
    Icons icons;
    Audio audio;
    Notification notification;
    Statusbar statusbar;

    // Derived surfaces (were statusbar::panelSurface() etc.): the tint colors
    // at the nested-overlay alpha, so the whole overlay reads as one surface.
    Color panelSurface() const { return colors.surface.withAlpha(statusbar.panelSurfaceAlpha); }
    Color panelSurfaceHover() const {
        return colors.surfaceHover.withAlpha(statusbar.panelSurfaceAlpha);
    }
    Color panelBackground() const {
        return colors.background.withAlpha(statusbar.panelSurfaceAlpha);
    }
};

// Compiled defaults, immutable: widgets default to this until a host binds
// the live state, and tests assert against it without loading anything.
inline const State kDefaultState{};

// Host-facing mixin: every theme-reading UI object inherits this, binds the
// owner's live State once via setTheme(), and reads through theme(). The
// default is the immutable compiled default — unbound objects (previews,
// tests that don't care) render deterministically instead of crashing.
class ThemeAware {
public:
    void setTheme(const State& state) { theme_ = &state; }
    const State& theme() const { return theme_ != nullptr ? *theme_ : kDefaultState; }

private:
    const State* theme_ = nullptr;
};

// Build the theme for a Config + day/night value, purely: no global is
// touched, so tests construct expected States directly. Hosts assign the
// result into their owned State (same object, fresh contents — outstanding
// references stay valid).
State loadThemeState(const Config& cfg, const AutoPalette& palette);

// Resolve the [theme] colors-file / matugen key to a filesystem path.
// Returns "" when the key is unset or empty. `~` is expanded against $HOME
// and relative paths resolve against the loaded config file's directory
// (the same rule as the `import` directive). `lightPalette` selects the
// light-palette keys (colors-file-light / matugen-light).
std::string resolveColorsPath(const Config& cfg, bool lightPalette = false);

// Current local wall-clock hour (0–23), for the auto palette mode.
int localHourNow();

// Parse a matugen-generated palette file and apply its colour tokens onto
// the given State. Understood formats (auto-detected, may be mixed in one
// file):
//   CSS custom properties      --primary: #aabbcc;
//   GTK/GDK palette            @define-color primary #aabbcc;
//   flat JSON / CSS quoted     "primary": "#aabbcc"
//   matugen JSON scheme        "primary": { "hex": "#aabbcc", ... }
// Token names are matched case-insensitively; leading `--` is stripped.
// Returns the number of colours actually applied (0 when the file is
// missing, unreadable, or carries no recognised tokens).
int applyColorsFile(const std::string& path, State& state);

// Interim shim (deleted with the globals): feeds not-yet-migrated readers
// from a freshly built State. Migrated code reads its own State instead.
void toGlobals(const State& state);

// Legacy entry point, reimplemented on loadThemeState + toGlobals. Survives
// only until the last global reader migrates; new code takes States.
void loadTheme(const Config& cfg, AutoPalette& palette);

}  // namespace theme
}  // namespace qypr
