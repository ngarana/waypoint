#include "waylaunch/switcher/preview_scheduler.h"
#include <algorithm>
#include <unordered_set>

namespace waylaunch {

std::vector<uintptr_t>
PreviewScheduler::due(uint64_t now_ms, const std::vector<uintptr_t>& ordered_handles,
                      uintptr_t selected, const std::unordered_set<uintptr_t>& in_flight,
                      const std::unordered_map<uintptr_t, uint64_t>& last_captured,
                      const std::unordered_map<uintptr_t, uint64_t>& failed_until,
                      bool live_previews) {
    std::vector<uintptr_t> order;
    order.reserve(ordered_handles.size());
    if (selected != 0 && std::ranges::find(ordered_handles, selected) != ordered_handles.end()) {
        order.push_back(selected);
    }
    for (uintptr_t handle_id : ordered_handles) {
        if (handle_id != selected) order.push_back(handle_id);
    }

    std::vector<uintptr_t> result;
    result.reserve(4);
    std::unordered_set<uintptr_t> seen;
    for (uintptr_t handle_id : order) {
        if (handle_id == 0 || !seen.insert(handle_id).second || in_flight.contains(handle_id))
            continue;
        auto const failed = failed_until.find(handle_id);
        if (failed != failed_until.end() && now_ms < failed->second) continue;

        auto const captured = last_captured.find(handle_id);
        if (!live_previews) {
            if (captured != last_captured.end()) continue;
        } else if (captured != last_captured.end()) {
            uint64_t const interval = handle_id == selected ? 66 : 1000;
            if (now_ms < captured->second || now_ms - captured->second < interval) continue;
        }

        result.push_back(handle_id);
        if (result.size() == 4) break;
    }
    return result;
}

std::optional<uintptr_t> PreviewScheduler::on_hidden_deactivate(uintptr_t handle_id) {
    if (handle_id == 0) return std::nullopt;
    return handle_id;
}

} // namespace waylaunch
