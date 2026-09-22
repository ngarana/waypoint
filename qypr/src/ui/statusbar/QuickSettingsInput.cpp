// QuickSettingsInput.cpp - Hit testing and event routing for the Quick Settings panel.
#include "ui/statusbar/QuickSettingsInput.hpp"

namespace qypr {

bool QuickSettingsInput::handleMotion(Context& ctx, double x, double y) {
    ctx.curX = x;
    ctx.curY = y;
    return ctx.popBounds.contains(x, y);
}

bool QuickSettingsInput::handleClick(Context& ctx, double x, double y) {
    ctx.activeDragTile = nullptr;
    ctx.curX = x;
    ctx.curY = y;

    if (ctx.closeBounds.contains(x, y)) {
        ctx.closeRequested = true;
        return true;
    }

    if (ctx.power != nullptr && ctx.power->bounds.contains(x, y)) {
        ctx.power->onClick(x, y);
        return true;
    }

    if (ctx.volume != nullptr && ctx.volume->bounds.contains(x, y)) {
        ctx.volume->onClick(x, y);
        ctx.activeDragTile = ctx.volume;
        return true;
    }

    for (auto& t : ctx.tiles) {
        if (t && t->bounds.contains(x, y)) {
            t->onClick(x, y);
            if (t->type() == QSTile::Type::Slider) { ctx.activeDragTile = t.get(); }
            return true;
        }
    }

    if (ctx.media != nullptr && ctx.media->bounds.contains(x, y)) {
        ctx.media->onClick(x, y);
        return true;
    }

    if (ctx.wifiCombo != nullptr && ctx.wifiCombo->bounds.contains(x, y)) {
        ctx.wifiCombo->onClick(x, y);
        return true;
    }

    return false;
}

bool QuickSettingsInput::handleSecondaryClick(Context& ctx, double x, double y) {
    ctx.curX = x;
    ctx.curY = y;

    // Right-click is the "more" action: the Wi-Fi tile opens the network
    // picker; every other tile opens its source indicator's detail popover
    // (attached by StatusBar at createTile time) or does nothing.
    if (ctx.wifiCombo != nullptr && ctx.wifiCombo->bounds.contains(x, y)) {
        if (ctx.onOpenWifi) { ctx.onOpenWifi(); }
        return true;
    }

    for (auto& t : ctx.tiles) {
        if (t && t->bounds.contains(x, y)) {
            t->onSecondaryClick(x, y);
            return true;
        }
    }

    return true;  // swallow right-clicks inside the panel (never dismiss)
}

bool QuickSettingsInput::handleDrag(Context& ctx, double x, double y) {
    ctx.curX = x;
    ctx.curY = y;
    if (ctx.activeDragTile != nullptr) {
        ctx.activeDragTile->onDrag(x, y);
        return true;
    }
    return false;
}

bool QuickSettingsInput::handleScroll(Context& ctx, double dx, double dy) {
    if (ctx.volume != nullptr && ctx.volume->bounds.contains(ctx.curX, ctx.curY) &&
        ctx.volume->onScroll(dx, dy)) {
        return true;
    }
    for (auto& t : ctx.tiles) {
        if (t && t->bounds.contains(ctx.curX, ctx.curY) && t->onScroll(dx, dy)) { return true; }
    }
    return false;
}

bool QuickSettingsInput::handleKey(Context& ctx, uint32_t keysym) {
    QSTile* target = nullptr;
    if ((ctx.activeDragTile != nullptr) && (ctx.activeDragTile->type() == QSTile::Type::Slider ||
                                            ctx.activeDragTile->type() == QSTile::Type::Volume)) {
        target = ctx.activeDragTile;
    } else {
        for (auto& t : ctx.tiles) {
            if (t && t->type() == QSTile::Type::Slider && t->bounds.contains(ctx.curX, ctx.curY)) {
                target = t.get();
                break;
            }
        }
    }
    if ((target != nullptr) && target->handleKey(keysym)) { return true; }
    return false;
}

}  // namespace qypr
