// QuickSettingsLayout.cpp - Geometry layout for the Quick Settings panel.
#include "ui/statusbar/QuickSettingsLayout.hpp"

namespace qypr {

double QuickSettingsLayout::computeContentHeight(const QSLayoutMetrics& m, bool hasHeader,
                                                 bool hasWifiCombo,
                                                 const std::vector<std::unique_ptr<QSTile>>& tiles,
                                                 bool hasVolume, bool hasMedia) {
    double h = m.pad;
    if (hasHeader) { h += kHeaderH; }

    int toggleCount = 0;
    if (hasWifiCombo) { toggleCount++; }
    for (const auto& t : tiles) {
        if (t && (t->type() == QSTile::Type::Toggle || t->type() == QSTile::Type::WifiCombo)) {
            toggleCount++;
        }
    }
    int rows = (toggleCount + kGridCols - 1) / kGridCols;
    if (rows > 0) { h += m.gap + (rows * m.gridRowH) + ((rows - 1) * m.gap); }

    for (const auto& t : tiles) {
        if (t && t->type() == QSTile::Type::Slider) { h += m.gap + kSliderH; }
    }

    if (hasVolume) { h += m.gap + kVolumeH; }

    for (const auto& t : tiles) {
        if (t && t->type() == QSTile::Type::Info) { h += m.gap + kInfoH; }
    }

    if (hasMedia) { h += m.gap + kMediaH; }

    h += m.pad;
    return h;
}

QSLayoutResult QuickSettingsLayout::layout(const Rect& popBounds, const QSLayoutMetrics& m,
                                           QSHeaderTile* header, QSPowerTile* power,
                                           QSWifiComboTile* wifiCombo,
                                           std::vector<std::unique_ptr<QSTile>>& tiles,
                                           QSVolumeTile* volume, QSMediaTile* media) {
    QSLayoutResult res;
    res.popBounds = popBounds;

    // Close button (×) at top right — a subtle circular button.
    res.closeBounds = {.x = popBounds.x + popBounds.w - m.pad - 24.0,
                       .y = popBounds.y + 8.0,
                       .w = 24.0,
                       .h = 24.0};

    double y = popBounds.y + m.pad;
    const double contentW = popBounds.w - (2 * m.pad);
    const double colW = (contentW - (2 * m.gap)) / kGridCols;

    // 1. Header (left side) & Power button (right side)
    constexpr double powerW = 52.0;
    const double headerW = contentW - powerW - m.gap;
    if (header != nullptr) {
        header->bounds = {.x = popBounds.x + m.pad, .y = y, .w = headerW, .h = kHeaderH};
    }

    if (power != nullptr) {
        const double px = popBounds.x + popBounds.w - m.pad - powerW;
        res.powerBounds = {.x = px, .y = y, .w = powerW, .h = kHeaderH};
        power->bounds = res.powerBounds;
    }

    y += kHeaderH + m.gap;

    // 2. 3-Column Toggle Grid
    std::vector<QSTile*> gridTiles;
    if (wifiCombo != nullptr) { gridTiles.push_back(wifiCombo); }
    for (auto& t : tiles) {
        if (t && (t->type() == QSTile::Type::Toggle || t->type() == QSTile::Type::WifiCombo)) {
            gridTiles.push_back(t.get());
        }
    }

    int col = 0;
    double rowY = y;
    for (auto* tile : gridTiles) {
        if (col >= kGridCols) {
            col = 0;
            rowY += m.gridRowH + m.gap;
        }
        const double tx = popBounds.x + m.pad + (col * (colW + m.gap));
        tile->bounds = {.x = tx, .y = rowY, .w = colW, .h = m.gridRowH};
        col++;
    }

    if (!gridTiles.empty()) { y = rowY + m.gridRowH; }

    // 3. Slider section (Brightness)
    for (auto& t : tiles) {
        if (t && t->type() == QSTile::Type::Slider) {
            y += m.gap;
            t->bounds = {.x = popBounds.x + m.pad, .y = y, .w = contentW, .h = kSliderH};
            y += kSliderH;
        }
    }

    // 4. Volume section
    if (volume != nullptr) {
        y += m.gap;
        volume->bounds = {.x = popBounds.x + m.pad, .y = y, .w = contentW, .h = kVolumeH};
        y += kVolumeH;
    }

    // 5. Info section (Battery)
    for (auto& t : tiles) {
        if (t && t->type() == QSTile::Type::Info) {
            y += m.gap;
            t->bounds = {.x = popBounds.x + m.pad, .y = y, .w = contentW, .h = kInfoH};
            y += kInfoH;
        }
    }

    // 6. Media card
    if (media != nullptr) {
        y += m.gap;
        media->bounds = {.x = popBounds.x + m.pad, .y = y, .w = contentW, .h = kMediaH};
        y += kMediaH;
    }

    res.contentHeight = (y - popBounds.y) + m.pad;
    return res;
}

}  // namespace qypr
