#include "waylaunch/switcher/preview_scheduler.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace waylaunch;

int main() {
    std::vector<uintptr_t> handles{1, 2, 3, 4, 5, 6};
    std::unordered_set<uintptr_t> in_flight;
    std::unordered_map<uintptr_t, uint64_t> captured;
    std::unordered_map<uintptr_t, uint64_t> failed;

    auto initial = PreviewScheduler::due(100, handles, 4, in_flight, captured, failed, true);
    assert((initial == std::vector<uintptr_t>{4, 1, 2, 3}));

    captured = {{1, 100}, {2, 100}, {3, 100}, {4, 100}};
    auto queued = PreviewScheduler::due(165, handles, 4, in_flight, captured, failed, true);
    assert((queued == std::vector<uintptr_t>{5, 6}));
    in_flight = {5, 6};
    auto before_selected =
        PreviewScheduler::due(165, handles, 4, in_flight, captured, failed, true);
    assert(before_selected.empty());
    auto selected_due = PreviewScheduler::due(166, handles, 4, in_flight, captured, failed, true);
    assert((selected_due == std::vector<uintptr_t>{4}));
    auto others_due = PreviewScheduler::due(1100, handles, 4, in_flight, captured, failed, true);
    assert((others_due == std::vector<uintptr_t>{4, 1, 2, 3}));

    failed[4] = 2000;
    auto backed_off = PreviewScheduler::due(1999, handles, 4, in_flight, {}, failed, true);
    assert(std::ranges::find(backed_off, 4) == backed_off.end());
    assert((PreviewScheduler::due(2000, handles, 4, in_flight, {}, failed, true) ==
            std::vector<uintptr_t>{4, 1, 2, 3}));

    in_flight.clear();
    auto snapshot = PreviewScheduler::due(1, handles, 4, in_flight, {}, {}, false);
    assert((snapshot == std::vector<uintptr_t>{4, 1, 2, 3}));
    std::unordered_map<uintptr_t, uint64_t> attempted{{4, 1}, {1, 1}, {2, 1}, {3, 1}};
    auto snapshot_rest = PreviewScheduler::due(5000, handles, 4, in_flight, attempted, {}, false);
    assert((snapshot_rest == std::vector<uintptr_t>{5, 6}));
    attempted.emplace(5, 5000);
    attempted.emplace(6, 5000);
    assert(PreviewScheduler::due(5000, handles, 4, in_flight, attempted, {}, false).empty());

    assert(PreviewScheduler::on_hidden_deactivate(9) == 9);
    assert(!PreviewScheduler::on_hidden_deactivate(0));
    std::cout << "Preview scheduler tests passed\n";
}
