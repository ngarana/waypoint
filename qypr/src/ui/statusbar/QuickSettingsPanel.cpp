// QuickSettingsPanel.cpp - Shared Quick Settings panel widget implementation
#include "ui/statusbar/QuickSettingsPanel.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "core/EventLoop.hpp"
#include "render/Painter.hpp"
#include "ui/Theme.hpp"
#include "ui/statusbar/QSTileFactory.hpp"
#include "ui/statusbar/QuickSettingsInput.hpp"
#include "ui/statusbar/QuickSettingsLayout.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

// ─── Tile building ────────────────────────────────────────────────────────

void QuickSettingsPanel::buildTiles(EventLoop& loop, const SystemBackends& backends,
                                    const std::function<void()>& onOpenWifi) {
    onOpenWifi_ = onOpenWifi;
    auto built = QSTileFactory::createTiles(loop, backends, theme(), tiles_, onOpenWifi);
    header_ = std::move(built.header);
    power_ = std::move(built.power);
    wifiCombo_ = std::move(built.wifiCombo);
    volume_ = std::move(built.volume);
    media_ = std::move(built.media);

    // Remove any indicator-created WiFi or volume tiles from grid
    std::erase_if(tiles_, [](const std::unique_ptr<QSTile>& t) {
        if (t == nullptr) { return false; }
        return t->role() == QSTile::Role::Wifi || t->type() == QSTile::Type::Volume ||
               t->role() == QSTile::Role::Volume;
    });

    for (auto& fallback : built.fallbackTiles) { tiles_.push_back(std::move(fallback)); }

    setTheme(theme());
}

void QuickSettingsPanel::updateWifi(const WifiSnapshot& s) {
    if (!wifiCombo_) { return; }
    wifiCombo_->setEnabled(s.enabled);
    wifiCombo_->setConnected(s.connected);
    wifiCombo_->setSsid(s.ssid);
    wifiCombo_->setStrength(s.strength);
    wifiCombo_->setScanning(s.scanning);
}

void QuickSettingsPanel::addTile(std::unique_ptr<QSTile> tile) {
    if (tile) { tile->setTheme(theme()); }
    tiles_.push_back(std::move(tile));
}

void QuickSettingsPanel::setTheme(const theme::State& state) {
    theme::ThemeAware::setTheme(state);
    for (const auto& tile : tiles_) {
        if (tile) { tile->setTheme(state); }
    }
    if (header_) { header_->setTheme(state); }
    if (power_) { power_->setTheme(state); }
    if (wifiCombo_) { wifiCombo_->setTheme(state); }
    if (volume_) { volume_->setTheme(state); }
    if (media_) { media_->setTheme(state); }
}

// ─── Geometry ─────────────────────────────────────────────────────────────

double QuickSettingsPanel::contentWidth() const {
    return panelW();
}

double QuickSettingsPanel::contentHeight() const {
    QSLayoutMetrics m{
        .panelW = panelW(),
        .pad = pad(),
        .gap = gap(),
        .gridRowH = gridRowH(),
    };
    return QuickSettingsLayout::computeContentHeight(m, header_ != nullptr, wifiCombo_ != nullptr,
                                                     tiles_, volume_ != nullptr, media_ != nullptr);
}

// ─── Layout ───────────────────────────────────────────────────────────────

void QuickSettingsPanel::layoutTiles() {
    Rect const popBounds = getBounds();
    QSLayoutMetrics m{
        .panelW = panelW(),
        .pad = pad(),
        .gap = gap(),
        .gridRowH = gridRowH(),
    };
    auto res = QuickSettingsLayout::layout(popBounds, m, header_.get(), power_.get(),
                                           wifiCombo_.get(), tiles_, volume_.get(), media_.get());
    closeBounds_ = res.closeBounds;
    powerBounds_ = res.powerBounds;
}

Rect QuickSettingsPanel::boundsFor(QSTile::Role role) const {
    if (wifiCombo_ && wifiCombo_->role() == role) { return wifiCombo_->bounds; }
    if (volume_ && volume_->role() == role) { return volume_->bounds; }
    for (const auto& t : tiles_) {
        if (t && t->role() == role) { return t->bounds; }
    }
    return {.x = 0, .y = 0, .w = 0, .h = 0};
}

// ─── Draw ─────────────────────────────────────────────────────────────────

