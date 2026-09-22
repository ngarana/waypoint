// test_theme.cpp - Theme loading, palettes, matugen sources.
// Split verbatim from tests/unit_tests.cpp; bodies unchanged.
#include "test_framework.hpp"

#include "core/SolarCalc.hpp"
#include "ui/PaletteReader.hpp"
#include "ui/PaletteSource.hpp"

// Apply a config with a deterministic noon resolution; returns the owned
// design value. Tests assert on the returned State — never on shared
// globals — so they are order-independent.
qypr::theme::State applyTheme(qypr::Config& cfg) {
    qypr::theme::AutoPalette pal = qypr::theme::AutoPalette::fromConfig(cfg, 12);
    return qypr::theme::loadThemeState(cfg, pal);
}

TEST(ThemeLoadThemeDefaults) {
    qypr::Config c;
    c.load("/nonexistent");
    qypr::theme::State stC = applyTheme(c);
    EXPECT_EQ(stC.font.family, std::string("Inter"));
    EXPECT_EQ(stC.font.iconFamily, std::string("CaskaydiaCove Nerd Font"));
    EXPECT_EQ(stC.font.size, 16);
    EXPECT_NEAR(stC.colors.primary.r, 0.537, 0.01);
    EXPECT_NEAR(stC.statusbar.height, 36.0, 0.01);
    // Icons default to Auto (themed-when-available, else glyph).
    EXPECT_TRUE(stC.icons.mode == qypr::theme::icons::Mode::Auto);
}

// The bar backdrop tint/border colours follow the active palette (no hardcoded
// macOS values), and icon-style + the legacy `macos` alias parse correctly.
TEST(ThemeFollowsSystemPalette) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded test binary.
    const char* tmp = ::getenv("TMPDIR");
    const std::string path = std::string((tmp != nullptr) ? tmp : "/tmp") + "/qypr-theme-test.conf";
    {
        std::ofstream f(path);
        f << "[theme]\n"
          << "style = macos\n"  // legacy alias → glass rendering
          << "icon-style = glyph\n"
          << "background = #112233\n"
          << "text = #ffeedd\n";
    }
    qypr::Config c;
    c.load(path);
    qypr::theme::State stC = applyTheme(c);

    // `macos` is accepted but resolves to the generic glass rendering path.
    EXPECT_EQ(stC.style.mode, std::string("glass"));
    // icon-style honoured.
    EXPECT_TRUE(stC.icons.mode == qypr::theme::icons::Mode::Glyph);
    // Backdrop tint defaults to the configured background, border to the text
    // colour — the strip tracks whatever palette the user set, not a fixed hue.
    EXPECT_NEAR(stC.statusbar.barTint.r, 0x11 / 255.0, 0.01);
    EXPECT_NEAR(stC.statusbar.barTint.g, 0x22 / 255.0, 0.01);
    EXPECT_NEAR(stC.statusbar.barTint.b, 0x33 / 255.0, 0.01);
    EXPECT_NEAR(stC.statusbar.barBorder.r, 0xff / 255.0, 0.01);
    EXPECT_NEAR(stC.statusbar.barBorder.g, 0xee / 255.0, 0.01);
    EXPECT_NEAR(stC.statusbar.barBorder.b, 0xdd / 255.0, 0.01);

    // An explicit bar-tint override wins over the derived default.
    {
        std::ofstream f(path);
        f << "[theme]\n"
          << "icon-style = symbolic\n"
          << "background = #112233\n"
          << "bar-tint = #445566\n";
    }
    qypr::Config c2;
    c2.load(path);
    qypr::theme::State stC2 = applyTheme(c2);
    EXPECT_TRUE(stC2.icons.mode == qypr::theme::icons::Mode::Symbolic);
    EXPECT_NEAR(stC2.statusbar.barTint.r, 0x44 / 255.0, 0.01);
    EXPECT_NEAR(stC2.statusbar.barTint.g, 0x55 / 255.0, 0.01);
    EXPECT_NEAR(stC2.statusbar.barTint.b, 0x66 / 255.0, 0.01);

    std::remove(path.c_str());
}

