// AppTile.hpp - Shared helpers for drawing an app as a small tile.
//
// Used by every module that renders windows by their app id (taskbar, pager):
// resolve a freedesktop icon when one exists, else fall back to a themed
// initial-letter tile whose hue is derived from the app id (stable per app,
// distinguishable between apps).
#pragma once

#include <algorithm>
#include <cctype>
#include <string>

#include "core/Types.hpp"
#include "render/IconResolver.hpp"
#include "ui/Theme.hpp"

namespace qypr::apptile {

// A themed initial-letter tile colour. The hue is derived from the app id so
// the same app is always the same colour, and different apps are
// distinguishable.
inline Color fallbackColor(const std::string& appId, const theme::State& theme) {
    const Color palette[] = {theme.colors.blue,  theme.colors.green, theme.colors.yellow,
                             theme.colors.red,   theme.colors.mauve, theme.colors.teal,
                             theme.colors.peach, theme.colors.sky};
    uint32_t h = 2166136261u;
    for (char c : appId) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
    return palette[h % (sizeof(palette) / sizeof(palette[0]))];
}

// The letter for the fallback tile: last dotted segment's first char, upper.
// "org.kde.dolphin" → 'D', "firefox" → 'F', "" → '?'.
inline std::string initialFor(const std::string& appId) {
    if (appId.empty()) return "?";
    size_t start = appId.find_last_of('.');
    start = (start == std::string::npos) ? 0 : start + 1;
    if (start >= appId.size()) start = 0;
    char c = appId[start];
    return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
}

// Human-readable app name for tooltips: last dotted segment of the app id.
inline std::string prettyApp(const std::string& appId) {
    if (appId.empty()) return "unknown";
    const size_t start = appId.find_last_of('.');
    return start == std::string::npos ? appId : appId.substr(start + 1);
}

// Try the app id, then its lowercase form, as a freedesktop icon name.
inline cairo_surface_t* resolveIcon(const std::string& appId) {
    if (appId.empty()) return nullptr;
    if (auto* s = IconResolver::instance().get(appId)) return s;
    std::string lower = appId;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower != appId) {
        if (auto* s = IconResolver::instance().get(lower)) return s;
    }
    return nullptr;
}

}  // namespace qypr::apptile
