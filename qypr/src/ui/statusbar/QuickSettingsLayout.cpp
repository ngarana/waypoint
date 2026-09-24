// QuickSettingsLayout.cpp - Geometry layout for the Quick Settings panel.
#include "ui/statusbar/QuickSettingsLayout.hpp"

#include <algorithm>

namespace {

std::vector<qypr::QSTile*> gridTiles(qypr::QSWifiComboTile* wifiCombo,
                                     const std::vector<std::unique_ptr<qypr::QSTile>>& tiles) {
    std::vector<qypr::QSTile*> result;
    if (wifiCombo != nullptr) { result.push_back(wifiCombo); }
    for (const auto& tile : tiles) {
        if (tile && (tile->type() == qypr::QSTile::Type::Toggle ||
                     tile->type() == qypr::QSTile::Type::WifiCombo)) {
            result.push_back(tile.get());
        }
    }
    return result;
}

double gridRowHeight(const qypr::QSLayoutMetrics& m, const std::vector<qypr::QSTile*>& grid,
                     double colW) {
    double rowH = m.gridRowH;
    for (const auto* tile : grid) {
        if (tile != nullptr) { rowH = std::max(rowH, tile->preferredGridHeight(colW)); }
    }
    return rowH;
}

}  // namespace

namespace qypr {

double QuickSettingsLayout::computeContentWidth(const QSLayoutMetrics& m,
                                                QSWifiComboTile* wifiCombo,
                                                const std::vector<std::unique_ptr<QSTile>>& tiles) {
    double preferredColW = (m.panelW - (2.0 * m.pad) - (2.0 * m.gap)) / kGridCols;
    if (wifiCombo != nullptr) {
        preferredColW = std::max(preferredColW, wifiCombo->preferredGridWidth());
    }
    for (const auto& tile : tiles) {
        if (tile &&
            (tile->type() == QSTile::Type::Toggle || tile->type() == QSTile::Type::WifiCombo)) {
            preferredColW = std::max(preferredColW, tile->preferredGridWidth());
        }
    }

    // Keep the panel usable on smaller displays while allowing normal tile
    // labels (for example "Do Not Disturb") to determine its natural width.
    constexpr double kMaxPanelW = 640.0;
    const double required = (2.0 * m.pad) + (kGridCols * preferredColW) + (2.0 * m.gap);
    return std::min(std::max(m.panelW, required), std::max(m.panelW, kMaxPanelW));
}

double QuickSettingsLayout::computeContentHeight(const QSLayoutMetrics& m, bool hasHeader,
                                                 QSWifiComboTile* wifiCombo,
                                                 const std::vector<std::unique_ptr<QSTile>>& tiles,
                                                 bool hasVolume, bool hasMedia) {
    double h = m.pad;
    if (hasHeader) { h += kHeaderH; }

    const auto grid = gridTiles(wifiCombo, tiles);
    const int toggleCount = static_cast<int>(grid.size());
    const double contentW = m.panelW - (2.0 * m.pad);
    const double colW = (contentW - (2.0 * m.gap)) / kGridCols;
    const double rowH = gridRowHeight(m, grid, colW);
    int rows = (toggleCount + kGridCols - 1) / kGridCols;
    if (rows > 0) { h += m.gap + (rows * rowH) + ((rows - 1) * m.gap); }

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
    constexpr double kPowerW = 52.0;
    const double headerW = contentW - kPowerW - m.gap;
    if (header != nullptr) {
        header->bounds = {.x = popBounds.x + m.pad, .y = y, .w = headerW, .h = kHeaderH};
    }

    if (power != nullptr) {
        const double px = popBounds.x + popBounds.w - m.pad - kPowerW;
        res.powerBounds = {.x = px, .y = y, .w = kPowerW, .h = kHeaderH};
        power->bounds = res.powerBounds;
    }

    y += kHeaderH + m.gap;

    // 2. 3-Column Toggle Grid
    const auto grid = gridTiles(wifiCombo, tiles);
    const double rowH = gridRowHeight(m, grid, colW);

    int col = 0;
    double rowY = y;
    for (auto* tile : grid) {
        if (col >= kGridCols) {
            col = 0;
            rowY += rowH + m.gap;
        }
        const double tx = popBounds.x + m.pad + (col * (colW + m.gap));
        tile->bounds = {.x = tx, .y = rowY, .w = colW, .h = rowH};
        col++;
    }

    if (!grid.empty()) { y = rowY + rowH; }

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
