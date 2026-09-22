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
// overrides still win. Path resolution and format decoding live in
// PaletteSource.hpp / PaletteReader.hpp.

#pragma once

#include <string>

#include "core/Types.hpp"

namespace qypr {

class Config;

namespace theme {

// (theme::color globals deleted: values live in theme::State; see above)

// (theme::font globals deleted: values live in theme::State; see above)

// (theme::spacing globals deleted: values live in theme::State; see above)

// (theme::radius globals deleted: values live in theme::State; see above)

// (theme::anim globals deleted: values live in theme::State; see above)

// (theme::effects globals deleted: values live in theme::State; see above)

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
// (theme::style globals deleted: values live in theme::State; see above)

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
// (theme::icons::mode value deleted with the other globals.)
}  // namespace icons

// (theme::audio globals deleted: values live in theme::State; see above)

// (theme::notification globals deleted: values live in theme::State; see above)

// (theme::statusbar globals deleted: values live in theme::State; see above)

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
//
// Hosts that DERIVE values into the theme (StatusBar folds the backdrop
// alpha into panelSurfaceAlpha) override both methods around an owned State
// copy, then cascade that copy to their children.
class ThemeAware {
public:
    virtual ~ThemeAware() = default;
    virtual void setTheme(const State& state) { theme_ = &state; }
    virtual const State& theme() const { return theme_ != nullptr ? *theme_ : kDefaultState; }

private:
    const State* theme_ = nullptr;
};

// Build the theme for a Config + day/night value, purely: no global is
// touched, so tests construct expected States directly. Hosts assign the
// result into their owned State (same object, fresh contents — outstanding
// references stay valid).
//
// Pipeline: palette-file path (PaletteSource) → format decode + token map
// (PaletteReader, reusing common/render/MatugenTokens) → typed [theme]
// overrides. This function owns only defaults + overrides; palette I/O is
// not inlined here.
State loadThemeState(const Config& cfg, const AutoPalette& palette);

// Current local wall-clock hour (0–23), for the auto palette mode.
int localHourNow();

}  // namespace theme
}  // namespace qypr
