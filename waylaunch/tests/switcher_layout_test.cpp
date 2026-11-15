#include "waylaunch/switcher/switcher_layout.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace waylaunch;

int main() {
    SwitcherLayoutConfig config;
    config.preview_height = 160;
    config.show_app_names = true;

    auto single =
        switcher_layout({{.aspect = 1.0}, {.aspect = 1.0}, {.aspect = 1.0}}, 1920, 1080, config);
    assert(single.columns_per_row.size() == 1);
    assert(single.columns_per_row.front() == 3);
    assert(single.panel.x + (single.panel.w / 2) == 960);
    assert(single.panel.y + (single.panel.h / 2) == 540);

    std::vector<SwitcherLayoutEntry> wide_entries(8, {.aspect = 1.0});
    auto wrapped = switcher_layout(wide_entries, 1000, 1200, config);
    assert(wrapped.panel.w <= 900);
    assert(wrapped.columns_per_row.size() > 1);
    assert(wrapped.columns == wrapped.columns_per_row.front());

    std::vector<SwitcherLayoutEntry> ten_entries(10, {.aspect = 1.0});
    auto shrunk = switcher_layout(ten_entries, 1000, 450, config);
    assert(shrunk.preview_height < config.preview_height);
    assert(shrunk.preview_height >= 64);
    assert(shrunk.panel.h <= 360);

    std::vector<SwitcherLayoutEntry> many_entries(20, {.aspect = 1.0});
    config.show_app_names = false;
    auto floor = switcher_layout(many_entries, 320, 200, config);
    assert(floor.preview_height == 64);

    config.show_app_names = false;
    auto aspects =
        switcher_layout({{.aspect = 0.0}, {.aspect = 9.0}, {.aspect = 0.2}}, 1600, 900, config);
    assert(aspects.cards.size() == 3);
    assert(std::abs((static_cast<double>(aspects.cards[0].thumb.w) / aspects.cards[0].thumb.h) -
                    (1600.0 / 900.0)) < 0.02);
    assert(std::abs((static_cast<double>(aspects.cards[1].thumb.w) / aspects.cards[1].thumb.h) -
                    2.2) < 0.02);
    assert(std::abs((static_cast<double>(aspects.cards[2].thumb.w) / aspects.cards[2].thumb.h) -
                    0.6) < 0.02);

    std::cout << "Switcher layout tests passed\n";
}
