// QuickSettingsModel.cpp - See the header for the design.

#include "ui/statusbar/QuickSettingsModel.hpp"

#include <algorithm>

namespace qypr {

size_t QuickSettingsModel::removePanelOwnedDuplicates() {
    const size_t before = tiles_.size();
    std::erase_if(tiles_, [](const std::unique_ptr<QSTile>& t) {
        if (t == nullptr) { return false; }
        return t->role() == QSTile::Role::Wifi || t->type() == QSTile::Type::Volume ||
               t->role() == QSTile::Role::Volume;
    });
    return before - tiles_.size();
}

bool QuickSettingsModel::hasRole(QSTile::Role role) const {
    return findByRole(role) != nullptr;
}

const QSTile* QuickSettingsModel::findByRole(QSTile::Role role) const {
    for (const auto& t : tiles_) {
        if (t && t->role() == role) { return t.get(); }
    }
    return nullptr;
}

void QuickSettingsModel::setTheme(const theme::State& state) {
    for (const auto& tile : tiles_) {
        if (tile) { tile->setTheme(state); }
    }
}

}  // namespace qypr
