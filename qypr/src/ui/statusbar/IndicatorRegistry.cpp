// IndicatorRegistry.cpp - Plugin registration system implementation
#include "ui/statusbar/IndicatorRegistry.hpp"
#include <algorithm>

namespace qypr {

IndicatorRegistry& IndicatorRegistry::instance() {
    static IndicatorRegistry inst;
    return inst;
}

void IndicatorRegistry::registerIndicator(const std::string& id, Zone zone, int priority,
                                          Factory factory, BundleKind bundle) {
    entries_.push_back({id, zone, priority, bundle, std::move(factory)});
}

std::vector<std::string> IndicatorRegistry::registeredIds() const {
    std::vector<std::string> ids;
    ids.reserve(entries_.size());
    for (const auto& e : entries_) ids.push_back(e.id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::unique_ptr<StatusIndicator>> IndicatorRegistry::createAll(
    const SystemBackends& backends, const ModuleSelection* sel) const {
    const_cast<SystemBackends&>(backends).sync();

    auto isAllowed = [&](const Entry& e) {
        if (e.bundle == BundleKind::Session && !backends.hasSession) { return false; }
        return true;
    };

    std::vector<std::unique_ptr<StatusIndicator>> result;

    // No config: every indicator, grouped by compiled zone, ordered by priority.
    if (!sel) {
        auto sortedEntries = entries_;
        std::sort(sortedEntries.begin(), sortedEntries.end(), [](const Entry& a, const Entry& b) {
            if (a.zone != b.zone) {
                return a.zone < b.zone;  // Group by zone
            }
            return a.priority < b.priority;  // Sort by priority ascending
        });
        for (const auto& entry : sortedEntries) {
            if (!isAllowed(entry)) { continue; }
            if (auto ind = entry.factory(backends)) { result.push_back(std::move(ind)); }
        }
        return result;
    }

    // Config: build each zone in the listed order. StatusBar buckets by zone()
    // and lays out in vector order, so insertion order is the visual order.
    auto buildZone = [&](const std::vector<std::string>& ids, Zone zone) {
        for (const auto& id : ids) {
            auto it = std::find_if(entries_.begin(), entries_.end(),
                                   [&](const Entry& e) { return e.id == id; });
            if (it == entries_.end()) continue;  // unknown id: drop, never crash
            if (!isAllowed(*it)) continue;       // unsafe in current runtime: drop
            if (auto ind = it->factory(backends)) {
                ind->setZone(zone);  // honour the zone it was listed under
                result.push_back(std::move(ind));
            }
        }
    };
    buildZone(sel->left, Zone::Left);
    buildZone(sel->center, Zone::Center);
    buildZone(sel->right, Zone::Right);
    return result;
}

}  // namespace qypr
