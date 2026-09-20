// StatusBar.cpp - Container for zones, indicators, popovers, and layout implementation
#include "ui/statusbar/StatusBar.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <ranges>

#include <algorithm>

#include <utility>

#include "core/EventLoop.hpp"
#include "core/Interfaces.hpp"
#include "core/Types.hpp"
#include "render/Painter.hpp"
#include "mpris/MprisController.hpp"
#include "system/BatteryBackend.hpp"
#include "system/BluetoothBackend.hpp"
#include "system/BrightnessBackend.hpp"
#include "system/DndState.hpp"
#include "system/IdleInhibitor.hpp"
#include "system/KeyboardLayout.hpp"
#include "system/SNIBackend.hpp"
#include "system/ToplevelBackend.hpp"
#include "system/VolumeBackend.hpp"
#include "system/WifiBackend.hpp"
#include "system/WorkspaceBackend.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"

namespace qypr {

namespace {
// Translucent menu-bar backdrop. The tint colour and its alpha come from the
// theme at draw time (theme::statusbar::barTint / barTintAlpha, both derived
// from the active palette); a config-supplied `bar.backdrop` value overrides the
// alpha (used by qypr-bar's standalone config). The bar itself is a single
// translucent slab; each indicator is a separate interactive item (no
// right-group chip).
}  // namespace

StatusBar::StatusBar(EventLoop& loop, Invalidator& host, const SystemBackends& backends,
                     const IndicatorRegistry::ModuleSelection* sel)
    : loop_(loop),
      host_(host),
      sessionSurface_(backends.sessionSurface),
      wifi_(backends.wifi) {
    // A bar preview and the lock screen can coexist in the same test process;
    // do not let the standalone bar's nested-surface alpha leak into a new
    // lock-screen StatusBar before its host configures the bar backdrop.
    theme::statusbar::panelSurfaceAlpha = 1.0;

    // Create indicators: the config-selected set when the host supplied one,
    // otherwise every registered indicator (the lock screen's behaviour).
    auto all = IndicatorRegistry::instance().createAll(backends, sel);
    for (auto& ind : all) {
        if (ind->zone() == Zone::Left) {
            leftIndicators_.push_back(std::move(ind));
        } else if (ind->zone() == Zone::Center) {
            centerIndicators_.push_back(std::move(ind));
        } else {
            rightIndicators_.push_back(std::move(ind));
        }
    }

    // Populate Quick Settings tiles from indicators
    for (const auto& ind : leftIndicators_) {
        if (auto tile = ind->createTile()) {
            // Right-click on a QS tile opens its source indicator's detail
            // popover (the same view the bar icon opens).
            tile->setOnSecondary([this, id = ind->id()] { openIndicatorDetail(id); });
            qsPanel_.addTile(std::move(tile));
        }
    }
    for (const auto& ind : centerIndicators_) {
        if (auto tile = ind->createTile()) {
            // Right-click on a QS tile opens its source indicator's detail
            // popover (the same view the bar icon opens).
            tile->setOnSecondary([this, id = ind->id()] { openIndicatorDetail(id); });
            qsPanel_.addTile(std::move(tile));
        }
    }
    for (const auto& ind : rightIndicators_) {
        if (auto tile = ind->createTile()) {
            // Right-click on a QS tile opens its source indicator's detail
            // popover (the same view the bar icon opens).
            tile->setOnSecondary([this, id = ind->id()] { openIndicatorDetail(id); });
            qsPanel_.addTile(std::move(tile));
        }
    }

    // Build internal QS panel tiles (header, power, wifi combo, volume, media).
    // The header power button runs `waylaunch --power` directly; the Wi-Fi
    // tile body opens the network picker.
    //
    // Only on the session surface (QL-7): two of these tiles run compiled-in
    // shell commands (`waylaunch --power`, the screenshot helper), and the lock
    // process must not even hold them — a locked machine cannot open Quick
    // Settings at all once the interaction policy below is applied, but the tiles
    // should not exist rather than be unreachable.
    if (sessionSurface_) {
        qsPanel_.buildTiles(loop_, backends, [this]() { openIndicatorDetail("wifi"); });
    }

    // Backends push; every push fans out to the indicators (they filter by
    // their own backend pointer) and triggers a repaint.
    if (backends.battery != nullptr) {
        backends.battery->setOnChange([this] { notifyBackendUpdate(); });
    }
    if (backends.brightness != nullptr) {
        backends.brightness->setOnChange([this] { notifyBackendUpdate(); });
    }
    if (backends.wifi != nullptr) {
        backends.wifi->setOnChange([this] { notifyBackendUpdate(); });
    }
    if (backends.bluetooth != nullptr) {
        backends.bluetooth->setOnChange([this] { notifyBackendUpdate(); });
    }
    if (backends.volume != nullptr) {
        backends.volume->setOnChange([this] { notifyBackendUpdate(); });
    }
    if (backends.sni != nullptr) {
        backends.sni->setOnChange([this] { notifyBackendUpdate(); });
    }
    if (backends.mpris != nullptr) {
        backends.mpris->setOnChange([this] { notifyBackendUpdate(); });
    }
    if (backends.idleInhibitor != nullptr) {
        backends.idleInhibitor->setOnChange([this] { notifyBackendUpdate(); });
    }
    if (backends.keyboardLayout != nullptr) {
        backends.keyboardLayout->setOnChange([this] { notifyBackendUpdate(); });
    }
    // Session-sensitive WM widgets (only ever started by the unlocked bar).
    if (backends.workspace != nullptr) {
        backends.workspace->setOnChange([this] { notifyBackendUpdate(); });
    }
    if (backends.toplevel != nullptr) {
        backends.toplevel->setOnChange([this] { notifyBackendUpdate(); });
    }
    if (backends.dnd != nullptr) {
        backends.dnd->addListener([this] { notifyBackendUpdate(); });
    }

    // The bar's own 1s tick: drives poll() (clock text, animations that
    // depend on wall time) — repaint cadence matches the lockscreen clock.
    tickTimer_ = loop_.addTimer(1000, true, [this] {
        const int64_t now = nowMs();
        for (auto& ind : leftIndicators_) { ind->poll(now); }
        for (auto& ind : centerIndicators_) { ind->poll(now); }
        for (auto& ind : rightIndicators_) { ind->poll(now); }
        host_.invalidate();
    });

    // Offscreen measurement context for layout-time text measuring.
    measureSurface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    measureCr_ = cairo_create(measureSurface_);
}

StatusBar::~StatusBar() {
    if (tickTimer_ >= 0) { loop_.removeTimer(tickTimer_); }
    if (dismissTimer_ >= 0) { loop_.removeTimer(dismissTimer_); }
    if (measureCr_ != nullptr) { cairo_destroy(measureCr_); }
    if (measureSurface_ != nullptr) { cairo_surface_destroy(measureSurface_); }
}

void StatusBar::reloadModules(const SystemBackends& backends,
                              const IndicatorRegistry::ModuleSelection* sel) {
    // Close any open popover (QS, detail, etc.) — the indicators are about to
    // be destroyed, so the popover's backing indicator would dangle.
    popovers_.closeActive();

    // Clear current indicators.
    leftIndicators_.clear();
    centerIndicators_.clear();
    rightIndicators_.clear();

    // Recreate from the new selection.
    auto all = IndicatorRegistry::instance().createAll(backends, sel);
    for (auto& ind : all) {
        if (ind->zone() == Zone::Left) {
            leftIndicators_.push_back(std::move(ind));
        } else if (ind->zone() == Zone::Center) {
            centerIndicators_.push_back(std::move(ind));
        } else {
            rightIndicators_.push_back(std::move(ind));
        }
    }

    // Rebuild QS tiles.
    qsPanel_.clearTiles();
    for (const auto& ind : leftIndicators_) {
        if (auto tile = ind->createTile()) {
            // Right-click on a QS tile opens its source indicator's detail
            // popover (the same view the bar icon opens).
            tile->setOnSecondary([this, id = ind->id()] { openIndicatorDetail(id); });
            qsPanel_.addTile(std::move(tile));
        }
    }
    for (const auto& ind : centerIndicators_) {
        if (auto tile = ind->createTile()) {
            // Right-click on a QS tile opens its source indicator's detail
            // popover (the same view the bar icon opens).
            tile->setOnSecondary([this, id = ind->id()] { openIndicatorDetail(id); });
            qsPanel_.addTile(std::move(tile));
        }
    }
    for (const auto& ind : rightIndicators_) {
        if (auto tile = ind->createTile()) {
            // Right-click on a QS tile opens its source indicator's detail
            // popover (the same view the bar icon opens).
            tile->setOnSecondary([this, id = ind->id()] { openIndicatorDetail(id); });
            qsPanel_.addTile(std::move(tile));
        }
    }
    if (sessionSurface_) {
        qsPanel_.buildTiles(loop_, backends, [this]() { openIndicatorDetail("wifi"); });
    }

    host_.invalidate();
}

void StatusBar::resetAutoDismiss() {
    if (dismissTimer_ >= 0) {
        loop_.removeTimer(dismissTimer_);
        dismissTimer_ = -1;
    }
    DetailedPopover const* p = popovers_.active();
    if (p == nullptr) { return; }
    const int ms = p->autoDismissMs();
    if (ms <= 0) {
        return;  // popover does not opt into auto-dismiss (e.g. QS)
    }
    dismissTimer_ = loop_.addTimer(ms, /*repeat=*/false, [this] {
        dismissTimer_ = -1;  // the loop already removed the one-shot fd
        DetailedPopover const* cur = popovers_.active();
        if (!cur || cur->autoDismissMs() <= 0) {
            return;  // gone or not transient
        }
        // This is an inactivity timer, not a hover timer. A parked pointer is
        // not interaction, so transient popovers close even when the pointer
        // remains over them. Click, scroll, drag, and keyboard paths re-arm it.
        popovers_.closeActive();
        host_.invalidate();
    });
}

void StatusBar::setSessionContentVisible(bool v) {
    if (v == sessionContentVisible_) { return; }
    sessionContentVisible_ = v;
    host_.invalidate();
}

bool StatusBar::hasOpenOverlay() const {
    return popovers_.active() != nullptr || popovers_.isTransitioning();
}

int StatusBar::overlayHeight() const {
    // Height (from the anchored edge) needed to contain the strip and the open
    // popover. The popover sits a 6px gap past the strip; add a little breathing
    // room past its bottom. Uses the tallest *drawing* popover so the surface
    // stays big enough through the close fade, then returns 0 (idle strip) once
    // nothing is drawn. Symmetric for top/bottom bars: the popover always grows
    // away from the anchored edge, so the extent from that edge is the same.
    const double popH = popovers_.maxContentHeight();
    if (popH <= 0.0) { return 0; }
    return static_cast<int>(geom_.edgeMargin + geom_.height + 6.0 + popH + 8.0 + 0.5);
}

void StatusBar::setBackdrop(bool enabled, double alpha) {
    backdrop_ = enabled;
    backdropAlpha_ = alpha;
    // Nested QS tiles and popup cards must use the same opacity as the outer
    // slab; otherwise the panel still looks opaque even when its shell is
    // translucent.
    theme::statusbar::panelSurfaceAlpha =
        enabled ? clamp01(alpha >= 0.0 ? alpha : theme::statusbar::barTintAlpha) : 0.0;
    // Keep every overlay in lock-step with the strip, including the borrowed
    // Quick Settings panel and popovers created later by indicators.
    qsPanel_.setBackdrop(enabled, alpha);
    popovers_.setBackdrop(enabled, alpha);
    host_.invalidate();
}

void StatusBar::setGeometry(const BarGeometry& g) {
    geom_ = g;
    host_.invalidate();  // layout() recomputes bounds on the next frame
}

void StatusBar::notifyBackendUpdate() {
    for (auto& ind : leftIndicators_) { ind->onBackendUpdate(); }
    for (auto& ind : centerIndicators_) { ind->onBackendUpdate(); }
    for (auto& ind : rightIndicators_) { ind->onBackendUpdate(); }
    // The QS Wi-Fi combo tile is setter-fed (not a polled tile); keep it in
    // step with every wifi push — radio state, SSID, and scan progress.
    if (wifi_ != nullptr) { qsPanel_.updateWifi(wifi_->snapshot()); }
    host_.invalidate();
}

void StatusBar::layout(int screenW, int screenH) {
    double const sideMargin = geom_.sideMargin;
    double const barH = geom_.height;
    double const pad = theme::statusbar::padding;
    double const sp = theme::statusbar::iconSpacing;

    // The strip sits `edgeMargin` from the anchored edge. Measuring the bottom
    // edge from screenH (rather than a fixed y) is what makes `position=bottom`
    // work in both host states: on the idle 66px layer-shell strip screenH is
    // the strip itself, and when the surface grows for an overlay screenH is the
    // whole output — the bar stays pinned to the same physical edge either way.
    const double y = geom_.bottom ? screenH - geom_.edgeMargin - barH : geom_.edgeMargin;

    // First pass: lay out indicators at their natural positions within a
    // full-width bounds to measure the content span.
    bounds = {.x = sideMargin, .y = y, .w = screenW - (2 * sideMargin), .h = barH};

    Painter meas(measureCr_);

    // 1. Layout Left Zone (flows right)
    double lx = bounds.x + pad;
    for (auto& ind : leftIndicators_) {
        if (!isShown(*ind)) {
            ind->bounds = {.x = 0, .y = 0, .w = 0, .h = 0};
            continue;
        }
        double const w = ind->measureWidth(meas);
        ind->bounds = {.x = lx, .y = bounds.y, .w = w, .h = barH};
        lx += w + sp;
    }

    // 2. Layout Right Zone (flows left from the bar's right edge).
    //    Each indicator is an independent interactive item.
    //    Clicking an indicator opens its own popover or toggles its state.
    double rx = bounds.x + bounds.w - pad;
    for (auto& rightIndicator : std::views::reverse(rightIndicators_)) {
        if (!isShown(*rightIndicator)) {
            rightIndicator->bounds = {.x = 0, .y = 0, .w = 0, .h = 0};
            continue;
        }
        double const w = rightIndicator->measureWidth(meas);
        rx -= w;
        rightIndicator->bounds = {.x = rx, .y = bounds.y, .w = w, .h = barH};
        rx -= sp;
    }

    // Compute right-group bounds for the backdrop chip (subtle, not a solid filled tile)
    rightGroupBounds_ = {.x = 0, .y = 0, .w = 0, .h = 0};
    double rightMinX = bounds.x + bounds.w;
    double rightMaxX = 0;
    for (const auto& ind : rightIndicators_) {
        if (!isShown(*ind)) { continue; }
        rightMinX = std::min(ind->bounds.x, rightMinX);
        rightMaxX = std::max(ind->bounds.x + ind->bounds.w, rightMaxX);
    }
    if (rightMinX < rightMaxX) {
        rightGroupBounds_ = {.x = rightMinX, .y = bounds.y, .w = rightMaxX - rightMinX, .h = barH};
    }

    // 3. Layout Center Zone
    double totalCenterW = 0;
    int visibleCenter = 0;
    for (auto& ind : centerIndicators_) {
        if (!isShown(*ind)) { continue; }
        totalCenterW += ind->measureWidth(meas);
        ++visibleCenter;
    }
    if (visibleCenter > 1) { totalCenterW += (visibleCenter - 1) * sp; }
    double cx = bounds.x + ((bounds.w - totalCenterW) / 2.0);
    for (auto& ind : centerIndicators_) {
        if (!isShown(*ind)) {
            ind->bounds = {.x = 0, .y = 0, .w = 0, .h = 0};
            continue;
        }
        double const w = ind->measureWidth(meas);
        ind->bounds = {.x = cx, .y = bounds.y, .w = w, .h = barH};
        cx += w + sp;
    }

    // Compute content width from the positioned indicator bounds — the span
    // from the leftmost to the rightmost visible indicator, plus padding.
    // Used to center the content within the full-width surface.
    double contentLeft = bounds.x + bounds.w;  // rightmost possible
    double contentRight = bounds.x;            // leftmost possible
    auto updateContentSpan = [&](const StatusIndicator& ind) {
        if (ind.bounds.w <= 0) { return; }
        contentLeft = std::min(ind.bounds.x, contentLeft);
        double const right = ind.bounds.x + ind.bounds.w;
        contentRight = std::max(right, contentRight);
    };
    for (const auto& ind : leftIndicators_) { updateContentSpan(*ind); }
    for (const auto& ind : rightIndicators_) { updateContentSpan(*ind); }
    for (const auto& ind : centerIndicators_) { updateContentSpan(*ind); }
    int contentW = 0;
    if (contentRight > contentLeft) {
        contentW = static_cast<int>(contentRight - contentLeft + (2 * pad) + 0.5);
    }
    contentW = std::min(contentW, screenW);

    // Default: content fills the full surface.
    contentBounds_ = bounds;

    // Center the bar content within the full-width surface. bounds stays at
    // full surface width (for popover anchoring); only the indicator positions
    // are shifted so the content is visually centered.
    if (contentW > 0 && contentW < static_cast<int>(screenW - (2 * sideMargin))) {
        const double offset = (screenW - (2 * sideMargin) - contentW) / 2.0;
        contentBounds_ = {
            .x = bounds.x + offset, .y = bounds.y, .w = static_cast<double>(contentW), .h = barH};

        // Re-layout left indicators starting from the centered content edge.
        double newLx = bounds.x + offset + pad;
        for (auto& ind : leftIndicators_) {
            if (!isShown(*ind)) { continue; }
            ind->bounds.x = newLx;
            newLx += ind->bounds.w + sp;
        }

        // Re-layout right indicators ending at the centered content edge.
        double newRx = bounds.x + offset + contentW - pad;
        for (auto& rightIndicator : std::views::reverse(rightIndicators_)) {
            if (!isShown(*rightIndicator)) { continue; }
            newRx -= rightIndicator->bounds.w;
            rightIndicator->bounds.x = newRx;
            newRx -= sp;
        }

        // Update right-group bounds to match centered positions.
        rightGroupBounds_ = {.x = 0, .y = 0, .w = 0, .h = 0};
        rightMinX = bounds.x + bounds.w;
        rightMaxX = 0;
        for (const auto& ind : rightIndicators_) {
            if (!isShown(*ind)) { continue; }
            rightMinX = std::min(ind->bounds.x, rightMinX);
            rightMaxX = std::max(ind->bounds.x + ind->bounds.w, rightMaxX);
        }
        if (rightMinX < rightMaxX) {
            rightGroupBounds_ = {
                .x = rightMinX, .y = bounds.y, .w = rightMaxX - rightMinX, .h = barH};
        }

        // Re-center the center zone within the centered content area.
        double newCx = bounds.x + offset + ((contentW - totalCenterW) / 2.0);
        for (auto& ind : centerIndicators_) {
            if (!isShown(*ind)) { continue; }
            ind->bounds.x = newCx;
            newCx += ind->bounds.w + sp;
        }
    }

    // 4. Anchor active popovers
    //    right edge of the bar, opening away from the anchored screen edge.
    if (popovers_.active() != nullptr) {
        DetailedPopover* p = popovers_.active();
        if (p == &qsPanel_ || p->contentWidth() >= 300.0) {
            // Anchor to the rightmost visible indicator, or contentBounds right edge.
            p->anchorX = rightGroupBounds_.w > 0 ? rightGroupBounds_.x + rightGroupBounds_.w
                                                 : contentBounds_.x + contentBounds_.w;
        }
        anchorPopoverY(*p);
    }
}

void StatusBar::anchorPopoverY(DetailedPopover& pop) const {
    // growUp makes getBounds() extend upward from the anchor instead of down.
    pop.growUp = geom_.bottom;
    pop.anchorY = geom_.bottom ? bounds.y - 6.0 : bounds.y + bounds.h + 6.0;
}

void StatusBar::draw(Painter& p, int64_t now) {
    if (!visible) { return; }

    // Translucent menu-bar backdrop: a single frosted-glass slab spans the
    // centered content area, with a subtle hairline bottom (or top) border.
    // The lock screen stays chromeless against its dark background.
    if (backdrop_ && contentBounds_.w > 0) {
        const double tintAlpha =
            backdropAlpha_ >= 0.0 ? backdropAlpha_ : theme::statusbar::barTintAlpha;
        p.fillRoundedRect(contentBounds_, theme::statusbar::cornerRadius,
                          theme::statusbar::barTint.withAlpha(tintAlpha));
        // Hairline separator along the anchored edge
        if (theme::statusbar::barBorderEnabled && theme::statusbar::barBorderAlpha > 0.0) {
            const double borderY =
                geom_.bottom ? contentBounds_.y : contentBounds_.y + contentBounds_.h;
            p.fillRect({.x = contentBounds_.x, .y = borderY - 0.5, .w = contentBounds_.w, .h = 1.0},
                       theme::statusbar::barBorder.withAlpha(theme::statusbar::barBorderAlpha));
        }
    }

    auto drawZone = [&](auto& list) {
        for (auto& ind : list) {
            if (!isShown(*ind)) { continue; }
            ind->hoverAlpha_.animateTo(ind->hovered ? 1.0 : 0.0, theme::anim::fast,
                                       ease::inOutQuad);
            ind->hoverScale_.animateTo(ind->hovered ? 1.05 : 1.0, theme::anim::fast,
                                       ease::inOutQuad);
            ind->draw(p, now);
        }
    };
    drawZone(leftIndicators_);
    drawZone(centerIndicators_);
    drawZone(rightIndicators_);

    // Popovers
    popovers_.draw(p, now);

    // Hover tooltip — last so it floats over the bar but below nothing else.
    drawTooltip(p, now);
}

bool StatusBar::animating(int64_t now) const {
    if (popovers_.animating(now)) { return true; }
    if (tooltipAlpha_.active(now)) { return true; }
    auto zoneAnimating = [&](const auto& list) {
        for (const auto& ind : list) {
            if (ind->animating(now)) { return true; }
        }
        return false;
    };
    return zoneAnimating(leftIndicators_) || zoneAnimating(centerIndicators_) ||
           zoneAnimating(rightIndicators_);
}

bool StatusBar::handlePointerMotion(double x, double y, int64_t now) {
    lastPtrX_ = x;
    lastPtrY_ = y;
    if (popovers_.active() == &qsPanel_ && (qsPanel_.activeDragTile_ != nullptr)) {
        popovers_.handleDrag(x, y);
        host_.invalidate();
        return true;
    }
    // A press captured a non-QS popover (the slider popup): keep feeding it the
    // pointer as a drag, even past its bounds, so the thumb follows. Dragging is
    // interaction, so it re-arms the inactivity timer.
    if (popoverDragging_ && (popovers_.active() != nullptr) && popovers_.active() != &qsPanel_) {
        popovers_.handleDrag(x, y);
        resetAutoDismiss();
        host_.invalidate();
        return true;
    }
    // An open popover hides the hover-tooltip (the popover's own title serves).
    if (popovers_.active() != nullptr) {
        tooltipTarget_ = nullptr;
        // Let the popover track the cursor for its internal hover states. When
        // the pointer is inside it, that's the whole story — repaint and stop,
        // so we don't also light up an indicator behind the panel.
        if (popovers_.handleMotion(x, y)) {
            host_.invalidate();
            return true;
        }
    }

    // Hit test indicators. The hovered indicator also drives the hover-tooltip
    // (Phase 6 polish): we record the one pointer is over, and the timestamp
    // at which it *first* became so; the draw pass reveals its tooltip once
    // the dwell hits kTooltipDelayMs.
    StatusIndicator* newTooltipTarget = nullptr;
    auto checkHover = [&](auto& list) {
        for (auto& ind : list) {
            bool const prev = ind->hovered;
            ind->hovered = isShown(*ind) && ind->bounds.contains(x, y);
            if (ind->hovered) { newTooltipTarget = ind.get(); }
            if (ind->hovered != prev) { host_.invalidate(); }
        }
    };
    checkHover(leftIndicators_);
    checkHover(centerIndicators_);
    checkHover(rightIndicators_);

    if (newTooltipTarget != tooltipTarget_) {
        tooltipTarget_ = newTooltipTarget;
        tooltipHoverStartMs_ = now;
        tooltipAlpha_.set(0.0);
        host_.invalidate();
    } else if (tooltipTarget_ != nullptr) {
        host_.invalidate();  // keep ticking so the fade can run
    }

    return bounds.contains(x, y) ||
           ((popovers_.active() != nullptr) && popovers_.active()->contains(x, y));
}

void StatusBar::activateIndicator(StatusIndicator& ind) {
    // Lock-screen policy (default deny): the activation is dropped before the
    // indicator or its backend is touched. A locked session must not let a click
    // reach Pair/Trust (Bluetooth), ActivateConnection (Wi-Fi) or a tray app's
    // handler — see QL-1, QL-2, QL-4, QL-7 in docs/LOCK_SECURITY_REVIEW.md.
    if (!isInteractive(ind)) { return; }

    ind.onActivate();
    if (ind.id() == "power") {
        toggleQuickSettings();
        host_.invalidate();
        return;
    }
    if (ind.hasDetailedView()) {
        if (auto view = ind.createDetailedView()) {
            DetailedPopover* p = view.get();
            const double anchorX = ind.zone() == Zone::Left ? ind.bounds.x + p->contentWidth()
                                                            : ind.bounds.x + ind.bounds.w;
            popovers_.open(std::move(view), anchorX, 0);
            anchorPopoverY(*p);
            resetAutoDismiss();  // arm auto-dismiss if this popover opts in
        }
    }
    host_.invalidate();
}

void StatusBar::openIndicatorDetail(const std::string& id) {
    for (auto* list : {&leftIndicators_, &centerIndicators_, &rightIndicators_}) {
        for (auto& ind : *list) {
            if (ind->id() == id && ind->hasDetailedView()) {
                activateIndicator(*ind);  // replaces the open QS panel
                return;
            }
        }
    }
}

bool StatusBar::handlePointerButton(double x, double y, uint32_t button, bool pressed,
                                    int64_t now) {
    (void)now;
    // Linux evdev button codes as delivered by wl_pointer.
    constexpr uint32_t kBtnLeft = 0x110;
    constexpr uint32_t kBtnRight = 0x111;
    constexpr uint32_t kBtnMiddle = 0x112;
    // A button press always cancels the hover-tooltip: the click either
    // activates an indicator (and opens its popover) or dismisses the open
    // popover, and the lingering label would otherwise sit under the new
    // surface. It re-pops after kTooltipDelayMs of fresh dwell if the cursor
    // stays put.
    if (pressed) {
        tooltipTarget_ = nullptr;
        tooltipAlpha_.set(0.0);
    }
    if (popovers_.active() != nullptr) {
        if (popovers_.active()->contains(x, y)) {
            if (pressed) {
                const bool isQS = popovers_.active() == &qsPanel_;
                if (button == kBtnRight) {
                    // Right-click is the popover's context action (forget a
                    // network, dismiss a notification card, open a tile's
                    // detail view) — never a left-click activation or a drag.
                    popovers_.handleSecondaryClick(x, y);
                    if ((popovers_.active() != nullptr) &&
                        popovers_.active()->consumeCloseRequest()) {
                        popovers_.closeActive();
                    }
                } else {
                    popovers_.handleClick(x, y);
                    // A menu item may ask to close its popover after firing.
                    if ((popovers_.active() != nullptr) &&
                        popovers_.active()->consumeCloseRequest()) {
                        popovers_.closeActive();
                    } else if (!isQS) {
                        // Capture the drag so a slider-style popup tracks the pointer.
                        popoverDragging_ = true;
                    }
                }
                resetAutoDismiss();  // clicking counts as interaction (or re-arms)
            } else {
                popoverDragging_ = false;  // release ends any popover drag
                if (popovers_.active() == &qsPanel_) {
                    qsPanel_.activeDragTile_ = nullptr;  // release slider drag
                }
            }
            host_.invalidate();
            return true;
        }
        if (pressed) {
            // Clicked outside the active popover: dismiss it
            popoverDragging_ = false;
            popovers_.closeActive();
            host_.invalidate();
            return true;
        }
    }

    if (!pressed) { return false; }

    // Clicking anywhere on the right-group chip (or a right-zone indicator that
    // does not have its own popover) opens Quick Settings.
    // Right-zone indicators with detailed views (WiFi AP picker, battery popover,
    // etc.) open their own popover first; a second click on the same indicator or
    // a click on the open space inside the right chip opens QS.
    if (button == kBtnLeft) {
        // Each indicator handles its own click. Right-zone indicators with
        // detailed views open their own popover; toggle indicators fire directly.
        // No automatic fallback to Quick Settings.
        for (const auto& ind : rightIndicators_) {
            if (!isShown(*ind) || !isInteractive(*ind) || !ind->bounds.contains(x, y)) { continue; }
            if (ind->hasDetailedView()) {
                activateIndicator(*ind);
                host_.invalidate();
                return true;
            }
        }
        for (const auto& ind : rightIndicators_) {
            if (!isShown(*ind) || !isInteractive(*ind) || !ind->bounds.contains(x, y)) { continue; }
            if (ind->onClick(x, y)) {
                host_.invalidate();
                return true;
            }
            return true;
        }
    }

    // Indicator click. Right/middle go to their handlers and never fall through
    // to activate; a left click gets onClick() first refusal (e.g. the tray host
    // maps it to a sub-icon), else the default activate (tile/popover) runs.
    // Non-interactive indicators (the whole lock screen, minus the allow-list)
    // are skipped entirely, so none of these handlers runs.
    auto checkClick = [&](auto& list) {
        for (auto& ind : list) {
            if (!isShown(*ind) || !isInteractive(*ind) || !ind->bounds.contains(x, y)) { continue; }
            if (button == kBtnRight) {
                ind->onSecondaryClick(x, y);
            } else if (button == kBtnMiddle) {
                ind->onMiddleClick(x, y);
            } else if (!ind->onClick(x, y)) {
                activateIndicator(*ind);
            }
            host_.invalidate();
            return true;
        }
        return false;
    };
    if (checkClick(leftIndicators_)) { return true; }
    if (checkClick(centerIndicators_)) { return true; }
    if (checkClick(rightIndicators_)) { return true; }

    return bounds.contains(x, y);
}

void StatusBar::handlePointerLeave(int64_t now) {
    (void)now;
    lastPtrX_ = lastPtrY_ = -1.0;  // pointer gone → a transient panel may time out
    popoverDragging_ = false;
    // Dismiss Quick Settings when the pointer leaves the surface entirely
    // (switching to another window/desktop).
    if (popovers_.active() == &qsPanel_) { popovers_.closeActive(); }
    auto clear = [&](auto& list) {
        for (auto& ind : list) { ind->hovered = false; }
    };
    clear(leftIndicators_);
    clear(centerIndicators_);
    clear(rightIndicators_);
    tooltipTarget_ = nullptr;
    tooltipAlpha_.set(0.0);
    host_.invalidate();
}

bool StatusBar::handleScroll(double x, double y, double dx, double dy) {
    if ((popovers_.active() != nullptr) && popovers_.active()->contains(x, y)) {
        resetAutoDismiss();  // scrolling the panel is interaction
        return popovers_.handleScroll(dx, dy);
    }

    // Scroll-to-adjust indicators (volume, brightness) — the lock screen's
    // allow-list, so this is the one input path that still reaches an indicator
    // while locked (StatusIndicator::lockInteractive()).
    auto checkScroll = [&](auto& list) {
        for (auto& ind : list) {
            if (isShown(*ind) && isInteractive(*ind) && ind->bounds.contains(x, y) &&
                ind->onScroll(dx, dy, x, y)) {
                host_.invalidate();
                return true;
            }
        }
        return false;
    };
    return checkScroll(rightIndicators_);
}

bool StatusBar::handleKey(uint32_t keysym) {
    if (popovers_.active() != nullptr) {
        if (keysym == XKB_KEY_Escape) {
            popovers_.closeActive();
            host_.invalidate();
            return true;
        }
        const bool isQS = (popovers_.active() == &qsPanel_);
        const bool handled = popovers_.handleKey(keysym);
        // A keyboard action may fire an item (launcher Enter) and ask to close.
        if ((popovers_.active() != nullptr) && popovers_.active()->consumeCloseRequest()) {
            popovers_.closeActive();
        } else if (isQS && !handled) {
            // Close Quick Settings on any unhandled keypress (keyboard dismissal).
            popovers_.closeActive();
        } else if (handled) {
            resetAutoDismiss();  // a keyboard change (slider arrows) is interaction
        }
        host_.invalidate();
        return handled || isQS;
    }

    if (!hasFocusedChild()) { return false; }
    switch (keysym) {
        case XKB_KEY_Escape:
            clearFocus();
            return true;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
        case XKB_KEY_space: {
            auto findFocused = [&](auto& list) -> StatusIndicator* {
                for (auto& ind : list) {
                    if (ind->focused) { return ind.get(); }
                }
                return nullptr;
            };
            StatusIndicator* focused = findFocused(leftIndicators_);
            if (focused == nullptr) { focused = findFocused(centerIndicators_); }
            if (focused == nullptr) { focused = findFocused(rightIndicators_); }
            if (focused != nullptr) { activateIndicator(*focused); }
            return true;
        }
        case XKB_KEY_Left:
            return cycleFocus(true);
        case XKB_KEY_Right:
            return cycleFocus(false);
    }
    return false;
}

bool StatusBar::handleTextInput(const std::string& utf8) {
    return popovers_.handleText(utf8);
}

bool StatusBar::wantsKeyboard() const {
    return popovers_.activeWantsKeyboard();
}

void StatusBar::toggleQuickSettings() {
    if (popovers_.active() == &qsPanel_) {
        popovers_.closeActive();
    } else {
        const double qsAnchorX = rightGroupBounds_.w > 0 ? rightGroupBounds_.x + rightGroupBounds_.w
                                                         : contentBounds_.x + contentBounds_.w;
        popovers_.openBorrowed(&qsPanel_, qsAnchorX, 0);
        anchorPopoverY(qsPanel_);
    }
    resetAutoDismiss();  // QS opts out; this cancels any pending transient timer
}

bool StatusBar::cycleFocus(bool reverse) {
    // Collect visible indicators in order
    std::vector<StatusIndicator*> inds;
    auto collect = [&](auto& list) {
        for (auto& ind : list) {
            if (isShown(*ind)) { inds.push_back(ind.get()); }
        }
    };
    collect(leftIndicators_);
    collect(centerIndicators_);
    collect(rightIndicators_);

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
        host_.invalidate();
        return true;
    }

