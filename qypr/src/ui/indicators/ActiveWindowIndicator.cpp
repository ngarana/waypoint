// ActiveWindowIndicator.cpp - Focused window bar widget implementation.
#include "ui/indicators/ActiveWindowIndicator.hpp"

#include "system/ToplevelBackend.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

namespace {
// Cap the displayed length so a long title cannot dominate the bar. Counts
// UTF-8 codepoints (not bytes) so multibyte titles are never cut mid-character.
std::string truncateUtf8(const std::string& s, size_t maxCps) {
    size_t cps = 0, i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = (c < 0x80) ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        if (cps + 1 > maxCps) return s.substr(0, i) + "…";
        i += len;
        ++cps;
    }
    return s;
}
}  // namespace

ActiveWindowIndicator::ActiveWindowIndicator(const SystemBackends& backends)
    : StatusIndicator("active-window", Zone::Center, 0),
      backend_(backends.toplevel) {
    visible = false;  // hidden until a window is focused
}

std::string ActiveWindowIndicator::label() const {
    if (!snap_.hasActive) return "";
    const std::string& s = !snap_.title.empty() ? snap_.title : snap_.appId;
    return truncateUtf8(s, 60);
}

std::string ActiveWindowIndicator::tooltip() const {
    if (!snap_.hasActive) return "";
    if (!snap_.appId.empty() && !snap_.title.empty()) return snap_.appId + " — " + snap_.title;
    return !snap_.title.empty() ? snap_.title : snap_.appId;
}

void ActiveWindowIndicator::onBackendUpdate() {
    if (!backend_) {
        visible = false;
        return;
    }
    snap_ = backend_->snapshot();
    visible = snap_.available && snap_.hasActive && !(snap_.title.empty() && snap_.appId.empty());
}

REGISTER_INDICATOR("active-window", Zone::Center, 0, ActiveWindowIndicator)

}  // namespace qypr
