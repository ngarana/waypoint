#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace waylaunch {

class PreviewScheduler {
  public:
    static std::vector<uintptr_t>
    due(uint64_t now_ms, const std::vector<uintptr_t>& ordered_handles, uintptr_t selected,
        const std::unordered_set<uintptr_t>& in_flight,
        const std::unordered_map<uintptr_t, uint64_t>& last_captured,
        const std::unordered_map<uintptr_t, uint64_t>& failed_until, bool live_previews);
    static std::optional<uintptr_t> on_hidden_deactivate(uintptr_t handle_id);
};

} // namespace waylaunch