void QuickSettingsPanel::draw(Painter& p, int64_t now) {
    layoutTiles();
    Rect const popBounds = getBounds();

    // Match the bar's shared backdrop. If this panel is hosted by the lock
    // screen, draw its original opaque surface instead.
    if (!drawSharedBackdrop(p, popBounds, theme().statusbar.qsCornerRadius)) {
        p.fillRoundedRect(popBounds, theme().statusbar.qsCornerRadius, theme().colors.surface);
    }

    // Close button (×) at top right — a subtle circular button.
    closeBounds_ = {.x = popBounds.x + popBounds.w - pad() - 24.0,
                    .y = popBounds.y + 8.0,
                    .w = 24.0,
                    .h = 24.0};
    p.fillCircleSource(closeBounds_.x + 12.0, closeBounds_.y + 12.0, 12.0,
                       theme().panelSurfaceHover());
    TextStyle const closeStyle{.family = theme().font.iconFamily,
                               .size = 11.0,
                               .weight = PANGO_WEIGHT_NORMAL,
                               .color = theme().colors.textSubtle};
    Size const closeSz = p.measureText("󰅖", closeStyle);
    p.drawText(closeBounds_.x + 12.0 - (closeSz.w / 2.0), closeBounds_.y + 12.0 - (closeSz.h / 2.0),
               "󰅖", closeStyle);

    auto drawOrSkip = [&](auto& tile) {
        if (!tile) { return; }
        tile->hoverAnim_.animateTo(tile->hovered ? 1.0 : 0.0, theme().anim.fast, ease::inOutQuad);
        tile->draw(p, now);
    };

    drawOrSkip(header_);
    drawOrSkip(power_);
    drawOrSkip(wifiCombo_);
    for (auto& t : tiles_) { drawOrSkip(t); }
    drawOrSkip(volume_);
    drawOrSkip(media_);
}

// ─── Input ────────────────────────────────────────────────────────────────

bool QuickSettingsPanel::handleMotion(double x, double y) {
    curX_ = x;
    curY_ = y;
    return getBounds().contains(x, y);
}

bool QuickSettingsPanel::consumeCloseRequest() {
    if (closeRequested_) {
        closeRequested_ = false;
        return true;
    }
    return false;
}

bool QuickSettingsPanel::handleClick(double x, double y) {
    QuickSettingsInput::Context ctx{
        .popBounds = getBounds(),
        .closeBounds = closeBounds_,
        .header = header_.get(),
        .power = power_.get(),
        .wifiCombo = wifiCombo_.get(),
        .volume = volume_.get(),
        .media = media_.get(),
        .tiles = tiles_,
        .activeDragTile = activeDragTile_,
        .curX = curX_,
        .curY = curY_,
        .closeRequested = closeRequested_,
        .onOpenWifi = onOpenWifi_,
    };
    return QuickSettingsInput::handleClick(ctx, x, y);
}

bool QuickSettingsPanel::handleSecondaryClick(double x, double y) {
    QuickSettingsInput::Context ctx{
        .popBounds = getBounds(),
        .closeBounds = closeBounds_,
        .header = header_.get(),
        .power = power_.get(),
        .wifiCombo = wifiCombo_.get(),
        .volume = volume_.get(),
        .media = media_.get(),
        .tiles = tiles_,
        .activeDragTile = activeDragTile_,
        .curX = curX_,
        .curY = curY_,
        .closeRequested = closeRequested_,
        .onOpenWifi = onOpenWifi_,
    };
    return QuickSettingsInput::handleSecondaryClick(ctx, x, y);
}

bool QuickSettingsPanel::handleDrag(double x, double y) {
    QuickSettingsInput::Context ctx{
        .popBounds = getBounds(),
        .closeBounds = closeBounds_,
        .header = header_.get(),
        .power = power_.get(),
        .wifiCombo = wifiCombo_.get(),
        .volume = volume_.get(),
        .media = media_.get(),
        .tiles = tiles_,
        .activeDragTile = activeDragTile_,
        .curX = curX_,
        .curY = curY_,
        .closeRequested = closeRequested_,
        .onOpenWifi = onOpenWifi_,
    };
    return QuickSettingsInput::handleDrag(ctx, x, y);
}

bool QuickSettingsPanel::handleScroll(double dx, double dy) {
    QuickSettingsInput::Context ctx{
        .popBounds = getBounds(),
        .closeBounds = closeBounds_,
        .header = header_.get(),
        .power = power_.get(),
        .wifiCombo = wifiCombo_.get(),
        .volume = volume_.get(),
        .media = media_.get(),
        .tiles = tiles_,
        .activeDragTile = activeDragTile_,
        .curX = curX_,
        .curY = curY_,
        .closeRequested = closeRequested_,
        .onOpenWifi = onOpenWifi_,
    };
    return QuickSettingsInput::handleScroll(ctx, dx, dy);
}

bool QuickSettingsPanel::handleKey(uint32_t keysym) {
    QuickSettingsInput::Context ctx{
        .popBounds = getBounds(),
        .closeBounds = closeBounds_,
        .header = header_.get(),
        .power = power_.get(),
        .wifiCombo = wifiCombo_.get(),
        .volume = volume_.get(),
        .media = media_.get(),
        .tiles = tiles_,
        .activeDragTile = activeDragTile_,
        .curX = curX_,
        .curY = curY_,
        .closeRequested = closeRequested_,
        .onOpenWifi = onOpenWifi_,
    };
    return QuickSettingsInput::handleKey(ctx, keysym);
}

}  // namespace qypr
