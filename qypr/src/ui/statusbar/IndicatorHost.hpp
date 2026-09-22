// IndicatorHost.hpp - Owns status-bar indicators: bucketing, reload, tiles.
//
// One load() path serves both the StatusBar constructor and reloadModules
// (QYPR_DECOMPOSITION_PLAN step 5): create from the registry, bucket by zone,
// and attach QS tiles with a single secondary-action hook.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

struct SystemBackends;
class QuickSettingsPanel;

class IndicatorHost {
public:
    // Right-click on a QS tile → open the source indicator's detail popover.
    using SecondaryAction = std::function<void(const std::string& indicatorId)>;

    // Create indicators from `sel` (nullptr = every registered module) and
    // bucket them into left/center/right. Does not touch QS tiles.
    void load(const SystemBackends& backends, const IndicatorRegistry::ModuleSelection* sel);

    // Drop every indicator (reload starts from empty).
    void clear();

    // Rebuild the panel's indicator-derived tiles from the current set.
    // `clearFirst` drops existing grid tiles before attaching (reload path).
    void attachTiles(QuickSettingsPanel& qs, const SecondaryAction& onSecondary,
                     bool clearFirst = false);

    std::vector<std::unique_ptr<StatusIndicator>>& left() { return left_; }
    std::vector<std::unique_ptr<StatusIndicator>>& center() { return center_; }
    std::vector<std::unique_ptr<StatusIndicator>>& right() { return right_; }
    const std::vector<std::unique_ptr<StatusIndicator>>& left() const { return left_; }
    const std::vector<std::unique_ptr<StatusIndicator>>& center() const { return center_; }
    const std::vector<std::unique_ptr<StatusIndicator>>& right() const { return right_; }

    // Visit every indicator in zone order (left → center → right).
    template <typename Fn>
    void forEach(Fn&& fn) {
        for (auto* list : {&left_, &center_, &right_}) {
            for (auto& ind : *list) { fn(*ind); }
        }
    }
    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (const auto* list : {&left_, &center_, &right_}) {
            for (const auto& ind : *list) { fn(*ind); }
        }
    }

    StatusIndicator* findById(const std::string& id);
    StatusIndicator* focused();

private:
    void bucket(std::vector<std::unique_ptr<StatusIndicator>>&& all);

    std::vector<std::unique_ptr<StatusIndicator>> left_;
    std::vector<std::unique_ptr<StatusIndicator>> center_;
    std::vector<std::unique_ptr<StatusIndicator>> right_;
};

}  // namespace qypr
