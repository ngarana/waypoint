// PaletteSource.hpp - Locate and load matugen/palette files for the theme.
//
// Extracted from Theme.cpp (QYPR_DECOMPOSITION_PLAN step 4): path resolution
// and file I/O live here so Theme.cpp only builds State from Config + palette.
#pragma once

#include <string>

namespace qypr {

class Config;

namespace theme {

// Resolve the [theme] colors-file / matugen key to a filesystem path.
// Returns "" when the key is unset or empty. `~` is expanded against $HOME
// and relative paths resolve against the loaded config file's directory
// (the same rule as the `import` directive). `lightPalette` selects the
// light-palette keys (colors-file-light / matugen-light).
std::string resolveColorsPath(const Config& cfg, bool lightPalette = false);

// Read the whole palette file at `path`. Returns "" when the file is missing
// or unreadable (callers treat empty content as "no palette").
std::string readPaletteFile(const std::string& path);

}  // namespace theme
}  // namespace qypr
