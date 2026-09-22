// QSTileFactory.hpp - Factory for Quick Settings tiles and lock-safe fallbacks.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/Theme.hpp"
#include "ui/statusbar/QSTile.hpp"

namespace qypr {

class EventLoop;
struct SystemBackends;

struct BuiltQSTiles {
    std::unique_ptr<QSHeaderTile> header;
    std::unique_ptr<QSPowerTile> power;
    std::unique_ptr<QSWifiComboTile> wifiCombo;
    std::unique_ptr<QSVolumeTile> volume;
    std::unique_ptr<QSMediaTile> media;
    std::vector<std::unique_ptr<QSTile>> fallbackTiles;
};

class QSTileFactory {
public:
    // Builds internal tiles (header, power, wifiCombo, volume, media) and any missing
    // fallback tiles (Bluetooth, Brightness, Dnd, NightLight, KeepAwake, Screenshot)
    // based on existing tiles and available backends.
    static BuiltQSTiles createTiles(EventLoop& loop, const SystemBackends& backends,
                                    const theme::State& theme,
                                    const std::vector<std::unique_ptr<QSTile>>& existingTiles,
                                    const std::function<void()>& onOpenWifi);
};

}  // namespace qypr
