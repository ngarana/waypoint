// KeyboardLayoutIndicator.hpp - Active keyboard-layout display (Phase 15).
//
// Shows the compact code of the active xkb layout (e.g. "US", "RU") next to a
// keyboard glyph, with the full description in the tooltip. Display-only: a
// Wayland client cannot switch the compositor's layout, so there is no click
// action — it mirrors what the Seat reports.
//
// Bar-only (the backend is null on the lock screen) and self-hides when fewer
// than two layouts are configured — a single-layout keyboard has nothing to
// show. Opt-in via config like every Phase 15 utility.
#pragma once

#include <string>

#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class KeyboardLayout;

class KeyboardLayoutIndicator : public StatusIndicator {
public:
    explicit KeyboardLayoutIndicator(const SystemBackends& backends);

    std::string icon() const override;
    std::string themedIcon() const override;
    std::string label() const override;
    std::string tooltip() const override;

    void onBackendUpdate() override;

private:
    KeyboardLayout* backend_ = nullptr;
};

}  // namespace qypr