// A matugen CSS template (custom properties) maps Material You tokens onto the
// bar palette: on-surface → text, surface-container → surface, tertiary →
// success, etc. Explicit [theme] keys still win over the palette.
TEST(ThemeMatugenCssPalette) {
    const std::string palette = writeTempConfig(":root {\n"
                                                "  --primary: #aabbcc;\n"
                                                "  --on-surface: #ddeeff;\n"
                                                "  --on-surface-variant: #99aabb;\n"
                                                "  --outline: #556677;\n"
                                                "  --surface-container: #223344;\n"
                                                "  --surface-container-high: #334455;\n"
                                                "  --surface: #101820;\n"
                                                "  --error: #ff5544;\n"
                                                "  --tertiary: #66d9a0;\n"
                                                "}\n");
    const std::string conf = writeTempConfig("[theme]\n"
                                             "colors-file = " +
                                             palette + "\n");
    qypr::Config c;
    c.load(conf);
    qypr::theme::State stC = applyTheme(c);

    EXPECT_NEAR(stC.colors.primary.r, 0xaa / 255.0, 0.01);
    EXPECT_NEAR(stC.colors.primary.g, 0xbb / 255.0, 0.01);
    EXPECT_NEAR(stC.colors.primary.b, 0xcc / 255.0, 0.01);
    // No `background` token: falls back to the M3 `surface` tone.
    EXPECT_NEAR(stC.colors.background.b, 0x20 / 255.0, 0.01);
    // surface / hover come from the M3 container tones.
    EXPECT_NEAR(stC.colors.surface.r, 0x22 / 255.0, 0.01);
    EXPECT_NEAR(stC.colors.surfaceHover.r, 0x33 / 255.0, 0.01);
    EXPECT_NEAR(stC.colors.text.r, 0xdd / 255.0, 0.01);
    EXPECT_NEAR(stC.colors.textSubtle.r, 0x99 / 255.0, 0.01);
    EXPECT_NEAR(stC.colors.textMuted.r, 0x55 / 255.0, 0.01);
    EXPECT_NEAR(stC.colors.error.g, 0x55 / 255.0, 0.01);
    // success has no dedicated token: falls back to tertiary.
    EXPECT_NEAR(stC.colors.success.g, 0xd9 / 255.0, 0.01);

    // An explicit colour key wins over the palette.
    const std::string conf2 = writeTempConfig("[theme]\n"
                                              "colors-file = " +
                                              palette +
                                              "\n"
                                              "primary = #010203\n");
    qypr::Config c2;
    c2.load(conf2);
    qypr::theme::State stC2 = applyTheme(c2);
    EXPECT_NEAR(stC2.colors.primary.r, 0x01 / 255.0, 0.01);
    // ...while palette-driven keys still apply.
    EXPECT_NEAR(stC2.colors.text.r, 0xdd / 255.0, 0.01);

    std::remove(conf.c_str());
    std::remove(conf2.c_str());
    std::remove(palette.c_str());
}

// matugen's nested JSON scheme format and the GTK @define-color style are both
// understood; `matugen` is an accepted alias for `colors-file`.
TEST(ThemeMatugenJsonAndDefineColor) {
    const std::string json =
        writeTempConfig("{\n"
                        "  \"primary\": { \"hex\": \"#112233\" },\n"
                        "  \"error\": { \"hex\": \"#445566\" },\n"
                        "  \"on_surface\": \"#8899aa\"\n"  // matugen emits underscore names
                        "}\n");
    const std::string conf = writeTempConfig("[theme]\n"
                                             "matugen = " +
                                             json + "\n");  // alias key
    qypr::Config c;
    c.load(conf);
    qypr::theme::State stC = applyTheme(c);
    EXPECT_NEAR(stC.colors.primary.r, 0x11 / 255.0, 0.01);
    EXPECT_NEAR(stC.colors.primary.b, 0x33 / 255.0, 0.01);
    EXPECT_NEAR(stC.colors.error.g, 0x55 / 255.0, 0.01);
    EXPECT_NEAR(stC.colors.text.r, 0x88 / 255.0, 0.01);
    std::remove(conf.c_str());
    std::remove(json.c_str());

    const std::string gtk = writeTempConfig("@define-color primary #abcdef;\n"
                                            "@define-color error #654321;\n");
    const std::string conf2 = writeTempConfig("[theme]\n"
                                              "colors-file = " +
                                              gtk + "\n");
    qypr::Config c2;
    c2.load(conf2);
    qypr::theme::State stC2 = applyTheme(c2);
    EXPECT_NEAR(stC2.colors.primary.r, 0xab / 255.0, 0.01);
    EXPECT_NEAR(stC2.colors.error.r, 0x65 / 255.0, 0.01);
    std::remove(conf2.c_str());
    std::remove(gtk.c_str());
}

