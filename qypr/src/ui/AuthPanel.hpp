// AuthPanel.hpp - The lock screen's authentication card.
//
// The redesign mock (docs/mock/code.html) puts the whole authentication
// affordance into one compact block: an "[ AUTH // ... ]" header, the
// operator identity with a biometric affordance, a single-line input notch,
// and a status line. qypr expresses that block as one glass card so the
// reveal animation, the focus treatment and the status line share a surface.
//
// The panel never sees the secret: it renders the typed character count only,
// exactly like the password field it replaces.

#pragma once

#include <string>

#include "ui/StatusMessage.hpp"
#include "ui/Widget.hpp"

namespace qypr {

class Painter;

class AuthPanel : public Widget {
public:
    // Identity shown in the card.
    std::string operatorId;
    std::string node;

    // The PAM stack the password is checked against ("qypr-lock", "login").
    std::string pamService;

    // Biometric state: the tile mirrors the fingerprint worker.
    bool fingerprintActive = false;
    bool fingerprintError = false;

    // Typed characters (the secret itself stays in the controller).
    int charCount = 0;

    // The status line inside the card's footer.
    StatusMessage status;

    Size measure(Painter& p, double width) const;
    void draw(Painter& p, int64_t now) override;

    // The inline UNLOCK affordance. The rect is valid after draw(), the same
    // way the notification cards cache their hit-test layout.
    bool unlockHit(double x, double y) const { return unlock_.contains(x, y); }
    const Rect& unlockButton() const { return unlock_; }
    void setUnlockHovered(bool hovered) { unlockHovered_ = hovered; }

private:
    Rect unlock_{};
    bool unlockHovered_ = false;
};

}  // namespace qypr
