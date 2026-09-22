// QuickSettingsLayout.hpp - Geometry layout for the Quick Settings panel.
#pragma once

#include <vector>
#include <memory>

#include "core/Types.hpp"
#include "ui/statusbar/QSTile.hpp"

namespace qypr {

struct QSLayoutMetrics {
    double panelW = 380.0;
    double pad = 14.0;
    double gap = 8.0;
    double gridRowH = 76.0;
};

struct QSLayoutResult {
    Rect popBounds;
    Rect closeBounds;
    Rect powerBounds;
    double contentHeight = 0.0;
};

class QuickSettingsLayout {
public:
    static constexpr double kHeaderH = 52.0;
    static constexpr int kGridCols = 3;
    static constexpr double kVolumeH = 52.0;
    static constexpr double kMediaH = 68.0;
    static constexpr double kSliderH = 48.0;
    static constexpr double kInfoH = 50.0;

    // Measures total content height based on tile presence and metrics.
    static double computeContentHeight(const QSLayoutMetrics& m, bool hasHeader, bool hasWifiCombo,
                                       const std::vector<std::unique_ptr<QSTile>>& tiles,
                                       bool hasVolume, bool hasMedia);

    // Lays out all tiles within popBounds and updates their `bounds`.
    // Returns closeBounds, powerBounds, and total contentHeight.
    static QSLayoutResult layout(const Rect& popBounds, const QSLayoutMetrics& m,
                                 QSHeaderTile* header, QSPowerTile* power,
                                 QSWifiComboTile* wifiCombo,
                                 std::vector<std::unique_ptr<QSTile>>& tiles, QSVolumeTile* volume,
                                 QSMediaTile* media);
};

}  // namespace qypr