// ConfigWatcher must survive a handler that re-arms the watcher from inside
// the callback (BarApp::watchPalette does stop()+watch() on every reload).
// Previously drain() invoked onChange_() in place, so the re-arm destroyed the
// std::function being executed — the second event was lost (or crashed).
TEST(ThemePaletteModeSelection) {
    const std::string darkPal = writeTempConfig("--background: #111111;\n--primary: #222222;\n");
    const std::string lightPal = writeTempConfig("--background: #eeeeee;\n--primary: #3355aa;\n");

    // dark mode → dark file, standard shadow.
    qypr::Config cd;
    cd.load(writeTempConfig("[theme]\n"
                            "palette-mode = dark\n"
                            "colors-file = " +
                            darkPal +
                            "\n"
                            "colors-file-light = " +
                            lightPal + "\n"));
    qypr::theme::AutoPalette palDark = qypr::theme::AutoPalette::fromConfig(cd, 12);
    qypr::theme::State stCd = qypr::theme::loadThemeState(cd, palDark);
    EXPECT_EQ(palDark.resolved, std::string("dark"));
    EXPECT_NEAR(stCd.colors.background.r, 0x11 / 255.0, 0.01);
    EXPECT_NEAR(stCd.colors.primary.g, 0x22 / 255.0, 0.01);
    EXPECT_NEAR(stCd.effects.shadowOpacity, 0.6, 0.01);

    // light mode → light file, shadow disabled (no ghost shades).
    qypr::Config cl;
    cl.load(writeTempConfig("[theme]\n"
                            "palette-mode = light\n"
                            "colors-file = " +
                            darkPal +
                            "\n"
                            "colors-file-light = " +
                            lightPal + "\n"));
    qypr::theme::AutoPalette palLight = qypr::theme::AutoPalette::fromConfig(cl, 12);
    qypr::theme::State stCl = qypr::theme::loadThemeState(cl, palLight);
    EXPECT_EQ(palLight.resolved, std::string("light"));
    EXPECT_NEAR(stCl.colors.background.r, 0xee / 255.0, 0.01);
    EXPECT_NEAR(stCl.colors.primary.b, 0xaa / 255.0, 0.01);
    EXPECT_NEAR(stCl.effects.shadowOpacity, 0.0, 0.01);

    // A dark load after a light one restores the default shadow.
    qypr::Config cd2;
    cd2.load(writeTempConfig("[theme]\n"
                             "palette-mode = dark\n"
                             "colors-file = " +
                             darkPal + "\n"));
    qypr::theme::State stCd2 = applyTheme(cd2);
    EXPECT_NEAR(stCd2.effects.shadowOpacity, 0.6, 0.01);

    // An explicit shadow-opacity wins over the light-mode softening.
    qypr::Config ce;
    ce.load(writeTempConfig("[theme]\n"
                            "palette-mode = light\n"
                            "shadow-opacity = 0.5\n"
                            "colors-file-light = " +
                            lightPal + "\n"));
    qypr::theme::State stCe = applyTheme(ce);
    EXPECT_NEAR(stCe.effects.shadowOpacity, 0.5, 0.01);

    // light mode with no light file falls back to the dark file's palette.
    qypr::Config cf;
    cf.load(writeTempConfig("[theme]\n"
                            "palette-mode = light\n"
                            "colors-file = " +
                            darkPal + "\n"));
    qypr::theme::State stCf = applyTheme(cf);
    EXPECT_NEAR(stCf.colors.background.r, 0x11 / 255.0, 0.01);
}

