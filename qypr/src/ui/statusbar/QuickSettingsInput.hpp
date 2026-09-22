// QuickSettingsInput.hpp - Hit testing and event routing for the Quick Settings panel.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "core/Types.hpp"
#include "ui/statusbar/QSTile.hpp"

namespace qypr {

class QuickSettingsInput {
public:
    struct Context {
        const Rect& popBounds;
        const Rect& closeBounds;
        QSHeaderTile* header = nullptr;
        QSPowerTile* power = nullptr;
        QSWifiComboTile* wifiCombo = nullptr;
        QSVolumeTile* volume = nullptr;
        QSMediaTile* media = nullptr;
        std::vector<std::unique_ptr<QSTile>>& tiles;
        QSTile*& activeDragTile;
        double& curX;
        double& curY;
        bool& closeRequested;
        const std::function<void()>& onOpenWifi;
    };

    static bool handleMotion(Context& ctx, double x, double y);
    static bool handleClick(Context& ctx, double x, double y);
    static bool handleSecondaryClick(Context& ctx, double x, double y);
    static bool handleDrag(Context& ctx, double x, double y);
    static bool handleScroll(Context& ctx, double dx, double dy);
    static bool handleKey(Context& ctx, uint32_t keysym);
};

}  // namespace qypr
