// IndicatorHost.cpp - Indicator ownership, bucketing, and QS tile attachment.
#include "ui/statusbar/IndicatorHost.hpp"

#include "ui/statusbar/QuickSettingsPanel.hpp"

namespace qypr {

void IndicatorHost::bucket(std::vector<std::unique_ptr<StatusIndicator>>&& all) {
    auto owned = std::move(all);
    for (auto& ind : owned) {
        if (ind->zone() == Zone::Left) {
            left_.push_back(std::move(ind));
        } else if (ind->zone() == Zone::Center) {
            center_.push_back(std::move(ind));
        } else {
            right_.push_back(std::move(ind));
        }
    }
}

void IndicatorHost::load(const SystemBackends& backends,
                         const IndicatorRegistry::ModuleSelection* sel) {
    clear();
    bucket(IndicatorRegistry::instance().createAll(backends, sel));
}

void IndicatorHost::clear() {
    left_.clear();
    center_.clear();
    right_.clear();
}

void IndicatorHost::attachTiles(QuickSettingsPanel& qs, const SecondaryAction& onSecondary,
                                bool clearFirst) {
    if (clearFirst) { qs.clearTiles(); }
    // One pass over zones in visual order — the ctor and reload both used to
    // triplicate this loop.
    forEach([&](StatusIndicator& ind) {
        if (auto tile = ind.createTile()) {
            // Right-click on a QS tile opens its source indicator's detail
            // popover (the same view the bar icon opens).
            if (onSecondary) {
                tile->setOnSecondary([cb = onSecondary, id = ind.id()] { cb(id); });
            }
            qs.addTile(std::move(tile));
        }
    });
}

StatusIndicator* IndicatorHost::findById(const std::string& id) {
    StatusIndicator* found = nullptr;
    forEach([&](StatusIndicator& ind) {
        if (found == nullptr && ind.id() == id) { found = &ind; }
    });
    return found;
}

StatusIndicator* IndicatorHost::focused() {
    StatusIndicator* found = nullptr;
    forEach([&](StatusIndicator& ind) {
        if (found == nullptr && ind.focused) { found = &ind; }
    });
    return found;
}

}  // namespace qypr