// The auto palette mode resolves by hour: light in [sunrise, sunset), dark
// outside. Hour bounds are configurable. Fully deterministic: the value
// takes the hour as a parameter, so no wall clock is read.
TEST(ThemePaletteAutoResolve) {
    using qypr::theme::AutoPalette;
    EXPECT_EQ(AutoPalette::resolveFor(7, 7, 19), std::string("light"));
    EXPECT_EQ(AutoPalette::resolveFor(12, 7, 19), std::string("light"));
    EXPECT_EQ(AutoPalette::resolveFor(18, 7, 19), std::string("light"));
    EXPECT_EQ(AutoPalette::resolveFor(19, 7, 19), std::string("dark"));
    EXPECT_EQ(AutoPalette::resolveFor(3, 7, 19), std::string("dark"));
    EXPECT_EQ(AutoPalette::resolveFor(6, 7, 19), std::string("dark"));
    // Midnight-spanning window is not supported by design: sunrise < sunset.
    EXPECT_EQ(AutoPalette::resolveFor(23, 20, 6), std::string("dark"));

    // fromConfig parses keys and resolves against the given hour.
    qypr::Config c;
    c.load(writeTempConfig(
        "[theme]\npalette-mode = auto\npalette-sunrise = 7\npalette-sunset = 19\n"));
    AutoPalette noon = AutoPalette::fromConfig(c, 12);
    EXPECT_EQ(noon.mode, std::string("auto"));
    EXPECT_EQ(noon.resolved, std::string("light"));
    AutoPalette night = AutoPalette::fromConfig(c, 22);
    EXPECT_EQ(night.resolved, std::string("dark"));

    // Ticks only flip on a real transition, and never outside auto mode.
    EXPECT_FALSE(noon.tick(13));
    EXPECT_EQ(noon.resolved, std::string("light"));
    EXPECT_TRUE(noon.tick(20));
    EXPECT_EQ(noon.resolved, std::string("dark"));
    AutoPalette fixed = AutoPalette::fromConfig(c, 12);
    fixed.mode = "dark";
    EXPECT_FALSE(fixed.tick(22));

    // The mode key is validated; an unknown value keeps the dark default.
    qypr::Config bad;
    bad.load(writeTempConfig("[theme]\npalette-mode = sometimes\n"));
    AutoPalette palBad = AutoPalette::fromConfig(bad, 12);
    EXPECT_EQ(palBad.mode, std::string("dark"));
    EXPECT_EQ(palBad.resolved, std::string("dark"));
}

// `~` in colors-file expands against $HOME.
TEST(ThemeMatugenHomeExpansion) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded test binary.
    const char* tmp = ::getenv("TMPDIR");
    const std::string home = std::string((tmp != nullptr) ? tmp : "/tmp") + "/qypr-matugen-home";
    ::mkdir(home.c_str(), 0755);
    const std::string palette = home + "/palette.css";
    {
        std::ofstream f(palette);
        f << "--primary: #2468ac;\n";
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded test binary.
    const char* oldHome = ::getenv("HOME");
    const std::string savedHome = (oldHome != nullptr) ? oldHome : "";
    // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded test binary.
    ::setenv("HOME", home.c_str(), 1);

    const std::string conf = writeTempConfig("[theme]\n"
                                             "colors-file = ~/palette.css\n");
    qypr::Config c;
    c.load(conf);
    qypr::theme::State stC = applyTheme(c);
    EXPECT_NEAR(stC.colors.primary.r, 0x24 / 255.0, 0.01);

    if (savedHome.empty()) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded test binary.
        ::unsetenv("HOME");
    } else {
        // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded test binary.
        ::setenv("HOME", savedHome.c_str(), 1);
    }
    std::remove(conf.c_str());
    std::remove(palette.c_str());
    std::remove(home.c_str());
}

// =============================================================================
// Phase 2: Clock + Battery Indicator Tests
// =============================================================================

// Solar goldens live with the shared unit (common/tests/solar_test.cpp) —
// this TU covers only the wiring: cache, fallback, and config. Each case
// owns its AutoPalette value, so no global state leaks between tests.

