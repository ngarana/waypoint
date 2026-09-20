// test_theme.cpp - Theme loading, palettes, matugen sources.
// Split verbatim from tests/unit_tests.cpp; bodies unchanged.
#include "test_framework.hpp"

TEST(ThemeLoadThemeDefaults) {
    qypr::Config c;
    c.load("/nonexistent");
    qypr::theme::loadTheme(c);
    EXPECT_EQ(qypr::theme::font::family, std::string("Inter"));
    EXPECT_EQ(qypr::theme::font::iconFamily, std::string("CaskaydiaCove Nerd Font"));
    EXPECT_EQ(qypr::theme::font::size, 16);
    EXPECT_NEAR(qypr::theme::color::primary.r, 0.537, 0.01);
    EXPECT_NEAR(qypr::theme::statusbar::height, 36.0, 0.01);
    // Icons default to Auto (themed-when-available, else glyph).
    EXPECT_TRUE(qypr::theme::icons::mode == qypr::theme::icons::Mode::Auto);
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
    qypr::theme::loadTheme(c);

    // `macos` is accepted but resolves to the generic glass rendering path.
    EXPECT_EQ(qypr::theme::style::mode, std::string("glass"));
    // icon-style honoured.
    EXPECT_TRUE(qypr::theme::icons::mode == qypr::theme::icons::Mode::Glyph);
    // Backdrop tint defaults to the configured background, border to the text
    // colour — the strip tracks whatever palette the user set, not a fixed hue.
    EXPECT_NEAR(qypr::theme::statusbar::barTint.r, 0x11 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::statusbar::barTint.g, 0x22 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::statusbar::barTint.b, 0x33 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::statusbar::barBorder.r, 0xff / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::statusbar::barBorder.g, 0xee / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::statusbar::barBorder.b, 0xdd / 255.0, 0.01);

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
    qypr::theme::loadTheme(c2);
    EXPECT_TRUE(qypr::theme::icons::mode == qypr::theme::icons::Mode::Symbolic);
    EXPECT_NEAR(qypr::theme::statusbar::barTint.r, 0x44 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::statusbar::barTint.g, 0x55 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::statusbar::barTint.b, 0x66 / 255.0, 0.01);

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
    qypr::theme::loadTheme(c);

    EXPECT_NEAR(qypr::theme::color::primary.r, 0xaa / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::primary.g, 0xbb / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::primary.b, 0xcc / 255.0, 0.01);
    // No `background` token: falls back to the M3 `surface` tone.
    EXPECT_NEAR(qypr::theme::color::background.b, 0x20 / 255.0, 0.01);
    // surface / hover come from the M3 container tones.
    EXPECT_NEAR(qypr::theme::color::surface.r, 0x22 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::surfaceHover.r, 0x33 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::text.r, 0xdd / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::textSubtle.r, 0x99 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::textMuted.r, 0x55 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::error.g, 0x55 / 255.0, 0.01);
    // success has no dedicated token: falls back to tertiary.
    EXPECT_NEAR(qypr::theme::color::success.g, 0xd9 / 255.0, 0.01);

    // An explicit colour key wins over the palette.
    const std::string conf2 = writeTempConfig("[theme]\n"
                                              "colors-file = " +
                                              palette +
                                              "\n"
                                              "primary = #010203\n");
    qypr::Config c2;
    c2.load(conf2);
    qypr::theme::loadTheme(c2);
    EXPECT_NEAR(qypr::theme::color::primary.r, 0x01 / 255.0, 0.01);
    // ...while palette-driven keys still apply.
    EXPECT_NEAR(qypr::theme::color::text.r, 0xdd / 255.0, 0.01);

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
    qypr::theme::loadTheme(c);
    EXPECT_NEAR(qypr::theme::color::primary.r, 0x11 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::primary.b, 0x33 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::error.g, 0x55 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::text.r, 0x88 / 255.0, 0.01);
    std::remove(conf.c_str());
    std::remove(json.c_str());

    const std::string gtk = writeTempConfig("@define-color primary #abcdef;\n"
                                            "@define-color error #654321;\n");
    const std::string conf2 = writeTempConfig("[theme]\n"
                                              "colors-file = " +
                                              gtk + "\n");
    qypr::Config c2;
    c2.load(conf2);
    qypr::theme::loadTheme(c2);
    EXPECT_NEAR(qypr::theme::color::primary.r, 0xab / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::error.r, 0x65 / 255.0, 0.01);
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
    qypr::theme::loadTheme(cd);
    EXPECT_EQ(qypr::theme::palette::resolved, std::string("dark"));
    EXPECT_NEAR(qypr::theme::color::background.r, 0x11 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::primary.g, 0x22 / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::effects::shadowOpacity, 0.6, 0.01);

    // light mode → light file, shadow disabled (no ghost shades).
    qypr::Config cl;
    cl.load(writeTempConfig("[theme]\n"
                            "palette-mode = light\n"
                            "colors-file = " +
                            darkPal +
                            "\n"
                            "colors-file-light = " +
                            lightPal + "\n"));
    qypr::theme::loadTheme(cl);
    EXPECT_EQ(qypr::theme::palette::resolved, std::string("light"));
    EXPECT_NEAR(qypr::theme::color::background.r, 0xee / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::primary.b, 0xaa / 255.0, 0.01);
    EXPECT_NEAR(qypr::theme::effects::shadowOpacity, 0.0, 0.01);

    // A dark load after a light one restores the default shadow.
    qypr::Config cd2;
    cd2.load(writeTempConfig("[theme]\n"
                             "palette-mode = dark\n"
                             "colors-file = " +
                             darkPal + "\n"));
    qypr::theme::loadTheme(cd2);
    EXPECT_NEAR(qypr::theme::effects::shadowOpacity, 0.6, 0.01);

    // An explicit shadow-opacity wins over the light-mode softening.
    qypr::Config ce;
    ce.load(writeTempConfig("[theme]\n"
                            "palette-mode = light\n"
                            "shadow-opacity = 0.5\n"
                            "colors-file-light = " +
                            lightPal + "\n"));
    qypr::theme::loadTheme(ce);
    EXPECT_NEAR(qypr::theme::effects::shadowOpacity, 0.5, 0.01);

    // light mode with no light file falls back to the dark file's palette.
    qypr::Config cf;
    cf.load(writeTempConfig("[theme]\n"
                            "palette-mode = light\n"
                            "colors-file = " +
                            darkPal + "\n"));
    qypr::theme::loadTheme(cf);
    EXPECT_NEAR(qypr::theme::color::background.r, 0x11 / 255.0, 0.01);
}

