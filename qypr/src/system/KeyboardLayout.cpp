// KeyboardLayout.cpp - Active keyboard-layout state + short-label derivation.
#include "system/KeyboardLayout.hpp"

#include <cctype>

namespace qypr {

void KeyboardLayout::update(const std::string& name, uint32_t index, uint32_t count) {
    // Skip redundant pushes: modifiers events fire on every Shift/Ctrl press,
    // but the layout rarely changes — only repaint when something actually did.
    if (available_ && name == name_ && index == index_ && count == count_) return;
    name_ = name;
    index_ = index;
    count_ = count;
    available_ = true;
    if (onChange_) onChange_();
}

std::string KeyboardLayout::shortLabel(const std::string& xkbName) {
    // Prefer a parenthetical code: xkb descriptions carry the country/variant
    // there — "English (US)" → "US", "English (UK)" → "UK".
    auto open = xkbName.rfind('(');
    if (open != std::string::npos) {
        auto close = xkbName.find(')', open + 1);
        if (close != std::string::npos) {
            std::string inner = xkbName.substr(open + 1, close - open - 1);
            auto b = inner.find_first_not_of(' ');
            auto e = inner.find_last_not_of(' ');
            if (b != std::string::npos) inner = inner.substr(b, e - b + 1);
            if (inner.size() >= 2 && inner.size() <= 3) {
                for (char& c : inner)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                return inner;
            }
        }
    }
    // Fallback: first two letters of the description, uppercased ("Russian" →
    // "RU"). A heuristic — the full name is always in the tooltip.
    std::string out;
    for (char c : xkbName) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (out.size() == 2) break;
        }
    }
    if (out.empty()) return "??";
    return out;
}

}  // namespace qypr
