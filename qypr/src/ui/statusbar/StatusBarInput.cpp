// StatusBarInput.cpp - Pointer, scroll, and keyboard routing for the status bar.
#include "ui/statusbar/StatusBarInput.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <utility>

#include "core/Interfaces.hpp"
#include "ui/statusbar/IndicatorHost.hpp"
#include "ui/statusbar/PopoverManager.hpp"
#include "ui/statusbar/QuickSettingsPanel.hpp"
#include "ui/statusbar/StatusIndicator.hpp"
#include "ui/statusbar/TooltipController.hpp"

namespace qypr {

namespace {

bool isShown(const StatusIndicator& ind, bool sessionContentVisible) {
    return ind.visible && !ind.qsOnly() && (!ind.sensitive() || sessionContentVisible);
}

bool isInteractive(const StatusIndicator& ind, bool sessionContentVisible) {
    return sessionContentVisible || ind.lockInteractive();
}

}  // namespace

StatusIndicator* StatusBarInput::hitTest(const std::vector<std::unique_ptr<StatusIndicator>>& zone,
                                         double x, double y, bool sessionContentVisible,
                                         bool requireInteractive) {
    for (auto& ind : zone) {
        if (!isShown(*ind, sessionContentVisible)) { continue; }
        if (requireInteractive && !isInteractive(*ind, sessionContentVisible)) { continue; }
        if (ind->bounds.contains(x, y)) { return ind.get(); }
    }
    return nullptr;
}

bool StatusBarInput::handlePointerMotion(Context& ctx, double x, double y, int64_t now) {
    lastPtrX_ = x;
    lastPtrY_ = y;
    if (ctx.popovers.active() == &ctx.qsPanel && (ctx.qsPanel.activeDragTile_ != nullptr)) {
        ctx.popovers.handleDrag(x, y);
        ctx.host.invalidate();
        return true;
    }
    if (popoverDragging_ && (ctx.popovers.active() != nullptr) &&
        ctx.popovers.active() != &ctx.qsPanel) {
        ctx.popovers.handleDrag(x, y);
        if (ctx.onResetAutoDismiss) { ctx.onResetAutoDismiss(); }
        ctx.host.invalidate();
        return true;
    }
    // An open popover hides the hover-tooltip.
    if (ctx.popovers.active() != nullptr) {
        ctx.tooltips.clear();
        if (ctx.popovers.handleMotion(x, y)) {
            ctx.host.invalidate();
            return true;
        }
    }

    StatusIndicator* newTarget = nullptr;
    auto checkHover = [&](auto& list) {
        for (auto& ind : list) {
            bool const prev = ind->hovered;
            ind->hovered = isShown(*ind, ctx.sessionContentVisible) && ind->bounds.contains(x, y);
            if (ind->hovered) { newTarget = ind.get(); }
            if (ind->hovered != prev) { ctx.host.invalidate(); }
        }
    };
    checkHover(ctx.indicators.left());
    checkHover(ctx.indicators.center());
    checkHover(ctx.indicators.right());

    if (ctx.tooltips.setTarget(newTarget, now)) {
        ctx.host.invalidate();
    } else if (newTarget != nullptr) {
        ctx.host.invalidate();  // keep fade ticking
    }

    return ctx.bounds.contains(x, y) ||
           ((ctx.popovers.active() != nullptr) && ctx.popovers.active()->contains(x, y));
}

bool StatusBarInput::handlePointerButton(Context& ctx, double x, double y, uint32_t button,
                                         bool pressed, int64_t now) {
    (void)now;
    constexpr uint32_t kBtnLeft = 0x110;
    constexpr uint32_t kBtnRight = 0x111;
    constexpr uint32_t kBtnMiddle = 0x112;
    if (pressed) { ctx.tooltips.clear(); }

    if (ctx.popovers.active() != nullptr) {
        if (ctx.popovers.active()->contains(x, y)) {
            if (pressed) {
                const bool isQS = ctx.popovers.active() == &ctx.qsPanel;
                if (button == kBtnRight) {
                    ctx.popovers.handleSecondaryClick(x, y);
                    if ((ctx.popovers.active() != nullptr) &&
                        ctx.popovers.active()->consumeCloseRequest()) {
                        ctx.popovers.closeActive();
                    }
                } else {
                    ctx.popovers.handleClick(x, y);
                    if ((ctx.popovers.active() != nullptr) &&
                        ctx.popovers.active()->consumeCloseRequest()) {
                        ctx.popovers.closeActive();
                    } else if (!isQS) {
                        popoverDragging_ = true;
                    }
                }
                if (ctx.onResetAutoDismiss) { ctx.onResetAutoDismiss(); }
            } else {
                popoverDragging_ = false;
                if (ctx.popovers.active() == &ctx.qsPanel) {
                    ctx.qsPanel.activeDragTile_ = nullptr;
                }
            }
            ctx.host.invalidate();
            return true;
        }
        if (pressed) {
            popoverDragging_ = false;
            ctx.popovers.closeActive();
            ctx.host.invalidate();
            return true;
        }
    }

    if (!pressed) { return false; }

    if (button == kBtnLeft) {
        // Right-zone indicators with detailed views open their own popover first.
        for (auto& ind : ctx.indicators.right()) {
            if (!isShown(*ind, ctx.sessionContentVisible) ||
                !isInteractive(*ind, ctx.sessionContentVisible) || !ind->bounds.contains(x, y)) {
                continue;
            }
            if (ind->hasDetailedView()) {
                if (ctx.onActivateIndicator) { ctx.onActivateIndicator(*ind); }
                ctx.host.invalidate();
                return true;
            }
        }
        for (auto& ind : ctx.indicators.right()) {
            if (!isShown(*ind, ctx.sessionContentVisible) ||
                !isInteractive(*ind, ctx.sessionContentVisible) || !ind->bounds.contains(x, y)) {
                continue;
            }
            if (ind->onClick(x, y)) {
                ctx.host.invalidate();
                return true;
            }
            return true;
        }
    }

    auto checkClick = [&](auto& list) {
        for (auto& ind : list) {
            if (!isShown(*ind, ctx.sessionContentVisible) ||
                !isInteractive(*ind, ctx.sessionContentVisible) || !ind->bounds.contains(x, y)) {
                continue;
            }
            if (button == kBtnRight) {
                ind->onSecondaryClick(x, y);
            } else if (button == kBtnMiddle) {
                ind->onMiddleClick(x, y);
            } else if (!ind->onClick(x, y)) {
                if (ctx.onActivateIndicator) { ctx.onActivateIndicator(*ind); }
            }
            ctx.host.invalidate();
            return true;
        }
        return false;
    };
    if (checkClick(ctx.indicators.left())) { return true; }
    if (checkClick(ctx.indicators.center())) { return true; }
    if (checkClick(ctx.indicators.right())) { return true; }

    return ctx.bounds.contains(x, y);
}

void StatusBarInput::handlePointerLeave(Context& ctx, int64_t now) {
    (void)now;
    lastPtrX_ = lastPtrY_ = -1.0;
    popoverDragging_ = false;
    if (ctx.popovers.active() == &ctx.qsPanel) { ctx.popovers.closeActive(); }
    ctx.indicators.forEach([&](StatusIndicator& ind) { ind.hovered = false; });
    ctx.tooltips.clear();
    ctx.host.invalidate();
}

bool StatusBarInput::handleScroll(Context& ctx, double x, double y, double dx, double dy) {
    if ((ctx.popovers.active() != nullptr) && ctx.popovers.active()->contains(x, y)) {
        if (ctx.onResetAutoDismiss) { ctx.onResetAutoDismiss(); }
        return ctx.popovers.handleScroll(dx, dy);
    }

    // Scroll-to-adjust on the right zone only (lock allow-list path).
    for (auto& ind : ctx.indicators.right()) {
        if (isShown(*ind, ctx.sessionContentVisible) &&
            isInteractive(*ind, ctx.sessionContentVisible) && ind->bounds.contains(x, y) &&
            ind->onScroll(dx, dy, x, y)) {
            ctx.host.invalidate();
            return true;
        }
    }
    return false;
}

bool StatusBarInput::handleKey(Context& ctx, uint32_t keysym) {
    if (ctx.popovers.active() != nullptr) {
        if (keysym == XKB_KEY_Escape) {
            ctx.popovers.closeActive();
            ctx.host.invalidate();
            return true;
        }
        const bool isQS = (ctx.popovers.active() == &ctx.qsPanel);
        const bool handled = ctx.popovers.handleKey(keysym);
        if ((ctx.popovers.active() != nullptr) && ctx.popovers.active()->consumeCloseRequest()) {
            ctx.popovers.closeActive();
        } else if (isQS && !handled) {
            ctx.popovers.closeActive();
        } else if (handled) {
            if (ctx.onResetAutoDismiss) { ctx.onResetAutoDismiss(); }
        }
        ctx.host.invalidate();
        return handled || isQS;
    }

    if (!hasFocusedChild(ctx)) { return false; }
    switch (keysym) {
        case XKB_KEY_Escape:
            clearFocus(ctx);
            return true;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
        case XKB_KEY_space: {
            if (auto* focused = ctx.indicators.focused()) {
                if (ctx.onActivateIndicator) { ctx.onActivateIndicator(*focused); }
            }
            return true;
        }
        case XKB_KEY_Left:
            return cycleFocus(ctx, true);
        case XKB_KEY_Right:
            return cycleFocus(ctx, false);
    }
    return false;
}

bool StatusBarInput::handleTextInput(Context& ctx, const std::string& utf8) {
    return ctx.popovers.handleText(utf8);
}

bool StatusBarInput::wantsKeyboard(const Context& ctx) const {
    return ctx.popovers.activeWantsKeyboard();
}

bool StatusBarInput::cycleFocus(Context& ctx, bool reverse) {
    std::vector<StatusIndicator*> inds;
    ctx.indicators.forEach([&](StatusIndicator& ind) {
        if (isShown(ind, ctx.sessionContentVisible)) { inds.push_back(&ind); }
    });
    if (inds.empty()) { return false; }

    int current = -1;
    for (size_t i = 0; i < inds.size(); ++i) {
        if (inds[i]->focused) {
            current = static_cast<int>(i);
            break;
        }
    }

    if (current == -1) {
        int const target = reverse ? static_cast<int>(inds.size()) - 1 : 0;
        inds[target]->focused = true;
        ctx.host.invalidate();
        return true;
    }

    inds[current]->focused = false;
    int const next = current + (reverse ? -1 : 1);
    if (next >= 0 && std::cmp_less(next, inds.size())) {
        inds[next]->focused = true;
        ctx.host.invalidate();
        return true;
    }

    ctx.host.invalidate();
    return false;
}

void StatusBarInput::clearFocus(Context& ctx) {
    ctx.indicators.forEach([](StatusIndicator& ind) { ind.focused = false; });
    ctx.host.invalidate();
}

bool StatusBarInput::hasFocusedChild(const Context& ctx) const {
    bool any = false;
    const_cast<IndicatorHost&>(ctx.indicators).forEach([&](StatusIndicator& ind) {
        if (ind.focused) { any = true; }
    });
    return any;
}

}  // namespace qypr