    inds[current]->focused = false;
    int const next = current + (reverse ? -1 : 1);
    if (next >= 0 && std::cmp_less(next, inds.size())) {
        inds[next]->focused = true;
        host_.invalidate();
        return true;
    }

    // Fell off the edge — focus leaves the bar
    host_.invalidate();
    return false;
}

void StatusBar::clearFocus() {
    auto clear = [&](auto& list) {
        for (auto& ind : list) { ind->focused = false; }
    };
    clear(leftIndicators_);
    clear(centerIndicators_);
    clear(rightIndicators_);
    host_.invalidate();
}

bool StatusBar::hasFocusedChild() const {
    auto anyFocused = [](const auto& list) {
        for (const auto& ind : list) {
            if (ind->focused) { return true; }
        }
        return false;
    };
    return anyFocused(leftIndicators_) || anyFocused(centerIndicators_) ||
           anyFocused(rightIndicators_);
}

// indicator for kTooltipDelayMs the bar fades in a small glass-card label
// completing the spec's "per-indicator tooltips" entry. It opens *away from
// the anchored edge* (down on a top bar, up on a bottom bar) — the same direction
// every other popover opens — and is anchored under the indicator's centre.
void StatusBar::drawTooltip(Painter& p, int64_t now) const {
    if (tooltipTarget_ == nullptr) {
        if (tooltipAlpha_.target() > 0.01) {
            const_cast<StatusBar*>(this)->tooltipAlpha_.animateTo(0.0, theme::anim::fast,
                                                                  ease::inOutQuad);
        }
        return;
    }
    const std::string text = tooltipTarget_->tooltip();
    if (text.empty()) { return; }

    const int64_t dwell = now - tooltipHoverStartMs_;
    if (dwell >= kTooltipDelayMs && tooltipAlpha_.target() < 0.99) {
        const_cast<StatusBar*>(this)->tooltipAlpha_.animateTo(1.0, theme::anim::fast,
                                                              ease::inOutQuad);
    }

    const double alpha = tooltipAlpha_.value(now);
    if (alpha < 0.01) { return; }

    // Geometry: measure once, place below (or above on a bottom bar) the
    // indicator. We mirror the popover screen-edge behaviour so the label
    // never gets cropped at the anchored edge.
    constexpr double kPadX = 10.0;
    constexpr double kPadY = 6.0;
    constexpr double kRadius = 8.0;
    constexpr double kGap = 8.0;  // gap from the indicator bounds

    TextStyle const style{.family = theme::font::family,
                          .size = 12.0,
                          .weight = PANGO_WEIGHT_NORMAL,
                          .color = theme::color::text};
    Size const ts = p.measureText(text, style);
    double const w = ts.w + (kPadX * 2.0);
    double const h = ts.h + (kPadY * 2.0);

    double const cx = tooltipTarget_->bounds.cx();
    double x = cx - (w / 2.0);
    // Open away from the anchored edge.
    double const y = geom_.bottom ? tooltipTarget_->bounds.y - kGap - h
                                  : tooltipTarget_->bounds.y + tooltipTarget_->bounds.h + kGap;

    // Clamp horizontal to the bar's slab so a near-edge indicator never clips.
    x = std::clamp(x, bounds.x + 2.0, bounds.x + bounds.w - w - 2.0);

    Rect const r{.x = x, .y = y, .w = w, .h = h};
    p.pushGroup();
    p.fillRoundedRect(r, kRadius, theme::color::glass);
    p.strokeRoundedRect(r, kRadius, theme::color::glassBorder, 1.0);
    p.drawText(r.x + kPadX, r.y + kPadY, text, style);
    p.popGroupWithAlpha(alpha);
}

}  // namespace qypr
