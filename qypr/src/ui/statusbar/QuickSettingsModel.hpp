// QuickSettingsModel.hpp - Grid tile ownership and placement policy.
//
// Owns the panel's grid tile list: insertion order (the grid order),
// indicator-tile registration, the Wi-Fi/volume dedupe rule, and role
// lookup. The panel keeps the fixed single tiles (header, power, wifiCombo,
// volume, media), theme ownership, drawing, and input delegation; layout and
// hit-testing live in QuickSettingsLayout/Input. No Painter, no EventLoop —
// the policy tests without a panel.

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "ui/Theme.hpp"
#include "ui/statusbar/QSTile.hpp"

namespace qypr {

class QuickSettingsModel {
public:
    // Append a grid tile (nulls included, as handed over — every consumer
    // already guards). Order is grid order.
    void addTile(std::unique_ptr<QSTile> tile) { tiles_.push_back(std::move(tile)); }
    void clearTiles() { tiles_.clear(); }

    const std::vector<std::unique_ptr<QSTile>>& tiles() const { return tiles_; }
    std::vector<std::unique_ptr<QSTile>>& tiles() { return tiles_; }

    // Dedupe policy: drop indicator-created Wi-Fi/Volume tiles — the panel
    // owns those roles as singles (wifiCombo_, volume_). Returns how many
    // were removed.
    size_t removePanelOwnedDuplicates();

    bool hasRole(QSTile::Role role) const;
    // First grid tile with the role, or nullptr (the panel checks its singles
    // first — see QuickSettingsPanel::boundsFor).
    const QSTile* findByRole(QSTile::Role role) const;

    // Cascade a re-theme to every owned grid tile.
    void setTheme(const theme::State& state);

private:
    // Indicator-created tiles (toggle, slider, info) placed in the grid.
    std::vector<std::unique_ptr<QSTile>> tiles_;
};

}  // namespace qypr