// The solar cache takes over the auto window while location == auto, and
// the fixed hours (or location = off) restore it. Invalid windows clear.
TEST(ThemeSolarCacheOverridesFixedHours) {
    using qypr::theme::AutoPalette;
    qypr::Config c;
    c.load(writeTempConfig(
        "[theme]\npalette-mode = auto\npalette-sunrise = 7\npalette-sunset = 19\n"));
    AutoPalette pal = AutoPalette::fromConfig(c, 12);
    // No fix cached: the fixed hours rule.
    EXPECT_EQ(pal.effectiveSunriseHour(), 7);
    EXPECT_EQ(pal.effectiveSunsetHour(), 19);
    EXPECT_EQ(pal.resolved, std::string("light"));
    // A solar fix takes over while location == auto …
    pal.setSolarTimes((5 * 60) + 30, (20 * 60) + 45);
    EXPECT_EQ(pal.effectiveSunriseHour(), 5);
    EXPECT_EQ(pal.effectiveSunsetHour(), 20);
    EXPECT_EQ(AutoPalette::resolveFor(6, pal.effectiveSunriseHour(), pal.effectiveSunsetHour()),
              std::string("light"));
    EXPECT_EQ(AutoPalette::resolveFor(21, pal.effectiveSunriseHour(), pal.effectiveSunsetHour()),
              std::string("dark"));
    EXPECT_TRUE(pal.tick(21));
    EXPECT_EQ(pal.resolved, std::string("dark"));
    // … clearing restores the fixed hours …
    pal.clearSolarTimes();
    EXPECT_EQ(pal.effectiveSunriseHour(), 7);
    EXPECT_EQ(pal.effectiveSunsetHour(), 19);
    // … and location = off ignores even a cached fix.
    qypr::Config off;
    off.load(writeTempConfig("[theme]\npalette-mode = auto\npalette-location = off\n"));
    AutoPalette palOff = AutoPalette::fromConfig(off, 12);
    palOff.setSolarTimes(300, 1200);
    EXPECT_EQ(palOff.effectiveSunriseHour(), 7);
    EXPECT_EQ(palOff.effectiveSunsetHour(), 19);
    // Out-of-range windows never drive the theme.
    palOff.setSolarTimes(-1, 1200);
    EXPECT_EQ(palOff.effectiveSunriseHour(), 7);
    palOff.setSolarTimes(300, 1440);
    EXPECT_EQ(palOff.effectiveSunriseHour(), 7);
    // The location key is validated; an unknown value keeps the auto default.
    qypr::Config bad;
    bad.load(writeTempConfig("[theme]\npalette-location = sometimes\n"));
    AutoPalette palBad = AutoPalette::fromConfig(bad, 12);
    EXPECT_EQ(palBad.location, std::string("auto"));
}

// Palette format decoding is unit-testable without a Config or State
// (QYPR_DECOMPOSITION_PLAN step 4: PaletteReader extraction).
TEST(PaletteReaderParsesMatugenFormats) {
    // CSS custom properties + GTK define-color + nested JSON + flat JSON
    // may appear in one file; first occurrence of a token wins; names are
    // normalised (lower-case, `--` stripped, `_` → `-`).
    const std::string mixed =
        ":root {\n"
        "  --primary: #aabbcc;\n"
        "  --on_surface: #ddeeff;\n"  // matugen underscore name
        "}\n"
        "@define-color error #ff5544;\n"
        "{\n"
        "  \"tertiary\": { \"hex\": \"#66d9a0\" },\n"
        "  \"surface_container\": \"#223344\"\n"
        "}\n"
        "outline: #556677;\n";
    const auto tokens = qypr::theme::parsePalette(mixed);
    EXPECT_EQ(tokens.at("primary"), std::string("#aabbcc"));
    EXPECT_EQ(tokens.at("on-surface"), std::string("#ddeeff"));
    EXPECT_EQ(tokens.at("error"), std::string("#ff5544"));
    EXPECT_EQ(tokens.at("tertiary"), std::string("#66d9a0"));
    EXPECT_EQ(tokens.at("surface-container"), std::string("#223344"));
    EXPECT_EQ(tokens.at("outline"), std::string("#556677"));

    // applyPaletteTokens maps candidates through MatugenTokens and returns
    // how many colours landed; unknown tokens write nothing.
    qypr::theme::State st;
    const int applied = qypr::theme::applyPaletteTokens(tokens, st);
    EXPECT_TRUE(applied > 0);
    EXPECT_NEAR(st.colors.primary.r, 0xaa / 255.0, 0.01);
    EXPECT_NEAR(st.colors.text.r, 0xdd / 255.0, 0.01);  // on-surface → text
    EXPECT_NEAR(st.colors.error.r, 0xff / 255.0, 0.01);
    EXPECT_NEAR(st.colors.success.g, 0xd9 / 255.0, 0.01);  // tertiary → success
    EXPECT_NEAR(st.colors.surface.r, 0x22 / 255.0, 0.01);

    const auto empty = qypr::theme::parsePalette("not-a-palette");
    EXPECT_TRUE(empty.empty());
    qypr::theme::State untouched;
    EXPECT_EQ(qypr::theme::applyPaletteTokens(empty, untouched), 0);
}
