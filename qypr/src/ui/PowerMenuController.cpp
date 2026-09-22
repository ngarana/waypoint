#include "ui/PowerMenuController.hpp"

#include <cmath>

#include "core/Interfaces.hpp"
#include "power/SystemActions.hpp"
#include "render/Painter.hpp"

namespace qypr {

namespace {
struct PowerActionConfig {
    const char* icon;
    const char* label;
    const char* confirmLabel;
};

constexpr std::array<PowerActionConfig, LockLayout::kNumPowerActions> kPowerActions = {{
    {.icon = "⏾", .label = "Suspend", .confirmLabel = "Suspend"},
    {.icon = "󰒲", .label = "Hibernate", .confirmLabel = "Hibernate"},
    {.icon = "↻", .label = "Reboot", .confirmLabel = "Reboot"},
    {.icon = "⏼", .label = "Shutdown", .confirmLabel = "Shut down"},
}};
}  // namespace

PowerMenuController::PowerMenuController(Invalidator& host, SystemActions& power,
                                         const LockLayout& layout)
    : host_(host),
      power_(power),
      layout_(layout) {
    size_t index = 0;
    for (auto& button : powerButtons_) {
        const auto& config = kPowerActions.at(index++);
        button.icon = config.icon;
        button.label = config.label;
        button.diameter = LockLayout::kButtonDiameter;
        button.onClick = [this, index] {
            showConfirm(static_cast<int>(index - 1));
        };
    }

    alwaysPower_.icon = "⏻";
    alwaysPower_.label = "Power";
    alwaysPower_.diameter = LockLayout::kButtonDiameter;
    alwaysPower_.onClick = [this] {
        toggle();
    };
    this->layout(width_, height_);
}

void PowerMenuController::setTheme(const theme::State& state) {
    theme::ThemeAware::setTheme(state);
    for (auto& button : powerButtons_) { button.setTheme(state); }
    alwaysPower_.setTheme(state);
    confirmPopover_.setTheme(state);
}

void PowerMenuController::layout(int width, int height) {
    width_ = width;
    height_ = height;
    int index = 0;
    for (auto& button : powerButtons_) {
        button.bounds = layout_.powerButtonRect(index++, width, height);
    }
    alwaysPower_.bounds = layout_.powerAnchorRect(width, height);
}

void PowerMenuController::expand() {
    if (powerExpanded_) { return; }
    powerExpanded_ = true;
    powerExpandAnim_.animateTo(1.0, theme().anim.medium, ease::inOutQuad);
    host_.invalidate();
}

void PowerMenuController::collapse() {
    if (!powerExpanded_) { return; }
    powerExpanded_ = false;
    powerExpandAnim_.animateTo(0.0, theme().anim.medium, ease::inOutQuad);
    host_.invalidate();
}

void PowerMenuController::toggle() {
    if (powerExpanded_) {
        collapse();
    } else {
        expand();
    }
}

void PowerMenuController::handleMotion(double x, double y, int64_t now) {
    for (auto& button : powerButtons_) {
        button.setHovered(powerExpanded_ && button.contains(x, y), now);
    }
    alwaysPower_.setHovered(alwaysPower_.contains(x, y), now);
    host_.invalidate();
}

void PowerMenuController::handleModalMotion(double x, double y, int64_t now) {
    confirmPopover_.updateHover(x, y, now);
    host_.invalidate();
}

bool PowerMenuController::handlePress(double x, double y, int64_t /*now*/) {
    for (auto& button : powerButtons_) {
        if (powerExpanded_ && button.contains(x, y)) {
            button.click();
            return true;
        }
    }
    if (alwaysPower_.contains(x, y)) {
        alwaysPower_.click();
        return true;
    }
    return false;
}

void PowerMenuController::handleModalPress(double x, double y, int64_t now) {
    confirmPopover_.handlePress(x, y, now);
    host_.invalidate();
}

void PowerMenuController::dismissModal() {
    confirmPopover_.dismiss();
    host_.invalidate();
}

void PowerMenuController::confirmModal() {
    confirmPopover_.confirm();
    host_.invalidate();
}

void PowerMenuController::handleLeave(int64_t now) {
    for (auto& button : powerButtons_) { button.setHovered(false, now); }
    alwaysPower_.setHovered(false, now);
    host_.invalidate();
}

void PowerMenuController::showConfirm(int index) {
    if (index < 0 || index >= LockLayout::kNumPowerActions) { return; }

    std::function<void()> action;
    switch (index) {
        case 0:
            action = [this] {
                power_.suspend();
            };
            break;
        case 1:
            action = [this] {
                power_.hibernate();
            };
            break;
        case 2:
            action = [this] {
                power_.reboot();
            };
            break;
        case 3:
            action = [this] {
                power_.shutdown();
            };
            break;
        default:
            return;
    }

    const Rect anchor = layout_.powerAnchorRect(width_, height_);
    const Rect fullColumn = layout_.powerRowRect(width_, height_);
    const auto& config = kPowerActions.at(static_cast<size_t>(index));
    confirmPopover_.show(config.icon, config.label, config.confirmLabel, std::move(action), anchor,
                         fullColumn.x);
    collapse();
    host_.invalidate();
}

void PowerMenuController::draw(Painter& p, int64_t now, double reveal) {
    const double pe = clamp01(powerExpandAnim_.value(now));
    const Rect fullColumn = layout_.powerRowRect(width_, height_);
    const Rect anchor = layout_.powerAnchorRect(width_, height_);

    const double collapsedHeight = anchor.h + (LockLayout::kPillPad * 2.0);
    const double expandedHeight = fullColumn.h;
    const double pillHeight = lerp(collapsedHeight, expandedHeight, pe);
    const double pillBottom = fullColumn.y + fullColumn.h;
    const Rect pillRect{
        .x = fullColumn.x, .y = pillBottom - pillHeight, .w = fullColumn.w, .h = pillHeight};
    const double pillCorner = fullColumn.w / 2.0;
    const double pillAlpha = lerp(0.7, 1.0, clamp01(reveal));

    alwaysPower_.icon = powerExpanded_ ? "✕" : "⏻";
    p.pushGroup();

    cairo_t* cr = p.cr();
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_arc(cr, pillRect.x + pillCorner, pillRect.y + pillCorner, pillCorner, M_PI,
              3.0 * M_PI / 2.0);
    cairo_arc(cr, pillRect.x + pillRect.w - pillCorner, pillRect.y + pillCorner, pillCorner,
              3.0 * M_PI / 2.0, 0.0);
    cairo_arc(cr, pillRect.x + pillRect.w - pillCorner, pillRect.y + pillRect.h - pillCorner,
              pillCorner, 0.0, M_PI / 2.0);
    cairo_arc(cr, pillRect.x + pillCorner, pillRect.y + pillRect.h - pillCorner, pillCorner,
              M_PI / 2.0, M_PI);
    cairo_close_path(cr);
    cairo_clip(cr);

    p.fillRoundedRect(pillRect, pillCorner, theme().colors.glass);
    p.strokeRoundedRect(pillRect, pillCorner, theme().colors.glassBorder, 1.0);

    if (pe > 0.01) {
        p.pushGroup();
        for (auto& button : powerButtons_) { button.draw(p, now); }
        p.popGroupWithAlpha(clamp01(pe));
    }
    alwaysPower_.draw(p, now);

    cairo_restore(cr);
    p.popGroupWithAlpha(pillAlpha);

    if (confirmPopover_.active()) { confirmPopover_.draw(p, width_, height_, now); }
}

bool PowerMenuController::animating(int64_t now) const {
    if (powerExpandAnim_.active(now)) { return true; }
    for (const auto& button : powerButtons_) {
        if (button.animating(now)) { return true; }
    }
    if (alwaysPower_.animating(now)) { return true; }
    return confirmPopover_.animating(now);
}

}  // namespace qypr
