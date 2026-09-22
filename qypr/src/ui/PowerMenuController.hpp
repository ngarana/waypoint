// PowerMenuController.hpp - Lock-safe power selection and confirmation policy.

#pragma once

#include <array>
#include <functional>

#include "ui/ActionButton.hpp"
#include "ui/ConfirmPopover.hpp"
#include "ui/LockLayout.hpp"

namespace qypr {

class Painter;
class Invalidator;
class SystemActions;

// The only object in the lock UI that can reach fixed system power actions.
// It owns the explicit action allow-list and always requires confirmation;
// timeout and outside clicks fail closed in ConfirmPopover.
class PowerMenuController : public theme::ThemeAware {
public:
    PowerMenuController(Invalidator& host, SystemActions& power, const LockLayout& layout);

    void setTheme(const theme::State& state) override;
    void layout(int width, int height);

    void expand();
    void collapse();
    void toggle();
    bool expanded() const { return powerExpanded_; }
    bool modalActive() const { return confirmPopover_.active(); }

    void handleMotion(double x, double y, int64_t now);
    void handleModalMotion(double x, double y, int64_t now);
    bool handlePress(double x, double y, int64_t now);
    void handleModalPress(double x, double y, int64_t now);
    void dismissModal();
    void confirmModal();
    void handleLeave(int64_t now);

    void draw(Painter& p, int64_t now, double reveal);
    bool animating(int64_t now) const;

private:
    void showConfirm(int index);

    Invalidator& host_;
    SystemActions& power_;
    const LockLayout& layout_;
    int width_ = 1920;
    int height_ = 1080;
    std::array<ActionButton, LockLayout::kNumPowerActions> powerButtons_;
    ActionButton alwaysPower_;
    ConfirmPopover confirmPopover_;
    bool powerExpanded_ = false;
    Animated powerExpandAnim_{0};
};

}  // namespace qypr
