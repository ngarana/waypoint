// PaletteReader.hpp - Format-specific palette decoding + token→theme mapping.
//
// Extracted from Theme.cpp (QYPR_DECOMPOSITION_PLAN step 4):
//   - parsePalette: CSS/GTK/JSON/flat-JSON token extraction from file text
//   - applyPaletteTokens: Material You token → State::Colors via MatugenTokens
// Theme.cpp keeps only loadThemeState (defaults + typed [theme] overrides)
// and AutoPalette; this module owns the palette-file side of the pipeline.
#pragma once

#include <map>
#include <string>

#include "ui/Theme.hpp"

namespace qypr {
namespace theme {

// Pull every `token -> #hex` pair out of a palette file's text. Handles the
// formats matugen templates actually produce (they may be mixed in one file):
//   "token": { "hex": "#aabbcc", ... }   matugen's JSON scheme format
//   --token: #aabbcc;                    CSS custom properties
//   @define-color token #aabbcc;         GTK/GDK palette files
//   "token": "#aabbcc"                   flat JSON / quoted CSS values
//   token: #aabbcc;                      plain CSS declarations
// First occurrence of a token wins; names are lower-cased, `--` stripped.
std::map<std::string, std::string> parsePalette(const std::string& content);

// Apply parsed tokens onto `state` using the shared MatugenTokens candidate
// lookup. Returns the number of colours actually written (0 when no known
// tokens are present).
int applyPaletteTokens(const std::map<std::string, std::string>& tokens, State& state);

// Parse a palette file path and apply its tokens. Understood formats match
// parsePalette. Returns colours applied (0 when missing/unreadable/unknown).
int applyColorsFile(const std::string& path, State& state);

}  // namespace theme
}  // namespace qypr
