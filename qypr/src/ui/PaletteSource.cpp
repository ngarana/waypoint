// PaletteSource.cpp - Path resolution and palette-file loading.
#include "ui/PaletteSource.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/Config.hpp"

namespace qypr::theme {

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

std::string readPaletteFile(const std::string& path) {
    if (path.empty()) { return ""; }
    std::ifstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "qypr: theme: colours file not readable: %s\n", path.c_str());
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace qypr::theme
