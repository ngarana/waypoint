// StatusBarInput.hpp - Pointer, scroll, and keyboard routing for the status bar.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Types.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/StatusBarLayout.hpp"

namespace qypr {

class IndicatorHost;
class Invalidator;
class PopoverManager;
class QuickSettingsPanel;
class StatusIndicator;
class TooltipController;

class StatusBarInput {
public:
    struct Context {
        IndicatorHost& indicators;
        PopoverManager& popovers;
        TooltipController& tooltips;
        QuickSettingsPanel& qsPanel;
        Invalidator& host;
        const BarGeometry& geom;
        const Rect& bounds;
        const Rect& rightGroupBounds;
        const Rect& contentBounds;
        const theme::State& theme;
        bool sessionContentVisible;
        std::function<void()> onToggleQuickSettings;
        std::function<void(StatusIndicator&)> onActivateIndicator;
        std::function<void()> onResetAutoDismiss;
    };

    // Pure hit-test of a zone: returns the first shown indicator whose bounds contain (x, y).
    // If requireInteractive is true, non-interactive indicators on the lock screen are skipped.
    static StatusIndicator* hitTest(const std::vector<std::unique_ptr<StatusIndicator>>& zone,
                                    double x, double y, bool sessionContentVisible,
                                    bool requireInteractive = false);

    bool handlePointerMotion(Context& ctx, double x, double y, int64_t now);
    bool handlePointerButton(Context& ctx, double x, double y, uint32_t button, bool pressed,
                             int64_t now);
    void handlePointerLeave(Context& ctx, int64_t now);
    bool handleScroll(Context& ctx, double x, double y, double dx, double dy);
    bool handleKey(Context& ctx, uint32_t keysym);
    bool handleTextInput(Context& ctx, const std::string& utf8);
    bool wantsKeyboard(const Context& ctx) const;

    bool cycleFocus(Context& ctx, bool reverse);
    void clearFocus(Context& ctx);
    bool hasFocusedChild(const Context& ctx) const;

    double lastPointerX() const { return lastPtrX_; }
    double lastPointerY() const { return lastPtrY_; }
    bool isPopoverDragging() const { return popoverDragging_; }

private:
    double lastPtrX_ = -1.0;
    double lastPtrY_ = -1.0;
    bool popoverDragging_ = false;
};

}  // namespace qypr
