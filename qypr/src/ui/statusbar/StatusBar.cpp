// StatusBar.cpp - Composition host: layout, input, draw over extracted parts.
#include "ui/statusbar/StatusBar.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <ranges>
#include <utility>

#include "core/EventLoop.hpp"
#include "core/Interfaces.hpp"
#include "core/Types.hpp"
#include "mpris/MprisController.hpp"
#include "render/Painter.hpp"
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
#include "ui/statusbar/StatusBarLayout.hpp"

namespace qypr {

StatusBar::StatusBar(EventLoop& loop, Invalidator& host, const SystemBackends& backends,
                     const IndicatorRegistry::ModuleSelection* sel)
    : loop_(loop),
      host_(host),
      sessionSurface_(backends.sessionSurface),
      wifi_(backends.wifi) {
    popovers_.setEventLoop(&loop);

    // One bucketing + tile-attachment path shared with reloadModules.
    indicators_.load(backends, sel);
    indicators_.attachTiles(qsPanel_, [this](const std::string& id) { openIndicatorDetail(id); });

    // Build internal QS panel tiles (header, power, wifi combo, volume, media).
    // Only on the session surface (QL-7): command tiles must not exist on lock.
    if (sessionSurface_) {
        qsPanel_.buildTiles(loop_, backends, [this]() { openIndicatorDetail("wifi"); });
    }
    cascadeTheme();

    // Backends push; every push fans out to the indicators and repaints.
    auto onChange = [this] {
        notifyBackendUpdate();
    };
    if (backends.battery != nullptr) { backends.battery->setOnChange(onChange); }
    if (backends.brightness != nullptr) { backends.brightness->setOnChange(onChange); }
    if (backends.wifi != nullptr) { backends.wifi->setOnChange(onChange); }
    if (backends.bluetooth != nullptr) { backends.bluetooth->setOnChange(onChange); }
    if (backends.volume != nullptr) { backends.volume->setOnChange(onChange); }
    if (backends.sni != nullptr) { backends.sni->setOnChange(onChange); }
    if (backends.mpris != nullptr) { backends.mpris->setOnChange(onChange); }
    if (backends.idleInhibitor != nullptr) { backends.idleInhibitor->setOnChange(onChange); }
    if (backends.keyboardLayout != nullptr) { backends.keyboardLayout->setOnChange(onChange); }
    if (backends.workspace != nullptr) { backends.workspace->setOnChange(onChange); }
    if (backends.toplevel != nullptr) { backends.toplevel->setOnChange(onChange); }
    if (backends.dnd != nullptr) { backends.dnd->addListener(onChange); }

    // The bar's own 1s tick: drives poll() (clock text, wall-time animations).
    tickTimer_ = loop_.addTimer(1000, true, [this] {
        const int64_t now = nowMs();
        indicators_.forEach([&](StatusIndicator& ind) { ind.poll(now); });
        host_.invalidate();
    });

    measureSurface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    measureCr_ = cairo_create(measureSurface_);
}

StatusBar::~StatusBar() {
    if (tickTimer_ >= 0) { loop_.removeTimer(tickTimer_); }
    if (measureCr_ != nullptr) { cairo_destroy(measureCr_); }
    if (measureSurface_ != nullptr) { cairo_surface_destroy(measureSurface_); }
}

void StatusBar::reloadModules(const SystemBackends& backends,
                              const IndicatorRegistry::ModuleSelection* sel) {
    // Close any open popover — the indicators are about to be destroyed.
    popovers_.closeActive();

    indicators_.load(backends, sel);
    indicators_.attachTiles(
        qsPanel_, [this](const std::string& id) { openIndicatorDetail(id); },
        /*clearFirst=*/true);
    if (sessionSurface_) {
        qsPanel_.buildTiles(loop_, backends, [this]() { openIndicatorDetail("wifi"); });
    }
    cascadeTheme();
    host_.invalidate();
}

void StatusBar::resetAutoDismiss() {
    popovers_.resetAutoDismiss([this] {
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
    const double popH = popovers_.maxContentHeight();
    if (popH <= 0.0) { return 0; }
    return static_cast<int>(geom_.edgeMargin + geom_.height + 6.0 + popH + 8.0 + 0.5);
}

void StatusBar::setTheme(const theme::State& state) {
    theme_ = state;
    applyBackdropAlpha();
    cascadeTheme();
}

const theme::State& StatusBar::theme() const {
    return theme_;
}

void StatusBar::cascadeTheme() {
    indicators_.forEach([&](StatusIndicator& ind) { ind.setTheme(theme_); });
    qsPanel_.setTheme(theme_);
}

void StatusBar::applyBackdropAlpha() {
    theme_.statusbar.panelSurfaceAlpha =
        backdrop_ ? clamp01(backdropAlpha_ >= 0.0 ? backdropAlpha_ : theme_.statusbar.barTintAlpha)
                  : 0.0;
}

void StatusBar::setBackdrop(bool enabled, double alpha) {
    backdrop_ = enabled;
    backdropAlpha_ = alpha;
    applyBackdropAlpha();
    qsPanel_.setBackdrop(enabled, alpha);
    popovers_.setBackdrop(enabled, alpha);
    host_.invalidate();
}

void StatusBar::setGeometry(const BarGeometry& g) {
    geom_ = g;
    host_.invalidate();
}

void StatusBar::notifyBackendUpdate() {
    indicators_.forEach([&](StatusIndicator& ind) { ind.onBackendUpdate(); });
    if (wifi_ != nullptr) { qsPanel_.updateWifi(wifi_->snapshot()); }
    host_.invalidate();
}

void StatusBar::layout(int screenW, int screenH) {
    double const pad = theme().statusbar.padding;
    double const sp = theme().statusbar.iconSpacing;

    Painter meas(measureCr_);

    auto measureZone = [&](const std::vector<std::unique_ptr<StatusIndicator>>& zone) {
        std::vector<LayoutItem> items;
        items.reserve(zone.size());
        for (const auto& ind : zone) {
            items.push_back(
                {.width = isShown(*ind) ? ind->measureWidth(meas) : 0.0, .shown = isShown(*ind)});
        }
        return items;
    };

    LayoutRequest req;
    req.geom = geom_;
    req.pad = pad;
    req.spacing = sp;
    req.screenW = screenW;
    req.screenH = screenH;
    req.left = measureZone(indicators_.left());
    req.center = measureZone(indicators_.center());
    req.right = measureZone(indicators_.right());

    const LayoutResult r = computeStatusBarLayout(req);
    bounds = r.bounds;
    contentBounds_ = r.contentBounds;
    rightGroupBounds_ = r.rightGroupBounds;

    auto apply = [](std::vector<std::unique_ptr<StatusIndicator>>& zone,
                    const std::vector<Rect>& rects) {
        for (size_t i = 0; i < zone.size() && i < rects.size(); ++i) { zone[i]->bounds = rects[i]; }
    };
    apply(indicators_.left(), r.left);
    apply(indicators_.center(), r.center);
    apply(indicators_.right(), r.right);

    // Anchor active popovers away from the screen edge, right-aligned for
    // wide/QS panels.
    if (popovers_.active() != nullptr) {
        DetailedPopover* p = popovers_.active();
        if (p == &qsPanel_ || p->contentWidth() >= 300.0) {
            p->anchorX = rightGroupBounds_.w > 0 ? rightGroupBounds_.x + rightGroupBounds_.w
                                                 : contentBounds_.x + contentBounds_.w;
        }
        popovers_.anchorToStrip(*p, geom_.bottom, bounds);
    }
}

void StatusBar::draw(Painter& p, int64_t now) {
    if (!visible) { return; }

    if (backdrop_ && contentBounds_.w > 0) {
        const double tintAlpha =
            backdropAlpha_ >= 0.0 ? backdropAlpha_ : theme().statusbar.barTintAlpha;
        p.fillRoundedRect(contentBounds_, theme().statusbar.cornerRadius,
                          theme().statusbar.barTint.withAlpha(tintAlpha));
        if (theme().statusbar.barBorderEnabled && theme().statusbar.barBorderAlpha > 0.0) {
            const double borderY =
                geom_.bottom ? contentBounds_.y : contentBounds_.y + contentBounds_.h;
            p.fillRect({.x = contentBounds_.x, .y = borderY - 0.5, .w = contentBounds_.w, .h = 1.0},
                       theme().statusbar.barBorder.withAlpha(theme().statusbar.barBorderAlpha));
        }
    }

    auto drawZone = [&](auto& list) {
        for (auto& ind : list) {
            if (!isShown(*ind)) { continue; }
            ind->hoverAlpha_.animateTo(ind->hovered ? 1.0 : 0.0, theme().anim.fast,
                                       ease::inOutQuad);
            ind->hoverScale_.animateTo(ind->hovered ? 1.05 : 1.0, theme().anim.fast,
                                       ease::inOutQuad);
            ind->draw(p, now);
        }
    };
    drawZone(indicators_.left());
    drawZone(indicators_.center());
    drawZone(indicators_.right());

    popovers_.draw(p, now);
    tooltips_.draw(p, theme(), geom_, bounds, now);
}

bool StatusBar::animating(int64_t now) const {
    if (popovers_.animating(now)) { return true; }
    if (tooltips_.animating(now)) { return true; }
    bool zoneAnimating = false;
    const_cast<IndicatorHost&>(indicators_).forEach([&](StatusIndicator& ind) {
        if (!zoneAnimating && ind.animating(now)) { zoneAnimating = true; }
    });
    return zoneAnimating;
}

StatusIndicator* StatusBar::hitTestZone(std::vector<std::unique_ptr<StatusIndicator>>& zone,
                                        double x, double y, bool requireInteractive) {
    return StatusBarInput::hitTest(zone, x, y, sessionContentVisible_, requireInteractive);
}

StatusBarInput::Context StatusBar::makeInputContext() {
    return StatusBarInput::Context{
        .indicators = indicators_,
        .popovers = popovers_,
        .tooltips = tooltips_,
        .qsPanel = qsPanel_,
        .host = host_,
        .geom = geom_,
        .bounds = bounds,
        .rightGroupBounds = rightGroupBounds_,
        .contentBounds = contentBounds_,
        .theme = theme(),
        .sessionContentVisible = sessionContentVisible_,
        .onToggleQuickSettings = [this] { toggleQuickSettings(); },
        .onActivateIndicator = [this](StatusIndicator& ind) { activateIndicator(ind); },
        .onResetAutoDismiss = [this] { resetAutoDismiss(); },
    };
}

bool StatusBar::handlePointerMotion(double x, double y, int64_t now) {
    auto ctx = makeInputContext();
    return input_.handlePointerMotion(ctx, x, y, now);
}

void StatusBar::activateIndicator(StatusIndicator& ind) {
    // Lock-screen policy (default deny) before any backend touch (QL-*).
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
            p->setTheme(theme());
            const double anchorX = ind.zone() == Zone::Left ? ind.bounds.x + p->contentWidth()
                                                            : ind.bounds.x + ind.bounds.w;
            popovers_.open(std::move(view), anchorX, 0);
            popovers_.anchorToStrip(*p, geom_.bottom, bounds);
            resetAutoDismiss();
        }
    }
    host_.invalidate();
}

void StatusBar::openIndicatorDetail(const std::string& id) {
    if (auto* ind = indicators_.findById(id); ind != nullptr && ind->hasDetailedView()) {
        activateIndicator(*ind);
    }
}

bool StatusBar::handlePointerButton(double x, double y, uint32_t button, bool pressed,
                                    int64_t now) {
    auto ctx = makeInputContext();
    return input_.handlePointerButton(ctx, x, y, button, pressed, now);
}

void StatusBar::handlePointerLeave(int64_t now) {
    auto ctx = makeInputContext();
    input_.handlePointerLeave(ctx, now);
}

bool StatusBar::handleScroll(double x, double y, double dx, double dy) {
    auto ctx = makeInputContext();
    return input_.handleScroll(ctx, x, y, dx, dy);
}

bool StatusBar::handleKey(uint32_t keysym) {
    auto ctx = makeInputContext();
    return input_.handleKey(ctx, keysym);
}

bool StatusBar::handleTextInput(const std::string& utf8) {
    auto ctx = makeInputContext();
    return input_.handleTextInput(ctx, utf8);
}

bool StatusBar::wantsKeyboard() const {
    auto ctx = const_cast<StatusBar*>(this)->makeInputContext();
    return input_.wantsKeyboard(ctx);
}

void StatusBar::toggleQuickSettings() {
    if (popovers_.active() == &qsPanel_) {
        popovers_.closeActive();
    } else {
        const double qsAnchorX = rightGroupBounds_.w > 0 ? rightGroupBounds_.x + rightGroupBounds_.w
                                                         : contentBounds_.x + contentBounds_.w;
        popovers_.openBorrowed(&qsPanel_, qsAnchorX, 0);
        popovers_.anchorToStrip(qsPanel_, geom_.bottom, bounds);
    }
    resetAutoDismiss();
}

bool StatusBar::cycleFocus(bool reverse) {
    auto ctx = makeInputContext();
    return input_.cycleFocus(ctx, reverse);
}

void StatusBar::clearFocus() {
    auto ctx = makeInputContext();
    input_.clearFocus(ctx);
}

bool StatusBar::hasFocusedChild() const {
    auto ctx = const_cast<StatusBar*>(this)->makeInputContext();
    return input_.hasFocusedChild(ctx);
}

}  // namespace qypr