// The auto palette mode resolves by local hour: light in [sunrise, sunset),
// dark outside. Hour bounds are configurable.
TEST(ThemePaletteAutoResolve) {
    using qypr::theme::resolveAutoPaletteMode;
    EXPECT_EQ(resolveAutoPaletteMode(7, 7, 19), std::string("light"));
    EXPECT_EQ(resolveAutoPaletteMode(12, 7, 19), std::string("light"));
    EXPECT_EQ(resolveAutoPaletteMode(18, 7, 19), std::string("light"));
    EXPECT_EQ(resolveAutoPaletteMode(19, 7, 19), std::string("dark"));
    EXPECT_EQ(resolveAutoPaletteMode(3, 7, 19), std::string("dark"));
    EXPECT_EQ(resolveAutoPaletteMode(6, 7, 19), std::string("dark"));
    // Midnight-spanning window is not supported by design: sunrise < sunset.
    EXPECT_EQ(resolveAutoPaletteMode(23, 20, 6), std::string("dark"));

    // The mode key is validated; an unknown value keeps the dark default.
    qypr::Config c;
    c.load(writeTempConfig("[theme]\npalette-mode = sometimes\n"));
    qypr::theme::loadTheme(c);
    EXPECT_EQ(qypr::theme::palette::mode, std::string("dark"));
    EXPECT_EQ(qypr::theme::palette::resolved, std::string("dark"));
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
    qypr::theme::loadTheme(c);
    EXPECT_NEAR(qypr::theme::color::primary.r, 0x24 / 255.0, 0.01);

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
