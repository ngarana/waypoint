#pragma once

#include <cstdint>
#include <string>

namespace waylaunch {

// Sanitation for the launcher's single-line search query. Control codepoints
// must never enter the query: xkb folds editing keys onto them (e.g. Delete ->
// U+007F DEL) and pasted clipboard text can carry newlines/tabs/other controls.
// The typed path and the paste path share this one gate.

// A control codepoint: C0 (including 0 for no-symbol keys), DEL, or C1.
bool is_control_cp(uint32_t cp);

// Drop control codepoints from UTF-8 text. Decodes per codepoint (not per byte)
// so C1 controls, which are two UTF-8 bytes, are removed without shearing
// adjacent multibyte characters; malformed bytes are dropped rather than
// corrupting the query.
std::string strip_control_chars(const std::string& in);

} // namespace waylaunch
