// PaletteReader.cpp - Palette format decoding and token→State application.
#include "ui/PaletteReader.hpp"

#include <cstdio>
#include <regex>
#include <vector>

#include "render/MatugenTokens.hpp"
#include "ui/PaletteSource.hpp"

namespace qypr::theme {

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

namespace {

// Material You token -> bar colour. Each entry tries its candidate tokens in
// order and keeps the current value when none are present, so narrow custom
// templates (a few `--background`/`--primary` lines) still theme the bar.
struct PaletteMapping {
    Color State::Colors::* target;
    std::vector<const char*> tokens;
    // >= 0: alpha applied when the matched hex carries none of its own
    // (#RRGGBB). < 0: keep whatever alpha the hex provided (default 1.0).
    double alpha = -1.0;
};

const std::vector<PaletteMapping>& paletteMappings() {
    static const std::vector<PaletteMapping> kMappings = {
        // Core surfaces & text. `background` prefers a dedicated background
        // token, then falls back to the M3 `surface` tone.
        {.target = &State::Colors::background,
         .tokens = {"background", "surface", "surface-container-lowest"}},
        {.target = &State::Colors::surface,
         .tokens = {"surface-container", "surface-container-low", "secondary-container",
                    "surface-variant", "surface"}},
        {.target = &State::Colors::surfaceHover,
         .tokens = {"surface-container-high", "surface-container-highest", "surface-variant",
                    "surface-container"}},
        {.target = &State::Colors::primary,
         .tokens = {"primary", "primary-fixed", "primary-container"}},
        // The glow is a translucent primary unless the template ships its own
        // 8-digit (AARRGGBB) value.
        {.target = &State::Colors::primaryGlow,
         .tokens = {"primary-glow", "primary"},
         .alpha = 0.25},
        {.target = &State::Colors::text,
         .tokens = {"on-surface", "on-background", "foreground", "on-primary-container", "text"}},
        {.target = &State::Colors::textSubtle,
         .tokens = {"on-surface-variant", "outline", "text-secondary"}},
        {.target = &State::Colors::textMuted,
         .tokens = {"outline", "outline-variant", "text-muted"}},
        {.target = &State::Colors::error, .tokens = {"error"}},
        // M3 has no success/warning hues; tertiary (and secondary for a
        // warning-ish tone) are the usual stand-ins. `success`/`warning`
        // custom keys win when a template defines them.
        {.target = &State::Colors::success, .tokens = {"success", "tertiary"}},
        {.target = &State::Colors::warning, .tokens = {"warning", "secondary"}},
        // Accent strip (app tiles, status dots, pager chips). Matugen palettes
        // are effectively two-accent, so accents fan out from the M3 roles.
        {.target = &State::Colors::blue, .tokens = {"blue", "primary"}},
        {.target = &State::Colors::lavender, .tokens = {"lavender", "primary-container"}},
        {.target = &State::Colors::mauve, .tokens = {"mauve", "tertiary"}},
        {.target = &State::Colors::pink, .tokens = {"pink", "tertiary"}},
        {.target = &State::Colors::red, .tokens = {"red", "error"}},
        {.target = &State::Colors::peach, .tokens = {"peach", "secondary"}},
        {.target = &State::Colors::yellow, .tokens = {"yellow", "warning"}},
        {.target = &State::Colors::green, .tokens = {"green", "success"}},
        {.target = &State::Colors::teal, .tokens = {"teal", "secondary-container"}},
        {.target = &State::Colors::sky, .tokens = {"sky", "secondary"}},
        {.target = &State::Colors::maroon, .tokens = {"maroon", "error-container"}},
    };
    return kMappings;
}

}  // namespace

int applyPaletteTokens(const std::map<std::string, std::string>& tokens, State& state) {
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
            (state.colors.*m.target) = c;
            ++applied;
        }
    }
    return applied;
}

int applyColorsFile(const std::string& path, State& state) {
    const std::string content = readPaletteFile(path);
    if (content.empty()) { return 0; }
    const auto tokens = parsePalette(content);
    const int applied = applyPaletteTokens(tokens, state);
    if (applied > 0) {
        std::fprintf(stderr, "qypr: theme: matugen palette applied (%d colours from %s)\n", applied,
                     path.c_str());
    } else {
        std::fprintf(stderr, "qypr: theme: no known colour tokens in %s\n", path.c_str());
    }
    return applied;
}

}  // namespace qypr::theme
