// MatugenTokens.hpp - Shared Material You token vocabulary + lookup.
//
// Both theme parsers (qypr's applyColorsFile, waylaunch's MatugenTheme)
// resolve semantic slots from candidate token lists. The candidate LISTS
// stay per-consumer — the file formats carry different token universes
// (matugen CSS/GTK/JSON vs matugen --json hex) — but the lookup mechanics
// and the empty-token rule live here once (ARCHITECTURE_REVIEW finding 9).
//
// Canonical semantic slots both sides must cover: background, surface and
// its container variants, primary, on-surface (+variant), error, warning,
// success, outline (+variant), secondary (+container), tertiary.
#pragma once

#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace qypr {

// First present non-empty value among the priority-ordered candidates, or
// "". Empty values never win: a present-but-blank token must not shadow a
// later real one.
std::string pickMatugenToken(const std::map<std::string, std::string>& tokens,
                             const std::vector<const char*>& candidates);
std::string pickMatugenToken(const std::map<std::string, std::string>& tokens,
                             std::initializer_list<const char*> candidates);

}  // namespace qypr
