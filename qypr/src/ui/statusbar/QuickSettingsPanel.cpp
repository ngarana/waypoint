// QuickSettingsPanel.cpp - Shared Quick Settings panel widget implementation
#include "ui/statusbar/QuickSettingsPanel.hpp"

#include <algorithm>
#include <xkbcommon/xkbcommon-keysyms.h>
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
                                    const std::function<void()>& onOpenWifi,
                                    const std::function<void()>& onOpenVolume) {
    onOpenWifi_ = onOpenWifi;
    auto built = QSTileFactory::createTiles(loop, backends, theme(), model_.tiles(), onOpenWifi);
    header_ = std::move(built.header);
    power_ = std::move(built.power);
    wifiCombo_ = std::move(built.wifiCombo);
    volume_ = std::move(built.volume);
    if (volume_) { volume_->setOnSecondary(onOpenVolume); }
    media_ = std::move(built.media);

    // Indicator-created Wi-Fi/volume tiles are owned by the panel as singles.
    model_.removePanelOwnedDuplicates();

    for (auto& fallback : built.fallbackTiles) { model_.addTile(std::move(fallback)); }

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
    model_.addTile(std::move(tile));
}

void QuickSettingsPanel::setTheme(const theme::State& state) {
    theme::ThemeAware::setTheme(state);
    model_.setTheme(state);
    if (header_) { header_->setTheme(state); }
    if (power_) { power_->setTheme(state); }
    if (wifiCombo_) { wifiCombo_->setTheme(state); }
    if (volume_) { volume_->setTheme(state); }
    if (media_) { media_->setTheme(state); }
}

// ─── Geometry ─────────────────────────────────────────────────────────────

double QuickSettingsPanel::contentWidth() const {
    QSLayoutMetrics m{
        .panelW = panelW(),
        .pad = pad(),
        .gap = gap(),
        .gridRowH = gridRowH(),
    };
    return QuickSettingsLayout::computeContentWidth(m, wifiCombo_.get(), model_.tiles());
}

double QuickSettingsPanel::contentHeight() const {
    QSLayoutMetrics m{
        .panelW = std::min(contentWidth(), availableWidth()),
        .pad = pad(),
        .gap = gap(),
        .gridRowH = gridRowH(),
    };
    return QuickSettingsLayout::computeContentHeight(m, header_ != nullptr, wifiCombo_.get(),
                                                     model_.tiles(), volume_ != nullptr,
                                                     media_ != nullptr);
}

// ─── Layout ───────────────────────────────────────────────────────────────

void QuickSettingsPanel::layoutTiles() {
    Rect const popBounds = getBounds();
    QSLayoutMetrics m{
        .panelW = std::min(contentWidth(), availableWidth()),
        .pad = pad(),
        .gap = gap(),
        .gridRowH = gridRowH(),
    };
    auto res =
        QuickSettingsLayout::layout(popBounds, m, header_.get(), power_.get(), wifiCombo_.get(),
                                    model_.tiles(), volume_.get(), media_.get());
    closeBounds_ = res.closeBounds;
    powerBounds_ = res.powerBounds;
}

Rect QuickSettingsPanel::boundsFor(QSTile::Role role) const {
    if (wifiCombo_ && wifiCombo_->role() == role) { return wifiCombo_->bounds; }
    if (volume_ && volume_->role() == role) { return volume_->bounds; }
    if (const QSTile* tile = model_.findByRole(role)) { return tile->bounds; }
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

    // Header actions occupy separate slots; close never overlaps power.
    const bool closeHot = closeBounds_.contains(curX_, curY_) || focusedControl_ == 0;
    p.fillRoundedRect(closeBounds_, 8.0,
                      closeHot ? theme().panelSurfaceHover() : theme().panelSurface());
    TextStyle const closeStyle{theme().font.family, 18.0, PANGO_WEIGHT_NORMAL,
                               theme().colors.textSubtle};
    p.drawText(closeBounds_.x + 10.0, closeBounds_.y + 5.0, "×", closeStyle);

    auto drawOrSkip = [&](auto& tile) {
        if (!tile) { return; }
        tile->hoverAnim_.animateTo(tile->hovered ? 1.0 : 0.0, theme().anim.fast, ease::inOutQuad);
        tile->draw(p, now);
        if (tile->hovered) {
            p.strokeRoundedRect(tile->bounds, 8.0, theme().colors.primary.withAlpha(0.6), 1.0);
        }
        if (tile->hasSecondaryAction() && tile->type() == QSTile::Type::Toggle) {
            const Rect more{tile->bounds.x + tile->bounds.w - 24.0, tile->bounds.y, 24.0,
                            tile->bounds.h};
            p.fillRoundedRect(more, 6.0, theme().panelSurfaceHover());
            TextStyle style{theme().font.family, 18.0, PANGO_WEIGHT_NORMAL, theme().colors.text};
            p.drawText(more.x + 7.0, more.y + (more.h - 22.0) / 2.0, "›", style);
        }
    };

    drawOrSkip(header_);
    drawOrSkip(power_);
    drawOrSkip(wifiCombo_);
    for (auto& t : model_.tiles()) { drawOrSkip(t); }
    drawOrSkip(volume_);
    drawOrSkip(media_);
}

