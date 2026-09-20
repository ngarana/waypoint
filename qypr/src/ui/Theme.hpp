// Theme.hpp - Single source of truth for the visual design.
//
// Direct port of the QML Theme singleton (Catppuccin Mocha + glassmorphism).
// Every widget reads from here so colours/spacing/timing stay consistent (DRY).
//
// All values are mutable `inline` variables with compiled-in defaults.
// User theming: call `theme::loadTheme(config)` at startup to override any
// value from the `[theme]` section of bar.conf.  Widgets continue to read
// `theme::color::primary` etc. — the indirection is invisible to callers.
//
// Matugen support: `[theme] colors-file` (alias `matugen`) points at a
// matugen-generated palette (Material You colours derived from the wallpaper).
// loadTheme() applies it before the explicit colour keys, so hand-tuned
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
//   auto  — light between sunriseHour and sunsetHour local time, dark outside;
//           BarApp re-checks once a minute and re-themes on flips.
namespace palette {
inline std::string mode = "dark";      // configured: dark | light | auto
inline std::string resolved = "dark";  // effective mode (== mode when not auto)
inline int sunriseHour = 7;            // auto: light from this hour…
inline int sunsetHour = 19;            // …until this hour (local time)
inline bool isLight() {
    return resolved == "light";
}
}  // namespace palette

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

// Read the [theme] section of a Config and override any values present.
// Call once at startup before creating widgets.  Missing keys keep defaults.
void loadTheme(const Config& cfg);

// Resolve the [theme] colors-file / matugen key to a filesystem path.
// Returns "" when the key is unset or empty. `~` is expanded against $HOME
// and relative paths resolve against the loaded config file's directory
// (the same rule as the `import` directive). `lightPalette` selects the
// light-palette keys (colors-file-light / matugen-light).
std::string resolveColorsPath(const Config& cfg, bool lightPalette = false);

// Pure helper: "light" when sunrise <= hour < sunset, else "dark". Exposed
// for the auto-palette unit tests.
std::string resolveAutoPaletteMode(int hour, int sunriseHour, int sunsetHour);

// Re-evaluate the auto palette mode against the current clock. Returns true
// when the resolved mode flipped (the caller should re-apply the theme).
// No-op (false) unless palette::mode is "auto".
bool paletteAutoTick();

// Parse a matugen-generated palette file and apply its colour tokens onto
// theme::color::*. Understood formats (auto-detected, may be mixed in one
// file):
//   CSS custom properties      --primary: #aabbcc;
//   GTK/GDK palette            @define-color primary #aabbcc;
//   flat JSON / CSS quoted     "primary": "#aabbcc"
//   matugen JSON scheme        "primary": { "hex": "#aabbcc", ... }
// Token names are matched case-insensitively; leading `--` is stripped.
// Returns the number of colours actually applied (0 when the file is
// missing, unreadable, or carries no recognised tokens).
int applyColorsFile(const std::string& path);

}  // namespace theme
}  // namespace qypr
