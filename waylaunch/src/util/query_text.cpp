#include "waylaunch/query_text.h"

namespace waylaunch {

bool is_control_cp(uint32_t cp) {
    return cp < 0x20U || cp == 0x7FU || (cp >= 0x80U && cp <= 0x9FU);
}

std::string strip_control_chars(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    const size_t n = in.size();
    size_t i = 0;
    while (i < n) {
        auto const c = static_cast<unsigned char>(in[i]);
        uint32_t cp = 0;
        size_t len = 0;
        if (c < 0x80U) {
            cp = c;
            len = 1;
        } else if ((c & 0xE0U) == 0xC0U) {
            cp = c & 0x1FU;
            len = 2;
        } else if ((c & 0xF0U) == 0xE0U) {
            cp = c & 0x0FU;
            len = 3;
        } else if ((c & 0xF8U) == 0xF0U) {
            cp = c & 0x07U;
            len = 4;
        } else {
            ++i; // stray continuation or invalid lead byte: drop and resync
            continue;
        }
        if (i + len > n) { break; } // truncated sequence at end of input
        bool valid = true;
        for (size_t k = 1; k < len; ++k) {
            auto const cont = static_cast<unsigned char>(in[i + k]);
            if ((cont & 0xC0U) != 0x80U) {
                valid = false;
                break;
            }
            cp = (cp << 6U) | (cont & 0x3FU);
        }
        if (!valid) {
            ++i; // bad continuation: drop the lead byte and resync
            continue;
        }
        if (!is_control_cp(cp)) { out.append(in, i, len); }
        i += len;
    }
    return out;
}

} // namespace waylaunch