// ─── Input ────────────────────────────────────────────────────────────────

bool QuickSettingsPanel::handleMotion(double x, double y) {
    curX_ = x;
    curY_ = y;
    focusedControl_ = -1;
    for (auto* tile : interactiveTiles()) { tile->hovered = tile->bounds.contains(x, y); }
    return contains(x, y);
}

std::vector<QSTile*> QuickSettingsPanel::interactiveTiles() {
    std::vector<QSTile*> tiles;
    if (power_) { tiles.push_back(power_.get()); }
    if (wifiCombo_) { tiles.push_back(wifiCombo_.get()); }
    for (auto& tile : model_.tiles()) {
        if (tile && tile->type() != QSTile::Type::Info) { tiles.push_back(tile.get()); }
    }
    if (volume_) { tiles.push_back(volume_.get()); }
    if (media_) { tiles.push_back(media_.get()); }
    return tiles;
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
        .tiles = model_.tiles(),
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
        .tiles = model_.tiles(),
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
        .tiles = model_.tiles(),
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
        .tiles = model_.tiles(),
        .activeDragTile = activeDragTile_,
        .curX = curX_,
        .curY = curY_,
        .closeRequested = closeRequested_,
        .onOpenWifi = onOpenWifi_,
    };
    return QuickSettingsInput::handleScroll(ctx, dx, dy);
}

bool QuickSettingsPanel::handleKey(uint32_t keysym) {
    auto tiles = interactiveTiles();
    const int count = static_cast<int>(tiles.size()) + 1;
    if (keysym == XKB_KEY_Tab || keysym == XKB_KEY_ISO_Left_Tab) {
        const bool reverse = keysym == XKB_KEY_ISO_Left_Tab;
        if (focusedControl_ < 0) {
            focusedControl_ = reverse ? count - 1 : 0;
        } else {
            focusedControl_ = (focusedControl_ + (reverse ? -1 : 1) + count) % count;
        }
        for (size_t i = 0; i < tiles.size(); ++i) {
            tiles[i]->hovered = static_cast<int>(i) + 1 == focusedControl_;
        }
        return true;
    }
    if (focusedControl_ == 0 && (keysym == XKB_KEY_Return || keysym == XKB_KEY_space)) {
        closeRequested_ = true;
        return true;
    }
    if (focusedControl_ > 0 && focusedControl_ < count) {
        auto* tile = tiles[static_cast<size_t>(focusedControl_ - 1)];
        if (tile->handleKey(keysym)) { return true; }
        const Rect b = tile->bounds;
        if (keysym == XKB_KEY_Return || keysym == XKB_KEY_space) {
            tile->onClick(b.x + b.w / 2.0, b.y + b.h / 2.0);
            if (tile->type() == QSTile::Type::Power) { closeRequested_ = true; }
            return true;
        }
        if (keysym == XKB_KEY_Right && tile->hasSecondaryAction()) {
            tile->onSecondaryClick(b.x, b.y);
            return true;
        }
    }
    QuickSettingsInput::Context ctx{
        .popBounds = getBounds(),
        .closeBounds = closeBounds_,
        .header = header_.get(),
        .power = power_.get(),
        .wifiCombo = wifiCombo_.get(),
        .volume = volume_.get(),
        .media = media_.get(),
        .tiles = model_.tiles(),
        .activeDragTile = activeDragTile_,
        .curX = curX_,
        .curY = curY_,
        .closeRequested = closeRequested_,
        .onOpenWifi = onOpenWifi_,
    };
    return QuickSettingsInput::handleKey(ctx, keysym);
}

}  // namespace qypr
